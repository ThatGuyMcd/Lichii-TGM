cmake_minimum_required(VERSION 3.22)

# This target can also run alone, without configuring emulator dependencies:
# cmake -S src/tests/qt_forest -B build-forest-tests -DCMAKE_PREFIX_PATH=<Qt>
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ForestSettingsTests LANGUAGES CXX)
    include(CTest)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Test)
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

if (BUILD_TESTING)
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
endif()
