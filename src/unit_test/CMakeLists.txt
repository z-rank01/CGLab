find_package(nlohmann_json CONFIG REQUIRED)

function(cglab_register_test target_name test_name)
    target_include_directories(${target_name}
        PRIVATE ${PROJECT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /FS /utf-8)
    endif()

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    add_test(NAME ${test_name} COMMAND ${target_name})
endfunction()

add_executable(cglab_application_options_tests
    ${PROJECT_SOURCE_DIR}/src/apps/application_options.cpp
    ${PROJECT_SOURCE_DIR}/src/apps/application_options.h
    application_options_test.cpp
)
cglab_register_test(cglab_application_options_tests cglab.application_options)

add_executable(cglab_swapchain_image_state_tests
    ${PROJECT_SOURCE_DIR}/src/swapchain_image_state.h
    swapchain_image_state_test.cpp
)
cglab_register_test(cglab_swapchain_image_state_tests cglab.swapchain_image_state)

add_executable(cglab_camera_input_tests camera_input_test.cpp)
target_link_libraries(cglab_camera_input_tests PRIVATE cglab_platform_sdl)
cglab_register_test(cglab_camera_input_tests cglab.camera_input)

add_executable(cglab_camera_rig_tests camera_rig_test.cpp)
target_link_libraries(cglab_camera_rig_tests PRIVATE cglab_engine_core)
cglab_register_test(cglab_camera_rig_tests cglab.camera_rig)

add_executable(cglab_scene_registry_tests scene_registry_test.cpp)
target_link_libraries(cglab_scene_registry_tests PRIVATE glm::glm)
cglab_register_test(cglab_scene_registry_tests cglab.scene_registry)

add_executable(cglab_control_plane_protocol_tests
    ${PROJECT_SOURCE_DIR}/src/control_plane/json_rpc.cpp
    control_plane_protocol_test.cpp
)
target_include_directories(cglab_control_plane_protocol_tests
    PRIVATE ${PROJECT_SOURCE_DIR}/src/control_plane
)
target_link_libraries(cglab_control_plane_protocol_tests PRIVATE nlohmann_json::nlohmann_json)
cglab_register_test(cglab_control_plane_protocol_tests cglab.control_plane_protocol)

add_executable(cglab_engine_runtime_tests engine_runtime_test.cpp)
target_link_libraries(cglab_engine_runtime_tests PRIVATE cglab_engine_runtime)
cglab_register_test(cglab_engine_runtime_tests cglab.engine_runtime)

add_executable(cglab_public_headers_tests public_headers_test.cpp)
target_link_libraries(cglab_public_headers_tests PRIVATE cglab_engine_runtime)
cglab_register_test(cglab_public_headers_tests cglab.public_headers)

add_executable(cglab_asset_gltf_tests asset_gltf_test.cpp)
target_link_libraries(cglab_asset_gltf_tests PRIVATE cglab_asset_runtime dcl::gltf)
target_compile_definitions(cglab_asset_gltf_tests
    PRIVATE CGLAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
cglab_register_test(cglab_asset_gltf_tests cglab.asset_gltf)

add_executable(cglab_asset_service_tests asset_service_test.cpp)
target_link_libraries(cglab_asset_service_tests PRIVATE cglab_asset_runtime)
target_compile_definitions(cglab_asset_service_tests
    PRIVATE CGLAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
cglab_register_test(cglab_asset_service_tests cglab.asset_service)
