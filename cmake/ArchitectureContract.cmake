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

# During the strangler migration, native Vulkan side effects are allowed only in
# the current Vulkan implementation, the protected legacy snapshot, and the RG
# Vulkan backend. W8 narrows this list to the RG backend alone.
file(GLOB_RECURSE _source_files
    LIST_DIRECTORIES false
    "${CGLAB_SOURCE_DIR}/src/*.h"
    "${CGLAB_SOURCE_DIR}/src/*.hpp"
    "${CGLAB_SOURCE_DIR}/src/*.cpp"
)

set(_allowed_native_vulkan_prefixes
    "src/_old/"
    "src/_templates/"
    "src/_vra/"
    "src/legacy_vulkan_sample/"
    "src/renderer/vulkan/"
)

set(_native_vulkan_pattern
    "(vk|vma)(Create|Allocate|Destroy|Free|Bind|Map|Unmap|Flush|Invalidate|UpdateDescriptor|CmdBindPipeline|CmdBindDescriptor)"
)

foreach(_source IN LISTS _source_files)
    file(RELATIVE_PATH _relative "${CGLAB_SOURCE_DIR}" "${_source}")
    string(REPLACE "\\" "/" _relative "${_relative}")
    file(READ "${_source}" _contents)
    if(_contents MATCHES "vmaCreate(Buffer|Image)" AND
       NOT _relative MATCHES "^src/(legacy_vulkan_sample|_old|_vra)/")
        message(FATAL_ERROR
            "Persistent GPU buffer/image allocation must be owned by the Render Graph Vulkan resource store: ${_relative}"
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
