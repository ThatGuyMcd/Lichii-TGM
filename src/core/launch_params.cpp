// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <charconv>
#include <regex>
#include <cctype>
#include <string_view>

#include "common/assert.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "common/scm_rev.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/launch_params.h"

#undef _UNICODE
#include <getopt.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

namespace Core {
LaunchParams ParseLaunchParams(Core::System& system, int argc, char *argv[], wchar_t *argv_w[],
                               bool defer_profile_selection) noexcept {
    LaunchParams p{};

    int option_index = 0;
    static struct option long_options[] = {
        // clang-format off
        {"debug", required_argument, 0, 'd'},
        {"config", required_argument, 0, 'c'},
        {"fullscreen", no_argument, 0, 'f'},
        {"help", no_argument, 0, 'h'},
        {"game", required_argument, 0, 'g'},
        {"multiplayer", required_argument, 0, 'm'},
        {"program", optional_argument, 0, 'p'},
        {"user", required_argument, 0, 'u'},
        {"version", no_argument, 0, 'v'},
        {"input-profile", required_argument, 0, 'i'},
        {"null-render", no_argument, 0, 'n'},
        {"singlecore", no_argument, 0, 's'},
        {"filter", no_argument, 0, 'x'},
        {"pack-nup", required_argument, 0, 'N'},
        {"unpack-nup", required_argument, 0, 'U'},
        {"edit-nup", required_argument, 0, 'E'},
        {"nup-name", required_argument, 0, 0x100},
        {"nup-icon", required_argument, 0, 0x101},
        {"nup-version", required_argument, 0, 0x102},
        {"nup-publisher", required_argument, 0, 0x103},
        {"clear-nup-name", no_argument, 0, 0x104},
        {"clear-nup-icon", no_argument, 0, 0x105},
        {"clear-nup-version", no_argument, 0, 0x106},
        {"clear-nup-publisher", no_argument, 0, 0x107},
        {0, 0, 0, 0},
        // clang-format on
    };

    while (optind < argc) {
        if (int arg = getopt_long(argc, argv, "d:c:fhg:m:p:u:vi:nsx", long_options, &option_index); arg != -1) {
            switch (arg) {
            case 'd':
                p.override_gdb_port = uint16_t(atoi(optarg));
                break;
            case 'c':
                p.config_path = optarg;
                break;
            case 'f':
                p.fullscreen = true;
                LOG_INFO(Frontend, "Starting in fullscreen mode...");
                break;
            case 'h':
                p.print_help = true;
                break;
            case 'g':
                p.filepath = std::string(optarg);
                break;
            case 'N':
                p.pack_nup_source = std::string(optarg);
                break;
            case 'U':
                p.unpack_nup_source = std::string(optarg);
                break;
            case 'E':
                p.edit_nup_source = std::string(optarg);
                break;
            case 0x100:
                p.edit_nup_name = std::string(optarg);
                break;
            case 0x101:
                p.edit_nup_icon = std::string(optarg);
                break;
            case 0x102:
                p.edit_nup_version = std::string(optarg);
                break;
            case 0x103:
                p.edit_nup_publisher = std::string(optarg);
                break;
            case 0x104:
                p.edit_nup_name = std::string{};
                break;
            case 0x105:
                p.edit_nup_icon = std::string{};
                break;
            case 0x106:
                p.edit_nup_version = std::string{};
                break;
            case 0x107:
                p.edit_nup_publisher = std::string{};
                break;
            case 'i': {
                p.input_profile = std::string(optarg);
                break;
            }
            case 'm': {
                p.use_multiplayer = true;
                const std::string str_arg(optarg);
                // regex to check if the format is nickname:password@ip:port
                // with optional :password
                const std::regex re("^([^:]+)(?::(.+))?@([^:]+)(?::([0-9]+))?$");
                if (std::regex_match(str_arg, re)) {
                    std::smatch match;
                    std::regex_search(str_arg, match, re);
                    ASSERT(match.size() == 5);
                    p.nickname = match[1];
                    p.password = match[2];
                    p.address = match[3];
                    if (!match[4].str().empty()) {
                        p.port = u16(std::strtoul(match[4].str().c_str(), nullptr, 0));
                    }
                    std::regex nickname_re("^[a-zA-Z0-9._\\- ]+$");
                    ASSERT(std::regex_match(p.nickname, nickname_re) && "Nickname is not valid. Must be 4 to 20 alphanumeric characters");
                    ASSERT(!p.address.empty() && "Address is empty");
                }
                break;
            }
            case 'p':
                p.program_args = argv[optind];
                ++optind;
                break;
            case 'u': {
                // Launch game with a specific user
                const std::string_view argument{optarg};
                p.selected_user.reset();
                p.selected_user_name.clear();
                if (argument.empty()) {
                    LOG_ERROR(Frontend, "Invalid empty user argument");
                    break;
                }
                if (std::ranges::all_of(argument, [](unsigned char c) { return isdigit(c) != 0; })) {
                    int index{};
                    const auto result = std::from_chars(argument.data(), argument.data() + argument.size(), index);
                    if (result.ec != std::errc{} ||
                        index >= static_cast<int>(Service::Account::MAX_USERS)) {
                        LOG_ERROR(Frontend, "User index '{}' must be between 0 and {}", argument,
                                  Service::Account::MAX_USERS - 1);
                        break;
                    }
                    p.selected_user = index;
                } else {
                    // The CLI resolves this again after its configuration selects the NAND.
                    p.selected_user_name = argument;
                    if (defer_profile_selection) {
                        break;
                    }
                    // try to look it up by username, only finds the first username that matches.
                    auto const user_idx = system.GetProfileManager().GetUserIndex(optarg);
                    if (user_idx != std::nullopt) {
                        p.selected_user = static_cast<int>(user_idx.value());
                    } else {
                        LOG_ERROR(Frontend, "Invalid user argument '{}'", optarg);
                        break;
                    }
                }
                if (defer_profile_selection) {
                    break;
                }
                if (system.GetProfileManager().UserExistsIndex(*p.selected_user)) {
                    Settings::values.current_user = s32(*p.selected_user);
                } else {
                    LOG_ERROR(Frontend, "Selected user {} doesn't exist", *p.selected_user);
                }
                break;
            }
            case 'v':
                p.print_version = true;
                break;
            case 'n':
                p.force_null_render = true;
                break;
            case 's':
                p.force_single_core = true;
                break;
            case 'x':
                p.log_filter = argv[optind];
                ++optind;
                break;
            }
        } else {
            // only kept due to shortcuts made by Qt frontend which use "-qlaunch"
            if (strcmp(argv[optind], "-hlaunch") == 0) {
                p.launch_hlaunch = true;
            } else if (strcmp(argv[optind], "-qlaunch") == 0) {
                p.launch_qlaunch = true;
            } else if (strcmp(argv[optind], "-setup") == 0) {
                p.launch_setup = true;
            } else {
                // qt feeds utf8 data, sdl frontend feeds raw utf16 data
#ifdef _WIN32
                p.filepath = argv_w != nullptr
                    ? Common::UTF16ToUTF8(argv_w[optind])
                    : argv[optind];
#else
                p.filepath = argv[optind];
#endif
            }
            optind++;
        }
    }
    p.argv0 = argv[0];
    return p;
}

void ApplyLaunchParams(LaunchParams const& lp) noexcept {
    // apply the log_filter setting
    // the logger was initialized before and doesn't pick up the filter on its own
    Common::Log::Filter filter{};
    filter.ParseFilterString(lp.log_filter.value_or(Settings::values.log_filter.GetValue()));
    Common::Log::SetGlobalFilter(filter);

    if (!lp.program_args.empty()) {
        Settings::values.program_args = lp.program_args;
    }
    if (!lp.input_profile.empty()) {
        auto& players = Settings::values.players.GetValue();
        players[0].profile_name = lp.input_profile;
    }
    if (lp.selected_user.has_value()) {
        Settings::values.current_user = std::clamp(*lp.selected_user, 0, 7);
    }
    if (lp.override_gdb_port.has_value()) {
        Settings::values.use_gdbstub = true;
        Settings::values.gdbstub_port = *lp.override_gdb_port;
    }
    if (lp.force_single_core) {
        Settings::values.use_multi_core = false;
    }
    if (lp.force_null_render) {
        Settings::values.renderer_backend = Settings::RendererBackend::Null;
    }

    if (lp.print_version) {
        LOG_INFO(Frontend, "Eden {} {}", Common::g_scm_branch, Common::g_scm_desc);
    }
    if (lp.print_help) {
        LOG_INFO(Frontend,
            "Usage: {}"
            " [options] <filename>\n"
            "-c, --config          Load the specified configuration file\n"
            "-f, --fullscreen      Start in fullscreen mode\n"
            "-g, --game            File path of the game to load\n"
            "-h, --help            Display this help and exit\n"
            "-m, --multiplayer=nick:password@address:port"
            " Nickname, password, address and port for multiplayer\n"
            "-p, --program         Pass following string as arguments to executable\n"
            "-u, --user            Select a specific user profile from 0 to 7\n"
            "-d, --debug           Run the GDB stub on a port from 1 to 65535\n"
            "-v, --version         Output version information and exit\n"
            "--pack-nup <dump> [<out.nup>]"
            " Pack a loose dump with all its updates and DLC into a NUP file and exit\n"
            "--unpack-nup <file.nup> [<out_dir>]"
            " Extract a NUP back into a loose dump and exit\n"
            "--edit-nup <file.nup> [--nup-name NAME --nup-icon PATH --nup-version VER"
            " --nup-publisher NAME --clear-nup-name --clear-nup-icon --clear-nup-version"
            " --clear-nup-publisher]"
            " Rewrite default-language NUP metadata in place and exit\n",
            lp.argv0
        );
    }
}

void ReloadLaunchProfile(LaunchParams const& lp, Core::System& system) {
    auto& profiles = system.GetProfileManager();
    profiles.ResetUserSaveFile();
    if (lp.selected_user_name.empty()) {
        return;
    }
    const auto index = profiles.GetUserIndex(lp.selected_user_name);
    if (!index) {
        LOG_ERROR(Frontend, "Invalid user argument '{}'", lp.selected_user_name);
    } else if (Settings::values.current_user.GetValue() != static_cast<s32>(*index)) {
        Settings::values.current_user = static_cast<s32>(*index);
        profiles.ResetUserSaveFile();
    }
}

}
