if(NOT DEFINED CGLAB_SOURCE_DIR)
    message(FATAL_ERROR "CGLAB_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE _engine_headers
    LIST_DIRECTORIES false
    "${CGLAB_SOURCE_DIR}/src/engine/*.h"
    "${CGLAB_SOURCE_DIR}/src/engine/*.hpp"
)

set(_engine_forbidden_includes
    "vulkan/"
    "vulkan\\.h"
    "SDL3/"
    "tiny_gltf"
    "render_graph/"
)

foreach(_header IN LISTS _engine_headers)
    file(READ "${_header}" _contents)
    foreach(_forbidden IN LISTS _engine_forbidden_includes)
        if(_contents MATCHES "#[ \t]*include[^\n]*${_forbidden}")
            file(RELATIVE_PATH _relative "${CGLAB_SOURCE_DIR}" "${_header}")
            message(FATAL_ERROR
                "Engine public header exposes a forbidden rendering/platform dependency: ${_relative} (${_forbidden})"
            )
        endif()
    endforeach()
endforeach()

# No active parent-repository source may include Vulkan or VMA headers directly.
# Platform code reaches Vulkan only through SDL_vulkan.h and the Render Graph
# public headers.
file(GLOB_RECURSE _all_sources
    LIST_DIRECTORIES false
    "${CGLAB_SOURCE_DIR}/src/*.h"
    "${CGLAB_SOURCE_DIR}/src/*.hpp"
    "${CGLAB_SOURCE_DIR}/src/*.cpp"
)
foreach(_source IN LISTS _all_sources)
    file(READ "${_source}" _contents)
    if(_contents MATCHES "#[ \t]*include[ \t]*[<\"](vulkan/|vulkan\\.h|vk_mem_alloc)")
        file(RELATIVE_PATH _relative "${CGLAB_SOURCE_DIR}" "${_source}")
        message(FATAL_ERROR
            "Direct Vulkan/VMA header include escaped the Render Graph submodule: ${_relative}"
        )
    endif()
endforeach()

if(EXISTS "${CGLAB_SOURCE_DIR}/src/renderer")
    message(FATAL_ERROR "src/renderer must not exist; Render Graph owns the rendering layer")
endif()

if(EXISTS "${CGLAB_SOURCE_DIR}/src/render_graph_vulkan")
    message(FATAL_ERROR "src/render_graph_vulkan must not exist; the reusable backend belongs to the Render Graph submodule")
endif()

file(READ "${CGLAB_SOURCE_DIR}/CMakeLists.txt" _root_cmake)
if(_root_cmake MATCHES "cglab_render_graph_vulkan|src/render_graph_vulkan")
    message(FATAL_ERROR "The removed parent-repository Vulkan backend target is still referenced")
endif()

# Active parent-repository sources may describe Vulkan handles but may not own
# Vulkan side effects. All native allocation, descriptor, pipeline, command and
# submission calls live in the Render Graph Vulkan backend.
file(GLOB_RECURSE _source_files
    LIST_DIRECTORIES false
    "${CGLAB_SOURCE_DIR}/src/*.h"
    "${CGLAB_SOURCE_DIR}/src/*.hpp"
    "${CGLAB_SOURCE_DIR}/src/*.cpp"
)

set(_allowed_native_vulkan_prefixes)

set(_native_vulkan_pattern
    "(vk|vma)(Create|Allocate|Destroy|Free|Bind|Map|Unmap|Flush|Invalidate|UpdateDescriptor|Cmd|Queue|DeviceWaitIdle|AcquireNextImage)"
)

foreach(_source IN LISTS _source_files)
    file(RELATIVE_PATH _relative "${CGLAB_SOURCE_DIR}" "${_source}")
    string(REPLACE "\\" "/" _relative "${_relative}")
    file(READ "${_source}" _contents)
    if(_contents MATCHES "SDL_Vulkan_CreateSurface" AND
       NOT _relative STREQUAL "src/platform/vulkan/sdl_vulkan_surface_adapter.cpp")
        message(FATAL_ERROR "SDL Vulkan surface creation escaped the platform adapter: ${_relative}")
    endif()
    if(_contents MATCHES "vmaCreate(Buffer|Image)")
        message(FATAL_ERROR
            "Persistent GPU buffer/image allocation must be owned by the Render Graph Vulkan resource store: ${_relative}"
        )
    endif()
    if(_contents MATCHES "vk(CreateDescriptor(SetLayout|Pool)|AllocateDescriptorSets|UpdateDescriptorSets|Create(GraphicsPipelines|ShaderModule|PipelineLayout))")
        message(FATAL_ERROR
            "Descriptor and pipeline creation must be owned by the Render Graph Vulkan runtime: ${_relative}"
        )
    endif()
    set(_allowed false)
    foreach(_prefix IN LISTS _allowed_native_vulkan_prefixes)
        string(FIND "${_relative}" "${_prefix}" _prefix_position)
        if(_prefix_position EQUAL 0)
            set(_allowed true)
            break()
        endif()
    endforeach()
    if(_allowed)
        continue()
    endif()

    if(_contents MATCHES "${_native_vulkan_pattern}")
        message(FATAL_ERROR "Native Vulkan side effect escaped an allowed implementation directory: ${_relative}")
    endif()
endforeach()

# --- DoD style contract (D0, 2026-08-14) ---
# The engine/scene layers must follow the DoD style the render-graph core
# already enforces: no std::vector<bool> (bit-packed proxy container, an
# anti-pattern for SoA column models) and no nested std::vector<std::vector
# (pointer chasing). Violations fail the build instead of living in comments.
file(GLOB_RECURSE _dod_sources
    LIST_DIRECTORIES false
    "${CGLAB_SOURCE_DIR}/src/engine/*.h"
    "${CGLAB_SOURCE_DIR}/src/engine/*.cpp"
    "${CGLAB_SOURCE_DIR}/src/scene/*.h"
    "${CGLAB_SOURCE_DIR}/src/scene/*.cpp"
)
foreach(_source IN LISTS _dod_sources)
    file(RELATIVE_PATH _relative "${CGLAB_SOURCE_DIR}" "${_source}")
    string(REPLACE "\\" "/" _relative "${_relative}")
    file(READ "${_source}" _contents)
    if(_contents MATCHES "std::vector[ \t]*<[ \t]*bool")
        message(FATAL_ERROR
            "DoD contract violated: std::vector<bool> in ${_relative} (use a uint8_t column)")
    endif()
    if(_contents MATCHES "std::vector[ \t]*<[ \t]*std::vector")
        message(FATAL_ERROR
            "DoD contract violated: nested std::vector<std::vector in ${_relative}")
    endif()
endforeach()
