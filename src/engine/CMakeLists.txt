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
