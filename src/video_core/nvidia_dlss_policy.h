// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <atomic>
#include <string_view>
#include "common/common_types.h"
#include "common/settings_enums.h"

namespace VideoCommon {

class NvidiaDlssActivity {
public:
    void Reset(bool backend_available) noexcept {
        available = backend_available;
        failed.store(false, std::memory_order_relaxed);
        paused.store(false, std::memory_order_relaxed);
        active.store(available, std::memory_order_relaxed);
    }
    void Fail() noexcept { failed.store(true, std::memory_order_relaxed); }
    void BeginFrame(bool pause = false) noexcept {
        paused.store(pause, std::memory_order_relaxed);
        active.store(available && !pause && !failed.load(std::memory_order_relaxed),
                     std::memory_order_relaxed);
    }
    [[nodiscard]] bool Active() const noexcept { return active.load(std::memory_order_relaxed); }
    [[nodiscard]] bool Paused() const noexcept { return paused.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> failed{};
    std::atomic<bool> active{};
    std::atomic<bool> paused{};
    bool available{};
};

[[nodiscard]] constexpr bool NvidiaOotDlssRequested(
    Settings::OotAntiAliasing mode, std::optional<bool> environment = std::nullopt) noexcept {
    return environment.value_or(mode == Settings::OotAntiAliasing::NvidiaDlss ||
                                 mode == Settings::OotAntiAliasing::NvidiaDlaa);
}

[[nodiscard]] constexpr bool NvidiaDlssHdr(int caller, bool environment_default) noexcept {
    return caller >= 0 ? caller != 0 : environment_default;
}

[[nodiscard]] constexpr Settings::NvidiaDlssPreset NvidiaDlssModelPreset(u32 hint) noexcept {
    switch (hint) {
    case 5: return Settings::NvidiaDlssPreset::E;
    case 6: return Settings::NvidiaDlssPreset::F;
    case 10: return Settings::NvidiaDlssPreset::J;
    case 11: return Settings::NvidiaDlssPreset::K;
    case 12: return Settings::NvidiaDlssPreset::L;
    case 13: return Settings::NvidiaDlssPreset::M;
    default: return Settings::NvidiaDlssPreset::NvidiaDefault;
    }
}

/// The platform APIs use the same preset numbers. Existing explicit choices retain priority.
[[nodiscard]] constexpr u32 NvidiaDlssPreset(
    Settings::DlssPreset setting, int caller,
    std::optional<std::string_view> environment = std::nullopt,
    Settings::NvidiaDlssPreset model = Settings::NvidiaDlssPreset::NvidiaDefault) noexcept {
    if (environment) {
        if (*environment == "default") {
            return 0;
        }
        if (!environment->empty()) {
            const char value = environment->front();
            if (value == 'E' || value == 'F' || (value >= 'J' && value <= 'M')) {
                return static_cast<u32>(value - 'A' + 1);
            }
        }
        return 11;
    }
    switch (setting) {
    case Settings::DlssPreset::K: return 11;
    case Settings::DlssPreset::L: return 12;
    case Settings::DlssPreset::M: return 13;
    case Settings::DlssPreset::Auto:
        switch (model) {
        case Settings::NvidiaDlssPreset::E: return 5;
        case Settings::NvidiaDlssPreset::F: return 6;
        case Settings::NvidiaDlssPreset::J: return 10;
        case Settings::NvidiaDlssPreset::K: return 11;
        case Settings::NvidiaDlssPreset::L: return 12;
        case Settings::NvidiaDlssPreset::M: return 13;
        case Settings::NvidiaDlssPreset::NvidiaDefault:
            return caller >= 0 ? static_cast<u32>(caller) : 11;
        }
        break;
    }
    return 11;
}

} // namespace VideoCommon
