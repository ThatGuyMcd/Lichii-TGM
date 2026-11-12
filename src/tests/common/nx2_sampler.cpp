// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <windows.h>

#include "common/nx2_sampler.h"
#include "common/thread.h"

// A thread spins in generated code whose unwind data doesn't describe its stack, as the JIT's
// doesn't inside a block. Unwinding through it reaches a frame that reads its stack through
// rbp, which holds a guest value here, so the walk has to end at the generated code.
TEST_CASE("Nx2Sampler ends a stack walk at generated code", "[common][nx2_sampler]") {
    auto* const memory = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    REQUIRE(memory != nullptr);
    const auto base = reinterpret_cast<DWORD64>(memory);
    const DWORD64 second_function = base + 0x41;
    const DWORD64 guest_value = 0x1234567812345678; // not a canonical address

    // void Spin(const unsigned char* stop): returns once *stop is set.
    unsigned char spin[] = {
        0x55,                               // push rbp
        0x48, 0x83, 0xEC, 0x28,             // sub rsp, 0x28
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, // mov rax, second_function
        0x48, 0x89, 0x04, 0x24,             // mov [rsp], rax
        0x48, 0xBD, 0, 0, 0, 0, 0, 0, 0, 0, // mov rbp, guest_value
        0xF3, 0x90,                         // pause
        0x80, 0x39, 0x00,                   // cmp byte ptr [rcx], 0
        0x74, 0xF9,                         // je to the pause
        0x48, 0x83, 0xC4, 0x28,             // add rsp, 0x28
        0x5D,                               // pop rbp
        0xC3,                               // ret
    };
    std::memcpy(spin + 7, &second_function, sizeof(second_function));
    std::memcpy(spin + 21, &guest_value, sizeof(guest_value));
    std::memcpy(memory, spin, sizeof(spin));
    std::memset(memory + 0x40, 0x90, 0x10);

    // Spin's unwind data claims no prologue, so its return address is read from [rsp], which
    // points into the second function. That one sets its frame from rbp (UWOP_SET_FPREG).
    const unsigned char no_prologue[] = {0x01, 0x00, 0x00, 0x00};
    const unsigned char frame_from_rbp[] = {0x01, 0x00, 0x01, 0x05, 0x00, 0x03, 0x00, 0x00};
    std::memcpy(memory + 0x100, no_prologue, sizeof(no_prologue));
    std::memcpy(memory + 0x108, frame_from_rbp, sizeof(frame_from_rbp));
    auto* const functions = reinterpret_cast<RUNTIME_FUNCTION*>(memory + 0x200);
    functions[0].BeginAddress = 0x00;
    functions[0].EndAddress = 0x40;
    functions[0].UnwindData = 0x100;
    functions[1].BeginAddress = 0x40;
    functions[1].EndAddress = 0x50;
    functions[1].UnwindData = 0x108;
    REQUIRE(RtlAddFunctionTable(functions, 2, base));

    std::atomic<unsigned char> stop{0};
    std::atomic<bool> started{false};
    std::thread thread{[&] {
        Common::SetCurrentThreadName("Nx2SamplerTest");
        started = true;
        reinterpret_cast<void (*)(const void*)>(memory)(&stop);
    }};
    while (!started) {
        std::this_thread::yield();
    }
    const auto out = std::filesystem::temp_directory_path() / "nx2_sampler_test.txt";
    const std::string report = Nx2Sampler::Sample("Nx2SamplerTest", 0.2, out.string());
    stop = 1;
    thread.join();
    RtlDeleteFunctionTable(functions);
    VirtualFree(memory, 0, MEM_RELEASE);
    std::ifstream raw_input{out.string() + ".raw.json"};
    REQUIRE(raw_input.good());
    const auto raw = nlohmann::json::parse(raw_input);
    CHECK(raw.at("format") == "nx2-samples-raw");
    CHECK(raw.at("sample_count").get<size_t>() > 0);
    CHECK(raw.at("sample_count") == raw.at("samples").size());
    bool sampled_jit = false;
    for (const auto& sample : raw.at("samples")) {
        if (!sample.at("frames").empty()) {
            const auto pc = std::stoull(sample.at("frames")[0].get<std::string>(), nullptr, 16);
            if (pc >= base && pc < base + 0x1000) {
                sampled_jit = true;
                CHECK(sample.at("frames").size() == 1);
            }
        }
    }
    CHECK(sampled_jit);
    raw_input.close();
    std::filesystem::remove(out);
    std::filesystem::remove(out.string() + ".folded");
    std::filesystem::remove(out.string() + ".raw.json");

    CHECK(report.find("(jit)") != std::string::npos);
}
