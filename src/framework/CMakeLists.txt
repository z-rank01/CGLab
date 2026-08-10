add_library(cglab_framework_runtime STATIC
    engine_runtime.cpp
    engine_runtime.h
    asset_service.h
    runtime_config.h
)

target_include_directories(cglab_framework_runtime
    PUBLIC ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cglab_framework_runtime
    PUBLIC
        cglab_engine_core
    PRIVATE
        cglab_platform_sdl
        cglab_asset_gltf
        control_plane
        utility
        $<$<PLATFORM_ID:Windows>:bcrypt>
)

if(MSVC)
    target_compile_options(cglab_framework_runtime PRIVATE /W4 /FS /utf-8)
endif()

set_target_properties(cglab_framework_runtime PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
