// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/sink/input_processing.h"
#include "audio_core/device/device_session.h"
#include "audio_core/device/audio_buffer.h"
#include "common/scope_exit.h"
#include "tests/audio_core/sysmix_test_context.h"

TEST_CASE("Microphone PCM conversion, gain and level meter", "[audio_core]") {
    using namespace AudioCore::Sink;
    const std::array<s16, 4> stereo{1000, -2000, 20000, -32768};
    const auto amplified = ConvertInputSamples(stereo, 6, 2, 2, 2.0f);
    REQUIRE(amplified == std::vector<s16>({2000, -4000, 32767, -32768, 0, 0}));
    REQUIRE(InputPeak(stereo, 2.0f) == 32768);
    REQUIRE(InputPeak(stereo, 0.0f) == 0);
    REQUIRE(InputPeak(stereo, 0.5f) == 16384);
    REQUIRE(ConvertInputSamples(stereo, 4, 2, 2, 0.0f) == std::vector<s16>(4, 0));
    REQUIRE(ConvertInputSamples({}, 8, 2, 2, 1.0f) == std::vector<s16>(8, 0));
    REQUIRE(ConvertInputSamples(stereo, 12, 2, 6, 1.0f) ==
            std::vector<s16>({1000, -2000, 0, 0, 0, 0, 20000, -32768, 0, 0, 0, 0}));
}

TEST_CASE("Capture uses the input sink gain instead of rereading configuration", "[audio][input]") {
    SysmixTests::Context context;
    const auto previous_gain = Settings::values.microphone_volume.GetValue();
    SCOPE_EXIT {
        Settings::values.microphone_volume.SetValue(previous_gain);
    };
    // SinkStream holds megabytes of inline queues, more than the 1 MiB Windows stack.
    auto owned_stream = std::make_unique<SysmixTests::Stream>(
        context.system, AudioCore::Sink::StreamType::In, 2, true);
    auto& stream = *owned_stream;
    stream.SetSystemChannels(2);
    stream.SetDeviceVolume(0.5f);
    Settings::values.microphone_volume.SetValue(200);
    const std::array<s16, 2> samples{1000, -2000};
    stream.ProcessAudioIn(samples, 1);
    SECTION("sysmix capture") {
        REQUIRE(stream.DrainInputSamples(1) == std::vector<s16>{500, -1000});
    }
    SECTION("legacy capture with underrun padding") {
        REQUIRE(stream.ReleaseBuffer(4) == std::vector<s16>{500, -1000, 0, 0});
    }
}

TEST_CASE("No microphone legacy capture opens no host device and releases silent PCM",
          "[audio][input][no_microphone]") {
    SysmixTests::Context context;
    auto& selected = Settings::values.audio_input_device_id;
    const bool was_global = selected.UsingGlobal();
    const auto previous = selected.GetValue(true);
    selected.SetGlobal(true);
    selected.SetValue(AudioCore::Sink::no_input_device_name);
    SCOPE_EXIT { selected.SetValue(previous); selected.SetGlobal(was_global); };
    // This fixture deliberately has no AudioCore host sinks: any attempted acquisition fails.
    AudioCore::DeviceSession session{context.system};
    REQUIRE(session.Initialize("test capture", AudioCore::SampleFormat::PcmInt16, 2, 0,
                               &*context.process, 0, AudioCore::Sink::StreamType::In).IsSuccess());
    REQUIRE(session.Start().IsSuccess());
    std::array<u8, 32> stale{};
    stale.fill(0x77);
    constexpr u64 address = SysmixTests::Context::Base + 0x1040;
    REQUIRE(context.Memory().WriteBlock(address, stale.data(), stale.size()));
    AudioCore::AudioBuffer buffer{};
    buffer.samples = address;
    buffer.size = stale.size();
    session.ReleaseBuffer(buffer);
    REQUIRE(context.Memory().ReadBlock(address, stale.data(), stale.size()));
    REQUIRE(std::ranges::all_of(stale, [](u8 value) { return value == 0; }));
    session.Stop();
    session.Finalize();
    REQUIRE(session.Initialize("capture again", AudioCore::SampleFormat::PcmInt16, 1, 1,
                               &*context.process, 0, AudioCore::Sink::StreamType::In).IsSuccess());
    session.Finalize();
}
