// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/fiber.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hardware_properties.h"
#include "core/hle/kernel/k_scheduler.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "tests/core/system_test_context.h"

TEST_CASE("Kernel shutdown releases the scheduler-owned host stacks on each core",
          "[core][kernel_core_fibers]") {
    const bool multicore = GENERATE(false, true);
    ScopedSystemTestContext context;
    Settings::values.use_multi_core.SetValue(multicore);
    for (int cycle = 0; cycle < 2; ++cycle) {
        CAPTURE(multicore, cycle);
        Core::System system;
        REQUIRE(system.Initialize());
        auto& kernel = system.Kernel();
        Kernel::KThread bootstrap{kernel};
        REQUIRE(Kernel::KThread::InitializeDummyThread(system, &bootstrap, nullptr).IsSuccess());
        kernel.SetCurrentEmuThread(&bootstrap);
        SCOPE_EXIT {
            kernel.SetCurrentEmuThread(nullptr);
            kernel.UnregisterKernelObject(&bootstrap);
        };
        kernel.Initialize();
        using Fibers = std::array<std::weak_ptr<Common::Fiber>, Core::Hardware::NUM_CPU_CORES>;
        Fibers main_fibers;
        Fibers idle_fibers;
        CHECK(kernel.IsMulticore() == multicore);
        for (size_t core = 0; core < main_fibers.size(); ++core) {
            auto& scheduler = kernel.Scheduler(core);
            if (auto* thread = scheduler.GetSchedulerCurrentThread()) {
                main_fibers[core] = thread->GetHostContext();
            }
            if (auto* thread = scheduler.GetIdleThread()) {
                idle_fibers[core] = thread->GetHostContext();
            }
            CHECK_FALSE(main_fibers[core].expired());
            CHECK_FALSE(idle_fibers[core].expired());
        }
        system.CoreTiming().ClearPendingEvents();
        kernel.Shutdown();
        for (size_t core = 0; core < main_fibers.size(); ++core) {
            CAPTURE(core);
            CHECK(main_fibers[core].expired());
            CHECK(idle_fibers[core].expired());
        }
    }
}
