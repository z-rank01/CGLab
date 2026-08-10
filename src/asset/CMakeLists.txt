add_library(cglab_asset_gltf STATIC
    gltf_adapter.cpp
    gltf_adapter.h
)

target_include_directories(cglab_asset_gltf PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
)

target_link_libraries(cglab_asset_gltf
    PUBLIC cglab_engine_core
    PRIVATE dcl::gltf
)

add_library(cglab_asset_runtime STATIC
    asset_service.cpp
    asset_service.h
    geometry_loader.cpp
    geometry_loader.h
)

target_include_directories(cglab_asset_runtime PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
)

target_link_libraries(cglab_asset_runtime
    PUBLIC cglab_asset_gltf
)
