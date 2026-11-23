// SPDX-FileCopyrightText: Copyright 2026 Lichii-TGM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "common/scope_exit.h"

using u32 = std::uint32_t;
using DAddr = std::uint64_t;
using GPUVAddr = std::uint64_t;
constexpr std::uint64_t VK_NULL_HANDLE = 0;
constexpr unsigned NULL_BUFFER_ID = 0;
enum class ObtainBufferSynchronize { FullSynchronize };
enum class ObtainBufferOperation { DoNothing };
struct Buffer {
    std::uint64_t handle{};
    unsigned uses{};
    u32 last_offset{}, last_size{};
    auto Handle() const { return handle; }
    void MarkUsage(u32 offset, u32 size) { ++uses; last_offset = offset; last_size = size; }
};
struct Memory {
    std::optional<DAddr> address;
    auto GpuToCpuAddress(GPUVAddr) const { return address; }
    void FlushCaching() {}
};
struct ObservedLock {
    bool held{};
    bool try_lock() { return !std::exchange(held, true); }
    void lock() { if (!try_lock()) throw std::logic_error("Lock was reacquired"); }
    void unlock() { held = false; }
};
struct Cache {
    ObservedLock mutex;
    Memory memory;
    Memory* gpu_memory = &memory;
    std::array<Buffer, 2> slot_buffers{};
    unsigned synced{}, work{};
    DAddr last_address{};
    std::pair<Buffer*, u32> ObtainBuffer(GPUVAddr gpu_addr, u32 size,
                                       ObtainBufferSynchronize sync_info,
                                       ObtainBufferOperation post_op) {
#include "indirect_obtain_buffer.inc"
    }
    std::pair<Buffer*, u32> ObtainCPUBuffer(DAddr address, u32 size,
                                         ObtainBufferSynchronize sync,
                                         ObtainBufferOperation operation) {
        if (size != 12 || sync != ObtainBufferSynchronize::FullSynchronize ||
            operation != ObtainBufferOperation::DoNothing) throw std::runtime_error("read contract changed");
        ++synced; last_address = address;
        return {&slot_buffers[1], 64};
    }
    void NoteWorkRecorded(bool compute) { if (!compute) throw std::runtime_error("not compute"); ++work; }
};
namespace VideoCommon {
template <class C>
auto ObtainIndirectBuffer(C& cache, GPUVAddr address) {
#include "indirect_obtain_arguments.inc"
}
}
struct Commands { unsigned dispatched{}; std::uint64_t buffer{}; u32 offset{}; };
namespace vk {
struct CommandBuffer {
    Commands* commands;
    void DispatchIndirect(std::uint64_t buffer, u32 offset) {
        if (buffer == VK_NULL_HANDLE) throw std::runtime_error("null Vulkan dispatch buffer");
        ++commands->dispatched; commands->buffer = buffer; commands->offset = offset;
    }
};
}
struct Pipeline {
    bool bound = true;
    bool IsBound() const { return bound; }
    std::uint64_t ShaderHash() const { return 123; }
};
struct Scheduler {
    unsigned outside{}, writes{}, traces{}, records{}, gpu_work{};
    std::function<void(vk::CommandBuffer)> command;
    void RequestOutsideRenderPassOperationContext() { ++outside; }
    void NoteComputeWrites() { ++writes; }
    void TraceComputePipeline(std::uint64_t) { ++traces; }
    template <class F> void Record(F&& f) { ++records; command = std::forward<F>(f); }
    template <class F> void TraceGpuWork(vk::CommandBuffer, std::uint64_t, int,
                                      std::array<int, 0>, F&& f) { ++gpu_work; f(); }
};
struct Rasterizer {
    Cache buffer_cache;
    Scheduler scheduler;
    Pipeline pipe;
    unsigned copies{}, published{}, barriers{}, direct{};
    void RecordDispatchBarrier(vk::CommandBuffer) { ++barriers; }
    void SubmitNx2ListCopies() { ++copies; }
    void Run(std::optional<GPUVAddr> indirect_address) {
        auto* pipeline = &pipe;
        const auto publish_collision_map = [&] { ++published; };
#include "indirect_dispatch.inc"
        ++direct;
    }
};
void Require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }

namespace Shader {
using RuntimeError = std::runtime_error;
}
using GLintptr = std::intptr_t;
constexpr u32 GL_DISPATCH_INDIRECT_BUFFER = 0x90ee;

struct ComputeEngine {
    struct { u32 grid_dim_x{1}, grid_dim_y{1}, grid_dim_z{1}; } launch_description;
    std::optional<GPUVAddr> indirect_address;
    bool native{};
    bool IsNativeMode() const { return native; }
    auto GetIndirectComputeAddress() const { return indirect_address; }
};

struct ComputePipeline {
    ObservedLock& buffer_lock;
    ObservedLock& texture_lock;
    bool fail{};
    bool UsesLocalMemory() const { return true; }
    bool WritesGlobalMemory() const { return true; }
    void SetEngine(ComputeEngine*, Memory*) {}
    void Configure() {
        Require(buffer_lock.held && texture_lock.held, "compute configure accessed unlocked caches");
        if (fail) {
            throw std::runtime_error("synthetic setup failure");
        }
    }
};

struct GlDispatch {
    Cache buffer_cache;
    struct { ObservedLock mutex; } texture_cache;
    Memory* gpu_memory = &buffer_cache.memory;
    ComputeEngine engine;
    ComputeEngine* kepler_compute = &engine;
    ComputePipeline pipeline{buffer_cache.mutex, texture_cache.mutex};
    struct {
        ComputePipeline* pipeline;
        ComputePipeline* CurrentComputePipeline() const { return pipeline; }
    } shader_cache{&pipeline};
    struct { void LocalMemoryWarmup() {} } program_manager;
    struct {
        const ObservedLock& buffer_lock;
        const ObservedLock& texture_lock;
        unsigned ticks{};
        bool last_tick_unlocked{};
        void TickWork() {
            ++ticks;
            last_tick_unlocked = !buffer_lock.held && !texture_lock.held;
        }
    } gpu{buffer_cache.mutex, texture_cache.mutex};
    unsigned calls{}, num_queued_commands{};
    bool has_written_global_memory{};

    void glBindBuffer(u32 target, std::uint64_t handle) const {
        Require(target == GL_DISPATCH_INDIRECT_BUFFER && handle == 99, "incorrect indirect buffer");
    }
    void glDispatchComputeIndirect(GLintptr offset) {
        Require(offset == 64, "incorrect argument offset");
        glDispatchCompute(1, 1, 1);
    }
    void glDispatchCompute(u32, u32, u32) {
        Require(buffer_cache.mutex.held && texture_cache.mutex.held, "dispatch lost its cache locks");
        ++calls;
    }
    void Run() {
#include "gl_compute_dispatch.inc"
    }
};

int main() {
    unsigned cases = 0, failures = 0;
    const auto run = [&](const char* name, auto test) {
        ++cases;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    run("unmapped with nullDescriptor", [] {
        Rasterizer r;
        r.Run(0x1000);
        Require(r.scheduler.records == 0, "invalid dispatch was queued");
        Require(r.scheduler.outside == 0 && r.scheduler.writes == 0 && r.scheduler.traces == 0,
                "invalid dispatch recorded work state");
        Require(r.copies == 0 && r.published == 0 && r.buffer_cache.work == 0,
                "invalid dispatch published work");
        Require(r.buffer_cache.synced == 0, "unmapped address entered CPU buffer path");
    });
    for (DAddr mapped : {DAddr{0}, DAddr{0x8000}}) {
        run("mapped dispatch preserves argument use", [=] {
            Rasterizer r; r.buffer_cache.memory.address = mapped;
            r.buffer_cache.slot_buffers[1].handle = 99;
            r.Run(0x1000);
            Require(r.scheduler.records == 1, "mapped dispatch not recorded once");
            Require(r.buffer_cache.synced == 1 && r.buffer_cache.last_address == mapped,
                    "mapped device address lost");
            const auto& buffer = r.buffer_cache.slot_buffers[1];
            Require(buffer.uses == 1 && buffer.last_offset == 64 && buffer.last_size == 12,
                    "argument lifetime/read marking changed");
            Commands commands; r.scheduler.command({&commands});
            Require(commands.dispatched == 1 && commands.buffer == 99 && commands.offset == 64,
                    "mapped buffer/offset changed");
            Require(r.copies == 1 && r.published == 1 && r.buffer_cache.work == 1,
                    "mapped bookkeeping changed");
        });
    }
    run("allocated zero-buffer fallback", [] {
        Rasterizer r; r.buffer_cache.slot_buffers[0].handle = 71;
        r.Run(0x1000);
        Require(r.scheduler.records == 1 && r.buffer_cache.synced == 0, "fallback changed");
        Commands commands; r.scheduler.command({&commands});
        Require(commands.dispatched == 1 && commands.buffer == 71 && commands.offset == 0,
                "fallback buffer was suppressed");
    });
    run("pipeline becomes unbound before execution", [] {
        Rasterizer r; r.buffer_cache.memory.address = 0x8000;
        r.buffer_cache.slot_buffers[1].handle = 99;
        r.Run(0x1000); r.pipe.bound = false;
        Commands commands; r.scheduler.command({&commands});
        Require(commands.dispatched == 0 && r.barriers == 1, "unbound execution guard changed");
    });
    run("direct dispatch does not enter indirect path", [] {
        Rasterizer r; r.Run(std::nullopt);
        Require(r.direct == 1 && r.scheduler.records == 0, "direct routing changed");
    });
    for (const bool indirect : {false, true}) {
        run(indirect ? "OpenGL indirect dispatch cache locks" : "OpenGL direct dispatch cache locks",
            [=] {
                GlDispatch gl;
                if (indirect) {
                    gl.engine.indirect_address = 0x1000;
                    gl.buffer_cache.memory.address = 0x8000;
                    gl.buffer_cache.slot_buffers[1].handle = 99;
                }
                gl.Run();
                Require(gl.calls == 1, "OpenGL did not dispatch exactly once");
                Require(gl.gpu.ticks == 1 && gl.gpu.last_tick_unlocked, "work drain retained cache locks");
            });
    }
    run("OpenGL releases caches after failed setup", [] {
        GlDispatch gl;
        gl.pipeline.fail = true;
        bool failed_setup = false;
        try {
            gl.Run();
        } catch (const std::runtime_error& error) {
            failed_setup = std::string_view{error.what()} == "synthetic setup failure";
        }
        Require(failed_setup && gl.calls == 0, "setup failure was swallowed or dispatch ran");
        Require(gl.gpu.ticks == 1 && gl.gpu.last_tick_unlocked, "unwind retained cache locks");
    });
    run("OpenGL pipeline rejection drains work without locking", [] {
        GlDispatch gl;
        gl.shader_cache.pipeline = nullptr;
        gl.Run();
        Require(gl.calls == 0 && gl.gpu.ticks == 1 && gl.gpu.last_tick_unlocked,
                "legacy pipeline rejection changed");
    });
    std::cout << cases << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
