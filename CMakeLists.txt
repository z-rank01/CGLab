cmake_minimum_required(VERSION 3.20)

# Use the repository's vcpkg toolchain unless the caller selected another one.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_cglab_default_vcpkg_toolchain
        "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    )
    if(NOT EXISTS "${_cglab_default_vcpkg_toolchain}")
        message(FATAL_ERROR
            "CGLab dependencies are not initialized. Run "
            "scripts/cglab.ps1 setup on Windows or "
            "scripts/cglab.sh setup on Linux before configuring."
        )
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${_cglab_default_vcpkg_toolchain}"
        CACHE STRING "Vcpkg toolchain file"
    )
endif()
set(VCPKG_MANIFEST_MODE ON)

project(CGLab LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Build options
# -----------------------------------------------------------------------------

option(CGLAB_BUILD_DCL_SAMPLES "Build digital-content-loader sample executables" OFF)
option(CGLAB_BUILD_DCL_TESTS "Build digital-content-loader unit tests" ${BUILD_TESTING})
option(CGLAB_BUILD_RENDER_GRAPH_SAMPLES "Build render-graph sample executables" OFF)
option(CGLAB_BUILD_RENDER_GRAPH_UNIT_TESTS "Build render-graph CTest executable" ${BUILD_TESTING})
option(CGLAB_RENDER_GRAPH_SHARED "Build render_graph as a shared library (DLL)" OFF)

set(DCL_BUILD_SAMPLES ${CGLAB_BUILD_DCL_SAMPLES}
    CACHE BOOL "Build sample/test executables (digital-content-loader)" FORCE
)
set(DCL_BUILD_TESTS ${CGLAB_BUILD_DCL_TESTS}
    CACHE BOOL "Build digital-content-loader unit tests" FORCE
)
set(RENDER_GRAPH_BUILD_SAMPLES ${CGLAB_BUILD_RENDER_GRAPH_SAMPLES}
    CACHE BOOL "Build render-graph sample executables" FORCE
)
set(RENDER_GRAPH_BUILD_UNIT_TESTS ${CGLAB_BUILD_RENDER_GRAPH_UNIT_TESTS}
    CACHE BOOL "Build render-graph CTest executable" FORCE
)
set(RENDER_GRAPH_BUILD_SHARED ${CGLAB_RENDER_GRAPH_SHARED}
    CACHE BOOL "Build render_graph as a shared library (DLL)" FORCE
)

# -----------------------------------------------------------------------------
# External packages
# -----------------------------------------------------------------------------

find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)

# digital-content-loader provides the format-agnostic asset row-table model
# (dcl::core) that engine public headers alias, so it must be added before
# src/engine. Third-party sample executables are excluded from the default ALL
# target; test executables must join the default build so CTest can run them.
if(CGLAB_BUILD_DCL_SAMPLES OR CGLAB_BUILD_DCL_TESTS)
    add_subdirectory(third_party/digital-content-loader)
else()
    add_subdirectory(third_party/digital-content-loader EXCLUDE_FROM_ALL)
endif()

# -----------------------------------------------------------------------------
# Foundation libraries
# -----------------------------------------------------------------------------

add_subdirectory(src/utility)
add_subdirectory(src/engine)
add_subdirectory(src/_interface)
add_subdirectory(src/control_plane)

add_subdirectory(src/asset)

if(CGLAB_BUILD_RENDER_GRAPH_SAMPLES OR CGLAB_BUILD_RENDER_GRAPH_UNIT_TESTS)
    add_subdirectory(third_party/render-graph)
else()
    add_subdirectory(third_party/render-graph EXCLUDE_FROM_ALL)
endif()

add_subdirectory(src/platform/vulkan)

# -----------------------------------------------------------------------------
# CGLab runtime, applications, and tests
# -----------------------------------------------------------------------------

add_subdirectory(src/apps)

if(BUILD_TESTING)
    add_subdirectory(src/unit_test)
endif()

# -----------------------------------------------------------------------------
# Configure summary
# -----------------------------------------------------------------------------

message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ compiler ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build CGLab tests: ${BUILD_TESTING}")
