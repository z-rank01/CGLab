#pragma once

#include <cstdint>
#include <vector>

#include <gltf/gltf_data.h>

struct smoke_scene_data
{
    std::vector<gltf::PerDrawCallData> draw_calls;
    std::vector<gltf::PerMeshData> meshes;
    std::vector<std::uint32_t> indices;
    std::vector<gltf::Vertex> vertices;
};

[[nodiscard]] smoke_scene_data make_smoke_scene();
