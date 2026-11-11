# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_library(qt_common STATIC
    forest_theme.cpp
    forest_theme.h
    touch_ui.cpp
    touch_ui.h
    cover_art.cpp
    cover_art.h
    ${PROJECT_SOURCE_DIR}/dist/qt_themes/forest/forest.qrc
    qt_common.h
    qt_common.cpp

    gamemode.cpp
    gamemode.h

    config/uisettings.cpp
    config/uisettings.h
    config/qt_config.cpp
    config/qt_config.h
    config/shared_translation.cpp
    config/shared_translation.h

    util/path.h util/path.cpp
    util/game.h util/game.cpp
    util/meta.h util/meta.cpp
    util/content.h util/content.cpp
    util/rom.h util/rom.cpp
    util/applet.h util/applet.cpp
    util/compress.h util/compress.cpp
    util/fs.h util/fs.cpp
    util/mod.h util/mod.cpp
    util/vk.h util/vk.cpp

    abstract/frontend.h abstract/frontend.cpp
    abstract/progress.h abstract/progress.cpp

    game_list/model.h game_list/model.cpp
    game_list/worker.h game_list/worker.cpp
    game_list/game_list_p.h

    qt_string_lookup.h
    qt_compat.h

    discord/discord.h
    render/context.h
    render/emu_thread.h render/emu_thread.cpp)

if (UNIX)
    target_sources(qt_common PRIVATE gui_settings.cpp gui_settings.h)
endif()

create_target_directory_groups(qt_common)

if (YUZU_TESTS)
    add_executable(mod_rename_tests
        ../tests/qt_common/mod.cpp
        util/mod.cpp)
    target_link_libraries(mod_rename_tests PRIVATE
        frontend_common common Qt6::Widgets QuaZip::QuaZip Catch2::Catch2WithMain)
    add_test(NAME mod_rename_tests COMMAND mod_rename_tests)
    set_tests_properties(mod_rename_tests PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms;QT_FORCE_STDERR_LOGGING=1")
    if (WIN32)
        set_property(TEST mod_rename_tests APPEND PROPERTY
            ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
        set_tests_properties(mod_rename_tests PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    elseif (UNIX)
        set_tests_properties(mod_rename_tests PROPERTIES
            ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endif()

if (USE_DISCORD_PRESENCE)
    target_sources(qt_common PRIVATE
        discord/discord_impl.cpp
        discord/discord_impl.h
    )
    target_link_libraries(qt_common PUBLIC DiscordRPC::discord-rpc httplib::httplib)

    if (YUZU_USE_BUNDLED_OPENSSL)
        target_link_libraries(qt_common PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()

    target_compile_definitions(qt_common PUBLIC USE_DISCORD_PRESENCE)
endif()

# TODO(crueter)
target_link_libraries(qt_common PRIVATE Qt6::Widgets)

target_compile_definitions(qt_common PUBLIC
    # Use QStringBuilder for string concatenation to reduce
    # the overall number of temporary strings created.
    QT_USE_QSTRINGBUILDER

    # Disable implicit conversions from/to C strings
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII

    # Disable implicit type narrowing in signal/slot connect() calls.
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT

    # Disable unsafe overloads of QProcess' start() function.
    QT_NO_PROCESS_COMBINED_ARGUMENT_START

    # Disable implicit QString->QUrl conversions to enforce use of proper resolving functions.
    QT_NO_URL_CAST_FROM_STRING
)

target_link_libraries(qt_common PRIVATE core Qt6::Core Qt6::Concurrent SimpleIni::SimpleIni QuaZip::QuaZip)
target_link_libraries(qt_common PUBLIC frozen::frozen-headers Vulkan::Headers)
target_link_libraries(qt_common PRIVATE gamemode::headers frontend_common)

if (ENABLE_OPENGL)
    target_compile_definitions(qt_common PUBLIC HAS_OPENGL)
endif()

if (UNIX AND NOT APPLE)
    if (DEFINED Qt6Gui_PRIVATE_INCLUDE_DIRS)
        target_include_directories(qt_common PRIVATE ${Qt6Gui_PRIVATE_INCLUDE_DIRS})
    else()
        target_link_libraries(qt_common PRIVATE Qt6::GuiPrivate)
    endif()
endif()

create_target_directory_groups(qt_common)
