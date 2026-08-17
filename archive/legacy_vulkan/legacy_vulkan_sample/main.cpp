#include <vma/vk_mem_alloc.h>

#include <algorithm>
#include <iostream>

#include <gltf/gltf_loader.h>
#include <gltf/gltf_parser.h>
#include "app_sample.h"
#include "utility/config_reader.h"
#include "utility/logger.h"

#if defined(CGLAB_ENABLE_RENDER_GRAPH_UNIT_TESTS)
// #include "render_graph/unit_test/deferred_rendering_compile_test.h"     // unit test
// #include "render_graph/unit_test/resource_producer_map_compile_test.h"  // unit test
// #include "render_graph/unit_test/resource_generation_compile_test.h"   // unit test
// #include "render_graph/unit_test/culling_compile_test.h"   // unit test
// #include "render_graph/unit_test/validation_compile_test.h"   // unit test
// #include "render_graph/unit_test/dag_compile_test.h"   // unit test
// #include "render_graph/unit_test/dag_cycle_compile_test.h"   // unit test
// #include "render_graph/unit_test/lifetime_aliasing_test.h"   // unit test
// #include "render_graph/unit_test/barrier_plan_test.h"   // unit test
#endif

int main()
{
#if defined(CGLAB_ENABLE_RENDER_GRAPH_UNIT_TESTS)
    // render_graph::unit_test::deferred_rendering_compile_test();
    // render_graph::unit_test::resource_producer_map_compile_test();
    // render_graph::unit_test::resource_generation_compile_test();
    // render_graph::unit_test::validation_compile_test();
    // render_graph::unit_test::culling_compile_test();
    // render_graph::unit_test::dag_compile_test();
    // render_graph::unit_test::dag_cycle_compile_test();
    // render_graph::unit_test::lifetime_aliasing_test();
    // render_graph::unit_test::barrier_plan_test();
#endif
    
    // std::cout << "Hello, World!" << '\n';
    // std::cout << "This is a Vulkan Sample" << '\n';

    // // general config

    // config_reader config_reader(R"(D:\Repository\CGLab\config\win64\app_config.json)");
    // general_config general_config;
    // if (!config_reader.try_parse_general_config(general_config))
    // {
    //     Logger::LogError("Failed to parse general config");
    //     return -1;
    // }

    // // read gltf file

    // auto loader = gltf::GltfLoader();
    // auto asset  = loader(general_config.asset_directory);

    // // parse gltf file

    // gltf::GltfParser parser;
    // auto mesh_list           = parser(asset, gltf::RequestMeshList{});
    // auto draw_call_data_list = parser(asset, gltf::RequestDrawCallList{});
    // // Transform vertex positions using the draw call's transform matrix (functional expression)
    // std::ranges::for_each(draw_call_data_list,
    //                       [](gltf::PerDrawCallData& primitive)
    //                       {
    //                           const glm::mat4& transform = primitive.transform;
    //                           std::ranges::for_each(primitive.vertices,
    //                                                 [&](gltf::Vertex& vertex)
    //                                                 {
    //                                                     glm::vec4 transformed_position = transform * glm::vec4(vertex.position, 1.0F);
    //                                                     vertex.position                = glm::vec3(transformed_position);
    //                                                 });
    //                       });

    // // collect all indices

    // std::vector<uint32_t> indices;
    // std::vector<gltf::Vertex> vertices;

    // // reserve memory

    // auto index_capacity  = std::accumulate(draw_call_data_list.begin(),
    //                                       draw_call_data_list.end(),
    //                                       0,
    //                                       [&](const auto& sum, const auto& draw_call_data) { return sum + draw_call_data.indices.size(); });
    // auto vertex_capacity = std::accumulate(draw_call_data_list.begin(),
    //                                        draw_call_data_list.end(),
    //                                        0,
    //                                        [&](const auto& sum, const auto& draw_call_data) { return sum + draw_call_data.vertices.size(); });
    // indices.reserve(index_capacity);
    // vertices.reserve(vertex_capacity);

    // // collect all indices and vertices

    // for (const auto& draw_call_data : draw_call_data_list)
    // {
    //     indices.insert(indices.end(), draw_call_data.indices.begin(), draw_call_data.indices.end());
    //     vertices.insert(vertices.end(), draw_call_data.vertices.begin(), draw_call_data.vertices.end());
    // }

    // // configs

    // window_config window_config = {.width = 1280, .height = 720, .title = "Vulkan Engine"};
    // engine_config config        = {.window_config = window_config, .general_config = general_config, .frame_count = 3, .use_validation_layers = true};

    // // main loop

    // app_sample sample(config);
    // sample.set_vertex_index_data(draw_call_data_list, indices, vertices);
    // sample.set_mesh_list(mesh_list);
    // sample.initialize();
    // sample.tick();

    // std::cout << "Goodbye" << '\n';

    // return 0;
}
