cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CouchModeTests LANGUAGES CXX)
    include(CTest)
endif()

if(TARGET qt_common)
    add_executable(couch_shell_tests couch_shell.cpp
        ../../shader_prepare/vma.cpp
        ../../yuzu/couch/couch_mode.cpp ../../yuzu/couch/couch_mode.h)
    set_target_properties(couch_shell_tests PROPERTIES AUTOMOC ON)
    target_compile_features(couch_shell_tests PRIVATE cxx_std_20)
    target_link_libraries(couch_shell_tests PRIVATE qt_common common frontend_common video_core
        input_common Qt6::Test SDL3::SDL3 ${PLATFORM_LIBRARIES})
    if(UNIX AND NOT APPLE)
        target_link_libraries(couch_shell_tests PRIVATE
            "$<LINK_GROUP:RESCAN,core,video_core,audio_core,hid_core,frontend_common>")
    endif()
    add_test(NAME couch_shell COMMAND couch_shell_tests)
    set_tests_properties(couch_shell PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins;QML_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../qml"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets QuickWidgets QuickControls2 Qml Test)
add_library(couch_test_ui STATIC
    ../../qt_common/couch/widget_model.cpp ../../qt_common/couch/widget_model.h
    ../../qt_common/couch/theme.cpp ../../qt_common/couch/theme.h
    ../../qt_common/couch/native_dialog.cpp ../../qt_common/couch/native_dialog.h
    ../../qt_common/couch/welcome.cpp ../../qt_common/couch/welcome.h
    ../../../dist/qt_themes/couch/couch.qrc)
set_target_properties(couch_test_ui PROPERTIES AUTOMOC ON AUTORCC ON)
target_compile_features(couch_test_ui PUBLIC cxx_std_20)
target_include_directories(couch_test_ui PUBLIC ../..)
target_link_libraries(couch_test_ui PUBLIC Qt6::Widgets Qt6::QuickWidgets Qt6::QuickControls2 Qt6::Qml)
add_executable(couch_mode_tests couch_mode.cpp)
set_target_properties(couch_mode_tests PROPERTIES AUTOMOC ON)
target_link_libraries(couch_mode_tests PRIVATE couch_test_ui Qt6::Test)
target_compile_features(couch_mode_tests PRIVATE cxx_std_20)
if(BUILD_TESTING OR YUZU_TESTS)
    add_test(NAME couch_mode COMMAND couch_mode_tests)
    set_tests_properties(couch_mode PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins;QML_IMPORT_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../qml"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
if(WIN32 AND (BUILD_TESTING OR YUZU_TESTS))
    set_property(TEST couch_mode APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
    if(TARGET couch_shell_tests)
        set_property(TEST couch_shell APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
    endif()
endif()
