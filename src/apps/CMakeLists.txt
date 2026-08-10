# Shared command-line parsing and runtime startup used by all CGLab apps.
add_library(cglab_application_runner STATIC
    application_runner.cpp
    application_runner.h
    application_options.cpp
    application_options.h
)

target_include_directories(cglab_application_runner
    PUBLIC ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cglab_application_runner
    PUBLIC
        cglab_framework_runtime
    PRIVATE
        utility
)

if(MSVC)
    target_compile_options(cglab_application_runner PRIVATE /W4 /FS /utf-8)
endif()

set_target_properties(cglab_application_runner PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Dedicated sample executables. Both are part of the default ALL build; use
# `cmake --build build --target <target>` when only one app is needed.
add_executable(TriangleSample
    triangle_sample.cpp
)

add_executable(GltfSponzaSample
    gltf_sponza_sample.cpp
)

foreach(cglab_app IN ITEMS TriangleSample GltfSponzaSample)
    target_include_directories(${cglab_app}
        PRIVATE ${PROJECT_SOURCE_DIR}/src
    )

    target_link_libraries(${cglab_app}
        PRIVATE
            cglab_application_runner
            cglab_vulkan_backend
    )

    target_compile_definitions(${cglab_app}
        PRIVATE CGLAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )

    if(MSVC)
        target_compile_options(${cglab_app} PRIVATE /W4 /FS /utf-8)
    endif()
endforeach()

# Preserve the paths used by existing scripts and documentation.
set_target_properties(TriangleSample GltfSponzaSample PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
