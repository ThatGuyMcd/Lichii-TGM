// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include "common/settings.h"

namespace QtCommon {

// Adapted from Lichii #1687/#1172: undo session hotkeys without replacing a saved choice.
class SessionSpeedLimit {
public:
    bool BeginHotkey(bool emulation_running) {
        if (!emulation_running) {
            return false;
        }
        if (!before) {
            before = State{Settings::values.use_speed_limit.GetValue(),
                           Settings::values.use_speed_limit.UsingGlobal()};
        }
        return true;
    }

    void Reset() {
        before.reset();
    }

    void Restore(bool configuration_applied) {
        auto& limit = Settings::values.use_speed_limit;
        if (before && !configuration_applied && limit.UsingGlobal() == before->using_global) {
            limit.SetValue(before->value);
        }
        Reset();
        Settings::values.current_speed_mode = Settings::SpeedMode::Standard;
    }

private:
    struct State {
        bool value;
        bool using_global;
    };
    std::optional<State> before;
};

} // namespace QtCommon
