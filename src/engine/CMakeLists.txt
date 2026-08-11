add_library(cglab_engine_core INTERFACE)

target_sources(cglab_engine_core INTERFACE
    geometry.h
    render_backend.h
    ../_interface/input.h
    ../_interface/input_router.h
    ../_interface/camera_component.h
    ../_interface/camera_system.h
    ../scene/scene_registry.h
)

target_include_directories(cglab_engine_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(cglab_engine_core INTERFACE glm::glm)

add_library(cglab_engine_runtime STATIC
    engine_runtime.cpp
    engine_runtime.h
    asset_service.h
    runtime_config.h
    sample.h
)

target_include_directories(cglab_engine_runtime
    PUBLIC ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cglab_engine_runtime
    PUBLIC
        cglab_engine_core
    PRIVATE
        cglab_platform_sdl
        cglab_asset_runtime
        control_plane
        utility
        $<$<PLATFORM_ID:Windows>:bcrypt>
)

if(MSVC)
    target_compile_options(cglab_engine_runtime PRIVATE /W4 /FS /utf-8)
endif()

set_target_properties(cglab_engine_runtime PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
