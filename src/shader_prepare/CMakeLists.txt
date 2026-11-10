# SPDX-License-Identifier: GPL-3.0-or-later
add_library(shader_preparation_gpu STATIC gpu_preparation.cpp gpu_preparation.h)
target_link_libraries(shader_preparation_gpu PUBLIC core video_core common input_common frontend_common
    ${PLATFORM_LIBRARIES} Threads::Threads)
add_executable(lichii-shader-prepare main.cpp vma.cpp)
add_executable(shader_preparation_gpu_smoke EXCLUDE_FROM_ALL
    ../tests/video_core/shader_preparation_gpu_smoke.cpp vma.cpp)
target_link_libraries(shader_preparation_gpu_smoke PRIVATE shader_preparation_gpu)
if(MSVC)
    target_link_options(shader_preparation_gpu_smoke PRIVATE /ENTRY:wmainCRTStartup)
endif()
set_target_properties(lichii-shader-prepare PROPERTIES OUTPUT_NAME "lichii-tgm-shader-prepare")
target_compile_definitions(lichii-shader-prepare PRIVATE QT_NO_KEYWORDS)
target_link_libraries(lichii-shader-prepare PRIVATE shader_preparation_gpu
    nlohmann_json::nlohmann_json Qt6::Core ${PLATFORM_LIBRARIES} Threads::Threads)
set(oot_recipe "${PROJECT_SOURCE_DIR}/dist/recipes/oot/oot-v131072-cache54.nx2recipe")
file(SHA256 "${oot_recipe}" oot_recipe_hash)
if(NOT oot_recipe_hash STREQUAL "85d944c8557fbeffe02af566c9c4277251ed0ab1a0f2a3ab7a626af6a877c3a1")
    message(FATAL_ERROR "The pinned OOT shader preparation recipe does not match its provenance.")
endif()
add_custom_command(TARGET lichii-shader-prepare POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:lichii-shader-prepare>/recipes/oot"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${oot_recipe}"
        "$<TARGET_FILE_DIR:lichii-shader-prepare>/recipes/oot/oot-v131072-cache54.nx2recipe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/dist/recipes/README.md"
        "$<TARGET_FILE_DIR:lichii-shader-prepare>/recipes/README.md"
    VERBATIM)
if(WIN32)
    target_sources(lichii-shader-prepare PRIVATE "${PROJECT_BINARY_DIR}/dist/lichii-tgm-version.rc")
endif()
if(UNIX AND NOT APPLE)
    install(TARGETS lichii-shader-prepare)
    install(FILES "${oot_recipe}" DESTINATION bin/recipes/oot)
    install(FILES "${PROJECT_SOURCE_DIR}/dist/recipes/README.md" DESTINATION bin/recipes)
endif()
if(NOT MSVC)
    target_compile_options(lichii-shader-prepare PRIVATE -Wno-conversion -Wno-unused-variable
        -Wno-unused-parameter -Wno-missing-field-initializers)
    target_compile_options(shader_preparation_gpu PRIVATE -Wno-conversion -Wno-unused-variable
        -Wno-unused-parameter -Wno-missing-field-initializers)
endif()
if(TARGET shader_preparation_ui_tests)
    add_dependencies(shader_preparation_ui_tests lichii-shader-prepare)
endif()
