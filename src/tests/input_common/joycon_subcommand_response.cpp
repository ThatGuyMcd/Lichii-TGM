// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "input_common/helpers/joycon_protocol/poller.h"
#include "input_common/helpers/joycon_protocol/subcommand_response.h"

namespace {
using Common::Input::DriverResult;
using namespace InputCommon::Joycon;

struct Packet {
    int size;
    ReportMode report;
    SubCommand command;
    u8 payload;
};

class ScriptedReader {
public:
    explicit ScriptedReader(std::vector<Packet> packets_) : packets{std::move(packets_)} {}

    int operator()(SubCommandResponse& output) {
        REQUIRE(reads < 12);
        const size_t index = reads++;
        if (index >= packets.size()) {
            return 0;
        }
        const auto& packet = packets[index];
        if (packet.size > 0) {
            SubCommandResponse reply{};
            reply.input_report.report_mode = packet.report;
            reply.sub_command = packet.command;
            reply.command_data.fill(packet.payload);
            std::memcpy(&output, &reply,
                        std::min(static_cast<size_t>(packet.size), sizeof(reply)));
        }
        return packet.size;
    }

    size_t reads{};

private:
    std::vector<Packet> packets;
};
} // namespace

TEST_CASE("Joy-Con replies must match both report and subcommand", "[joycon_reply]") {
    Packet unrelated{49, ReportMode::SUBCMD_REPLY, SubCommand::REQ_DEV_INFO, 0x11};
    SECTION("Unrelated subcommand reply") {}
    SECTION("Input report with a coincidental command byte") {
        unrelated.report = ReportMode::STANDARD_FULL_60HZ;
        unrelated.command = SubCommand::SET_REPORT_MODE;
    }
    ScriptedReader reader{{unrelated,
                           {49, ReportMode::SUBCMD_REPLY, SubCommand::SET_REPORT_MODE, 0x22}}};
    SubCommandResponse output{};
    REQUIRE(ReadSubCommandResponse(SubCommand::SET_REPORT_MODE, output, reader) ==
            DriverResult::Success);
    CHECK(reader.reads == 2);
    CHECK(output.input_report.report_mode == ReportMode::SUBCMD_REPLY);
    CHECK(output.sub_command == SubCommand::SET_REPORT_MODE);
    CHECK(output.command_data[0] == 0x22);
}

TEST_CASE("Joy-Con reads cannot reuse stale or truncated reply fields", "[joycon_reply]") {
    for (const int size : {-1, 0, 1, 14, 15, 48}) {
        CAPTURE(size);
        ScriptedReader reader{std::vector<Packet>(
            12, {size, ReportMode::SUBCMD_REPLY, SubCommand::SET_REPORT_MODE, 0x44})};
        SubCommandResponse output{};
        output.input_report.report_mode = ReportMode::SUBCMD_REPLY;
        output.sub_command = SubCommand::SET_REPORT_MODE;
        output.command_data.fill(0x77);
        CHECK(ReadSubCommandResponse(SubCommand::SET_REPORT_MODE, output, reader) ==
              DriverResult::Timeout);
        CHECK(reader.reads == 12);
        const SubCommandResponse empty{};
        CHECK(std::memcmp(&output, &empty, sizeof(output)) == 0);
    }
}

TEST_CASE("Joy-Con response reads accept standard and padded packets", "[joycon_reply]") {
    for (const int size : {49, 64}) {
        CAPTURE(size);
        ScriptedReader reader{{{size, ReportMode::SUBCMD_REPLY, SubCommand::SET_REPORT_MODE, 0x66}}};
        SubCommandResponse output{};
        REQUIRE(ReadSubCommandResponse(SubCommand::SET_REPORT_MODE, output, reader) ==
                DriverResult::Success);
        CHECK(reader.reads == 1);
        CHECK(output.command_data[0] == 0x66);
        CHECK(output.command_data[33] == 0x66);
        CHECK(output.command_data[34] == (size == 49 ? 0 : 0x66));
    }
}

TEST_CASE("Joy-Con unmatched replies exhaust a bounded read budget", "[joycon_reply]") {
    std::vector<Packet> packets(12, {49, ReportMode::SUBCMD_REPLY, SubCommand::REQ_DEV_INFO, 0x11});
    SECTION("All replies mismatch") {
        ScriptedReader reader{packets};
        SubCommandResponse output{};
        CHECK(ReadSubCommandResponse(SubCommand::SET_REPORT_MODE, output, reader) ==
              DriverResult::Timeout);
        CHECK(reader.reads == 12);
    }
    SECTION("Final read matches") {
        packets.back().command = SubCommand::SET_REPORT_MODE;
        packets.back().payload = 0x55;
        ScriptedReader reader{packets};
        SubCommandResponse output{};
        CHECK(ReadSubCommandResponse(SubCommand::SET_REPORT_MODE, output, reader) ==
              DriverResult::Success);
        CHECK(reader.reads == 12);
        CHECK(output.command_data[0] == 0x55);
    }
}

TEST_CASE("Joy-Con battery fallback survives passive input and yields to the first report",
          "[joycon_battery]") {
    std::vector<u8> batteries;
    JoyconCallbacks callbacks{
        .on_battery_data = [&](Battery battery) { batteries.push_back(battery.raw); },
        .on_button_data = [](int, bool) {},
        .on_stick_data = [](int, float) {},
    };
    constexpr JoyStickCalibration stick{{.max = 2047, .min = 2048, .center = 2048},
                                        {.max = 2047, .min = 2048, .center = 2048}};
    JoyconPoller poller{ControllerType::Left, stick, {}, {}, std::move(callbacks)};
    poller.SeedBattery();
    REQUIRE(batteries == std::vector<u8>{0x80});

    InputReportPassive passive{};
    passive.report_mode = ReportMode::SIMPLE_HID_MODE;
    std::array<u8, sizeof(passive)> passive_bytes{};
    std::memcpy(passive_bytes.data(), &passive, sizeof(passive));
    poller.ReadPassiveMode(passive_bytes);
    REQUIRE(batteries == std::vector<u8>{0x80});

    InputReportActive active{};
    active.report_mode = ReportMode::STANDARD_FULL_60HZ;
    active.battery_status.raw = 0;
    std::array<u8, sizeof(active)> active_bytes{};
    std::memcpy(active_bytes.data(), &active, sizeof(active));
    poller.ReadActiveMode(active_bytes, {}, {});
    REQUIRE(batteries == std::vector<u8>{0x80, 0x00});

    active.battery_status.raw = 0x30;
    std::memcpy(active_bytes.data(), &active, sizeof(active));
    poller.ReadActiveMode(active_bytes, {}, {});
    CHECK(batteries == std::vector<u8>{0x80, 0x00, 0x30});
}
