cmake_minimum_required(VERSION 3.22)

# This target can also run alone, without configuring emulator dependencies:
# cmake -S src/tests/qt_forest -B build-forest-tests -DCMAKE_PREFIX_PATH=<Qt>
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ForestSettingsTests LANGUAGES CXX)
    include(CTest)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Test Concurrent)
add_library(forest_test_widgets STATIC
    ../../qt_common/forest_theme.cpp
    ../../qt_common/forest_theme.h
    ../../qt_common/touch_ui.cpp
    ../../qt_common/touch_ui.h
    ../../qt_common/cover_art.cpp
    ../../qt_common/cover_art.h
    ../../../dist/qt_themes/forest/forest.qrc)
set_target_properties(forest_test_widgets PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(forest_test_widgets PUBLIC cxx_std_20)
target_include_directories(forest_test_widgets PUBLIC ../..)
target_link_libraries(forest_test_widgets PUBLIC Qt6::Widgets Qt6::Concurrent)
target_compile_definitions(forest_test_widgets PUBLIC
    QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_USE_QSTRINGBUILDER)
add_executable(forest_theme_tests forest_theme.cpp)
set_target_properties(forest_theme_tests PROPERTIES AUTOMOC ON)
target_link_libraries(forest_theme_tests PRIVATE forest_test_widgets Qt6::Test)

option(FOREST_BUILD_PREVIEW "Build standalone settings form screenshot tool" OFF)
if (FOREST_BUILD_PREVIEW)
    find_package(Qt6 REQUIRED COMPONENTS UiTools)
    add_executable(forest_ui_preview preview.cpp)
    target_link_libraries(forest_ui_preview PRIVATE forest_test_widgets Qt6::UiTools)
endif()
add_executable(settings_navigation_tests settings_navigation.cpp)
target_compile_features(settings_navigation_tests PRIVATE cxx_std_20)
target_link_libraries(settings_navigation_tests PRIVATE Qt6::Widgets Qt6::Test)
target_include_directories(settings_navigation_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../..")
target_compile_definitions(settings_navigation_tests PRIVATE
    QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_URL_CAST_FROM_STRING
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT QT_USE_QSTRINGBUILDER)
if (MSVC)
    target_compile_options(settings_navigation_tests PRIVATE /EHsc /GR-)
else()
    target_compile_options(settings_navigation_tests PRIVATE -fno-rtti -Wall -Wextra -Werror)
endif()

if (BUILD_TESTING OR YUZU_TESTS)
    add_test(NAME settings_navigation COMMAND settings_navigation_tests)
    set_tests_properties(settings_navigation PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms;QT_FORCE_STDERR_LOGGING=1")
    if (WIN32)
        set_property(TEST settings_navigation APPEND PROPERTY
            ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
        set_tests_properties(settings_navigation PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    elseif (UNIX)
        set_tests_properties(settings_navigation PROPERTIES
            ENVIRONMENT_MODIFICATION "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
    add_test(NAME forest_theme COMMAND forest_theme_tests)
    get_test_property(settings_navigation ENVIRONMENT forest_test_environment)
    get_test_property(settings_navigation ENVIRONMENT_MODIFICATION forest_test_modification)
    set_tests_properties(forest_theme PROPERTIES TIMEOUT 30
        ENVIRONMENT "${forest_test_environment}"
        ENVIRONMENT_MODIFICATION "${forest_test_modification}")
    if (TARGET common)
        add_executable(system_settings_tests system_settings.cpp
            ../../yuzu/configuration/configure_system.cpp
            ../../yuzu/configuration/configure_system.h
            ../../yuzu/configuration/configure_system.ui
            ../../yuzu/configuration/shared_widget.cpp
            ../../yuzu/configuration/shared_widget.h
            ../../yuzu/configuration/configuration_shared.cpp)
        set_target_properties(system_settings_tests PROPERTIES AUTOMOC ON AUTOUIC ON)
        target_include_directories(system_settings_tests PRIVATE ../..)
        target_compile_features(system_settings_tests PRIVATE cxx_std_20)
        target_link_libraries(system_settings_tests PRIVATE core frontend_common qt_common common
            Qt6::Widgets Qt6::Test)
        add_test(NAME system_settings COMMAND system_settings_tests)
        set_tests_properties(system_settings PROPERTIES TIMEOUT 45
            ENVIRONMENT "${forest_test_environment}"
            ENVIRONMENT_MODIFICATION "${forest_test_modification}")
        add_executable(shader_preparation_ui_tests shader_preparation.cpp
            ../../yuzu/shader_preparation_job.cpp ../../yuzu/shader_preparation_job.h)
        set_target_properties(shader_preparation_ui_tests PROPERTIES AUTOMOC ON)
        target_compile_features(shader_preparation_ui_tests PRIVATE cxx_std_20)
        target_include_directories(shader_preparation_ui_tests PRIVATE ../..)
        target_link_libraries(shader_preparation_ui_tests PRIVATE common Qt6::Widgets Qt6::Test)
        add_test(NAME shader_preparation_ui COMMAND shader_preparation_ui_tests)
        set_tests_properties(shader_preparation_ui PROPERTIES TIMEOUT 45
            ENVIRONMENT "${forest_test_environment}"
            ENVIRONMENT_MODIFICATION "${forest_test_modification}")
    endif()
endif()
