// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "common/logging.h"
#include "common/scope_exit.h"
#include "core/internal_network/network.h"
#include "core/internal_network/sockets.h"

#ifdef _WIN32
TEST_CASE("Network::DefaultProtocolDoesNotReportUnsupported", "[core][sockets]") {
    // Exercise the real translation via socket creation. The old Windows fallback returned
    // zero too, so success alone would miss the erroneous critical diagnostic.
    Common::Log::Initialize();
    Common::Log::SetRingCapacity(64);
    SCOPE_EXIT { Common::Log::SetRingCapacity(0); };
    LOG_CRITICAL(Debug, "Default protocol regression diagnostic control");
    const auto control = Common::Log::ReadRing(0);
    REQUIRE(std::ranges::any_of(control, [](const auto& line) {
        return line.text.find("Default protocol regression diagnostic control") != std::string::npos;
    }));
    const u64 start = control.back().sequence;
    Network::NetworkInstance network_instance;
    Network::Socket v4;
    CHECK(v4.Initialize(Network::Domain::INET, Network::Type::DGRAM, Network::Protocol::IP) ==
          Network::Errno::SUCCESS);
    Network::Socket v6;
    const auto ipv6 =
        v6.Initialize(Network::Domain::INET6, Network::Type::DGRAM, Network::Protocol::IP);
    INFO("IPv6 socket result (host IPv6 support is optional): " << static_cast<int>(ipv6));
    for (const auto& line : Common::Log::ReadRing(start)) {
        INFO(line.text);
        CHECK(line.text.find("Unimplemented protocol") == std::string::npos);
    }
}
#endif

TEST_CASE("Network::Errors", "[core]") {
    Network::NetworkInstance network_instance; // initialize network

    Network::Socket socks[2];
    for (Network::Socket& sock : socks) {
        REQUIRE(sock.Initialize(Network::Domain::INET, Network::Type::STREAM,
                                Network::Protocol::TCP) == Network::Errno::SUCCESS);
    }

    Network::SockAddrIn addr{
        Network::Domain::INET,
        {127, 0, 0, 1},
        1, // hopefully nobody running this test has something listening on port 1
    };
    REQUIRE(socks[0].Connect(addr) == Network::Errno::CONNREFUSED);

    std::vector<u8> message{1, 2, 3, 4};
    REQUIRE(socks[1].Recv(0, message).second == Network::Errno::NOTCONN);
}

TEST_CASE("Network::PerCallNonblockingReceive", "[core][sockets]") {
    using namespace std::chrono_literals;
    Network::NetworkInstance network_instance;
    Network::Socket receiver;
    Network::Socket sender;
    REQUIRE(receiver.Initialize(Network::Domain::INET, Network::Type::DGRAM,
                                Network::Protocol::UDP) == Network::Errno::SUCCESS);
    REQUIRE(sender.Initialize(Network::Domain::INET, Network::Type::DGRAM,
                              Network::Protocol::UDP) == Network::Errno::SUCCESS);
    REQUIRE(receiver.Bind({Network::Domain::INET, {127, 0, 0, 1}, 0}) ==
            Network::Errno::SUCCESS);
    REQUIRE(receiver.SetRcvTimeo(2000) == Network::Errno::SUCCESS);
    const auto [address, address_error] = receiver.GetSockName();
    REQUIRE(address_error == Network::Errno::SUCCESS);

    auto blocking = std::async(std::launch::async, [&] {
        std::array<u8, 1> buffer{};
        const auto [size, error] = receiver.RecvFrom(0, buffer, nullptr);
        return std::pair{std::pair{size, error}, buffer[0]};
    });
    const bool remained_blocked = blocking.wait_for(100ms) == std::future_status::timeout;
    auto no_wait = std::async(std::launch::async, [&] {
        std::array<u8, 1> buffer{};
        return receiver.RecvFrom(Network::FLAG_MSG_DONTWAIT, buffer, nullptr);
    });
    const bool returned_promptly = no_wait.wait_for(1s) == std::future_status::ready;
    const std::array<u8, 1> data{'x'};
    const auto first_send = sender.SendTo(0, data, &address);
    const auto second_send = sender.SendTo(0, data, &address);
    const auto no_wait_result = no_wait.get();
    const auto [blocking_result, received] = blocking.get();

    CHECK(remained_blocked);
    CHECK(returned_promptly);
    CHECK(no_wait_result.first == -1);
    CHECK(no_wait_result.second == Network::Errno::AGAIN);
    CHECK(first_send.second == Network::Errno::SUCCESS);
    CHECK(second_send.second == Network::Errno::SUCCESS);
    CHECK(blocking_result.first == 1);
    CHECK(blocking_result.second == Network::Errno::SUCCESS);
    CHECK(received == 'x');
}
