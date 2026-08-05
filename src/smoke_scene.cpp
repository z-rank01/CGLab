#include "smoke_scene.h"

#include <utility>

#include <glm/glm.hpp>

smoke_scene_data make_smoke_scene()
{
    smoke_scene_data scene;
    scene.vertices = {
        gltf::Vertex{
            .position = {-3.0F, -2.0F, 0.0F},
            .color = {1.0F, 0.0F, 0.0F, 1.0F},
            .normal = {0.0F, 0.0F, 1.0F},
            .tangent = {1.0F, 0.0F, 0.0F, 1.0F},
            .uv0 = {0.0F, 0.0F},
            .uv1 = {0.0F, 0.0F},
        },
        gltf::Vertex{
            .position = {3.0F, -2.0F, 0.0F},
            .color = {0.0F, 1.0F, 0.0F, 1.0F},
            .normal = {0.0F, 0.0F, 1.0F},
            .tangent = {1.0F, 0.0F, 0.0F, 1.0F},
            .uv0 = {1.0F, 0.0F},
            .uv1 = {1.0F, 0.0F},
        },
        gltf::Vertex{
            .position = {0.0F, 3.0F, 0.0F},
            .color = {0.0F, 0.0F, 1.0F, 1.0F},
            .normal = {0.0F, 0.0F, 1.0F},
            .tangent = {1.0F, 0.0F, 0.0F, 1.0F},
            .uv0 = {0.5F, 1.0F},
            .uv1 = {0.5F, 1.0F},
        },
    };
    scene.indices = {0, 1, 2};

    gltf::PerDrawCallData draw{
        .transform = glm::mat4(1.0F),
        .indices = scene.indices,
        .vertices = scene.vertices,
        .material_index = 0,
        .first_index = 0,
        .index_count = static_cast<std::uint32_t>(scene.indices.size()),
        .first_vertex = 0,
        .vertex_count = static_cast<std::uint32_t>(scene.vertices.size()),
    };
    scene.draw_calls.push_back(draw);
    scene.meshes.push_back(gltf::PerMeshData{
        .name = "SmokeTriangle",
        .primitives = {std::move(draw)},
    });
    return scene;
}
