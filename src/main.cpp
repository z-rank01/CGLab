#include <vma/vk_mem_alloc.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <gltf/gltf_loader.h>
#include <gltf/gltf_parser.h>

#include "app_sample.h"
#include "application_options.h"
#include "utility/config_reader.h"
#include "utility/logger.h"

namespace
{
    bool load_general_config(const application_options& options, general_config& config)
    {
        config.app_name = "VulkanSample";
        if (!options.config_path)
        {
            return true;
        }
        if (!std::filesystem::is_regular_file(*options.config_path))
        {
            Logger::LogError("Config file does not exist: " + options.config_path->string());
            return false;
        }

        config_reader reader(options.config_path->string());
        return reader.try_parse_general_config(config);
    }
}

int main(int argc, char** argv)
{
    const std::span arguments(argv + 1, static_cast<std::size_t>(argc - 1));
    std::vector<std::string_view> argument_views;
    argument_views.reserve(arguments.size());
    for (const char* argument : arguments)
    {
        argument_views.emplace_back(argument);
    }

    const auto options_result = parse_application_options(argument_views);
    if (options_result.status == application_options_status::help)
    {
        std::cout << application_usage(argc > 0 ? argv[0] : "VulkanSample");
        return EXIT_SUCCESS;
    }
    if (!options_result.succeeded())
    {
        std::cerr << options_result.message << '\n'
                  << application_usage(argc > 0 ? argv[0] : "VulkanSample");
        return EXIT_FAILURE;
    }

    const application_options& options = options_result.options;
    if (options.smoke_test)
    {
        Logger::LogError("--smoke-test is reserved for the Step 15 GPU smoke path and is not available yet");
        return EXIT_FAILURE;
    }

    try
    {
        general_config general;
        if (!load_general_config(options, general))
        {
            return EXIT_FAILURE;
        }

        const std::filesystem::path source_directory = std::filesystem::path(CGLAB_SOURCE_DIR);
        general.working_directory = source_directory.string();
        const std::filesystem::path asset_path = resolve_asset_path(options, general.asset_directory);
        if (asset_path.empty() || !std::filesystem::is_regular_file(asset_path))
        {
            Logger::LogError("A valid glTF asset is required; pass --asset <path> or provide asset_directory in --config");
            return EXIT_FAILURE;
        }
        general.asset_directory = asset_path.string();

        gltf::GltfLoader loader;
        auto asset = loader(general.asset_directory);

        gltf::GltfParser parser;
        auto mesh_list = parser(asset, gltf::RequestMeshList{});
        auto draw_call_data_list = parser(asset, gltf::RequestDrawCallList{});
        std::ranges::for_each(draw_call_data_list,
                              [](gltf::PerDrawCallData& primitive)
                              {
                                  const glm::mat4& transform = primitive.transform;
                                  std::ranges::for_each(primitive.vertices,
                                                        [&](gltf::Vertex& vertex)
                                                        {
                                                            const glm::vec4 transformed = transform * glm::vec4(vertex.position, 1.0F);
                                                            vertex.position = glm::vec3(transformed);
                                                        });
                              });

        const std::size_t index_capacity = std::accumulate(
            draw_call_data_list.begin(), draw_call_data_list.end(), std::size_t{0},
            [](std::size_t sum, const gltf::PerDrawCallData& draw) { return sum + draw.indices.size(); });
        const std::size_t vertex_capacity = std::accumulate(
            draw_call_data_list.begin(), draw_call_data_list.end(), std::size_t{0},
            [](std::size_t sum, const gltf::PerDrawCallData& draw) { return sum + draw.vertices.size(); });

        std::vector<std::uint32_t> indices;
        std::vector<gltf::Vertex> vertices;
        indices.reserve(index_capacity);
        vertices.reserve(vertex_capacity);
        for (const auto& draw_call_data : draw_call_data_list)
        {
            indices.insert(indices.end(), draw_call_data.indices.begin(), draw_call_data.indices.end());
            vertices.insert(vertices.end(), draw_call_data.vertices.begin(), draw_call_data.vertices.end());
        }

        window_config window{.width = 1280, .height = 720, .title = general.app_name};
        engine_config engine{
            .window_config = std::move(window),
            .general_config = std::move(general),
            .frame_count = 3,
            .use_validation_layers = options.validation,
        };

        app_sample sample(std::move(engine));
        sample.set_vertex_index_data(std::move(draw_call_data_list), std::move(indices), std::move(vertices));
        sample.set_mesh_list(mesh_list);
        sample.initialize();
        if (!sample.tick(options.frame_limit))
        {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        Logger::LogError(std::string("VulkanSample failed: ") + error.what());
        return EXIT_FAILURE;
    }
}
