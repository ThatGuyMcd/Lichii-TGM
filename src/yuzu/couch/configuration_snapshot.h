// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <tuple>
#include "common/settings.h"
#include "qt_common/config/uisettings.h"

// Native subdialogs may update live settings before the enclosing dialog is applied.
// Capture globals and active overrides without toggling live inheritance during UI reads.
class CouchConfigurationSnapshot {
    struct Scalar {
        Settings::BasicSetting* setting;
        std::string global;
        std::string local;
        bool inherited;
    };
    std::vector<Scalar> scalars;
    std::array<Settings::PlayerInput, 10> global_players, local_players;
    bool players_global{};
    Settings::TouchscreenInput touchscreen;
    Settings::ButtonsRaw debug_buttons;
    Settings::AnalogsRaw debug_analogs;
    std::vector<Settings::TouchFromButtonMap> touch_maps;
    std::vector<std::string> external_dirs;
    std::string ringcon, desktop_theme;
    QVector<UISettings::GameDir> game_dirs;
    std::vector<UISettings::Shortcut> shortcuts;

public:
    CouchConfigurationSnapshot() {
        Capture();
    }
    void Capture() {
        scalars.clear();
        for (auto* linkage : {&Settings::values.linkage, &UISettings::values.linkage}) {
            for (const auto& [key, setting] : linkage->by_key) {
                const bool inherited = setting->UsingGlobal();
                const auto global = setting->ToStringGlobal();
                const auto local = inherited ? std::string{} : setting->ToString();
                scalars.push_back({setting, global, local, inherited});
            }
        }
        auto& players = Settings::values.players;
        players_global = players.UsingGlobal();
        global_players = players.GetValue(true);
        if (!players_global)
            local_players = players.GetValue();
        touchscreen = Settings::values.touchscreen;
        debug_buttons = Settings::values.debug_pad_buttons;
        debug_analogs = Settings::values.debug_pad_analogs;
        touch_maps = Settings::values.touch_from_button_maps;
        external_dirs = Settings::values.external_content_dirs;
        ringcon = Settings::values.ringcon_analogs;
        desktop_theme = UISettings::values.theme;
        game_dirs = UISettings::values.game_dirs;
        shortcuts = UISettings::values.shortcuts;
    }
    bool ScalarsChanged() const {
        for (const auto& scalar : scalars) {
            const bool inherited = scalar.setting->UsingGlobal();
            const bool global_changed = scalar.setting->ToStringGlobal() != scalar.global;
            const bool local_changed =
                !scalar.inherited && !inherited && scalar.setting->ToString() != scalar.local;
            if (global_changed || local_changed || inherited != scalar.inherited)
                return true;
        }
        return UISettings::values.theme != desktop_theme;
    }
    bool Changed() const {
        if (ScalarsChanged())
            return true;
        auto fields = [](const Settings::PlayerInput& p) {
            return std::tie(p.connected, p.controller_type, p.buttons, p.analogs, p.motions,
                            p.vibration_enabled, p.vibration_strength, p.body_color_left,
                            p.body_color_right, p.button_color_left, p.button_color_right,
                            p.profile_name, p.use_system_vibrator);
        };
        auto same = [&](const auto& a, const auto& b) {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                              [&](const auto& x, const auto& y) { return fields(x) == fields(y); });
        };
        auto& players = Settings::values.players;
        const bool inherited = players.UsingGlobal();
        const bool global_changed = !same(players.GetValue(true), global_players);
        const bool local_changed =
            !players_global && !inherited && !same(players.GetValue(), local_players);
        if (global_changed || local_changed || inherited != players_global)
            return true;
        const auto& touch = Settings::values.touchscreen;
        if (std::tie(touch.enabled, touch.device, touch.finger, touch.diameter_x, touch.diameter_y,
                     touch.rotation_angle) !=
            std::tie(touchscreen.enabled, touchscreen.device, touchscreen.finger,
                     touchscreen.diameter_x, touchscreen.diameter_y, touchscreen.rotation_angle))
            return true;
        if (Settings::values.debug_pad_buttons != debug_buttons ||
            Settings::values.debug_pad_analogs != debug_analogs ||
            Settings::values.external_content_dirs != external_dirs ||
            Settings::values.ringcon_analogs != ringcon)
            return true;
        const auto& maps = Settings::values.touch_from_button_maps;
        if (!std::equal(maps.begin(), maps.end(), touch_maps.begin(), touch_maps.end(),
                        [](const auto& a, const auto& b) {
                            return a.name == b.name && a.buttons == b.buttons;
                        }))
            return true;
        const auto& dirs = UISettings::values.game_dirs;
        if (!std::equal(dirs.begin(), dirs.end(), game_dirs.begin(), game_dirs.end(),
                        [](const auto& a, const auto& b) {
                            return a.path == b.path && a.deep_scan == b.deep_scan &&
                                   a.expanded == b.expanded;
                        }))
            return true;
        return false;
    }
    void Restore() const {
        for (const auto& scalar : scalars) {
            const auto restore = [&scalar](const std::string& value) {
                if (scalar.setting->ToString() == value)
                    return;
                // LoadString treats an empty string as "reset to default".
                if (scalar.setting->TypeId() == "string" && !scalar.setting->Ranged())
                    static_cast<Settings::Setting<std::string>*>(scalar.setting)->SetValue(value);
                else
                    scalar.setting->LoadString(value);
            };
            scalar.setting->SetGlobal(true);
            restore(scalar.global);
            if (!scalar.inherited) {
                scalar.setting->SetGlobal(false);
                restore(scalar.local);
            }
            scalar.setting->SetGlobal(scalar.inherited);
        }
        auto& players = Settings::values.players;
        players.GetValue(true) = global_players;
        if (!players_global) {
            players.SetGlobal(false);
            players.GetValue() = local_players;
        }
        players.SetGlobal(players_global);
        Settings::values.touchscreen = touchscreen;
        Settings::values.debug_pad_buttons = debug_buttons;
        Settings::values.debug_pad_analogs = debug_analogs;
        Settings::values.touch_from_button_maps = touch_maps;
        Settings::values.external_content_dirs = external_dirs;
        Settings::values.ringcon_analogs = ringcon;
        UISettings::values.theme = desktop_theme;
        UISettings::values.game_dirs = game_dirs;
        UISettings::values.shortcuts = shortcuts;
        UISettings::values.configuration_applied = false;
    }
};
