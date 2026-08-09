#include "asset/gltf_adapter.h"

#include <limits>

#include <gltf/gltf_loader.h>
#include <gltf/gltf_parser.h>

namespace asset
{
    engine::result<engine::geometry_asset> load_gltf(const std::filesystem::path& path)
    {
        engine::result<engine::geometry_asset> result;
        try
        {
            if (!std::filesystem::is_regular_file(path))
            {
                result.error = "Asset file does not exist: " + path.string();
                return result;
            }

            gltf::GltfLoader loader;
            gltf::GltfParser parser;
            auto model = loader(path.string());
            auto source = parser(model, gltf::RequestDrawCallList{});
            if (source.empty())
            {
                result.error = "Asset contains no drawable primitives: " + path.string();
                return result;
            }

            engine::geometry_asset converted;
            converted.name = path.filename().string();
            converted.bounds_min = glm::vec3(std::numeric_limits<float>::max());
            converted.bounds_max = glm::vec3(std::numeric_limits<float>::lowest());
            converted.primitives.reserve(source.size());
            for (const gltf::PerDrawCallData& primitive : source)
            {
                engine::geometry_primitive destination;
                destination.indices = primitive.indices;
                destination.material_index = primitive.material_index;
                destination.vertices.reserve(primitive.vertices.size());
                for (const gltf::Vertex& vertex : primitive.vertices)
                {
                    engine::vertex output{
                        .position = glm::vec3(primitive.transform * glm::vec4(vertex.position, 1.0F)),
                        .color = vertex.color,
                        .normal = vertex.normal,
                        .tangent = vertex.tangent,
                        .uv0 = vertex.uv0,
                        .uv1 = vertex.uv1,
                    };
                    converted.bounds_min = glm::min(converted.bounds_min, output.position);
                    converted.bounds_max = glm::max(converted.bounds_max, output.position);
                    destination.vertices.push_back(output);
                }
                converted.primitives.push_back(std::move(destination));
            }
            result.value = std::move(converted);
        }
        catch (const std::exception& error)
        {
            result.error = error.what();
        }
        return result;
    }
} // namespace asset
