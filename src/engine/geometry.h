#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine
{
    using geometry_handle = std::uint32_t;
    inline constexpr geometry_handle invalid_geometry_handle = std::numeric_limits<geometry_handle>::max();

    struct vertex
    {
        glm::vec3 position{};
        glm::vec4 color{1.0F};
        glm::vec3 normal{};
        glm::vec4 tangent{};
        glm::vec2 uv0{};
        glm::vec2 uv1{};
    };

    struct geometry_primitive
    {
        std::vector<std::uint32_t> indices;
        std::vector<vertex> vertices;
        std::uint32_t material_index = 0;
    };

    struct draw_range
    {
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::int32_t vertex_offset = 0;
        std::uint32_t material_index = 0;
    };

    struct geometry_asset
    {
        std::string name;
        std::vector<geometry_primitive> primitives;
        glm::vec3 bounds_min{};
        glm::vec3 bounds_max{};

        [[nodiscard]] bool empty() const noexcept { return primitives.empty(); }
    };

    inline constexpr std::uint32_t invalid_asset_index = std::numeric_limits<std::uint32_t>::max();

    enum class alpha_mode : std::uint8_t { opaque, mask, blend };
    enum class sampler_filter : std::uint8_t { nearest, linear };
    enum class sampler_wrap : std::uint8_t { repeat, mirrored_repeat, clamp_to_edge };

    struct texture_ref
    {
        std::uint32_t image = invalid_asset_index;
        std::uint32_t sampler = invalid_asset_index;
        std::uint32_t texcoord = 0;
        float scale = 1.0F;
    };

    struct material_row
    {
        std::string name;
        glm::vec4 base_color_factor{1.0F};
        float metallic_factor = 1.0F;
        float roughness_factor = 1.0F;
        glm::vec3 emissive_factor{0.0F};
        texture_ref base_color_texture;
        texture_ref metallic_roughness_texture;
        texture_ref normal_texture;
        texture_ref occlusion_texture;
        texture_ref emissive_texture;
        alpha_mode alpha = alpha_mode::opaque;
        float alpha_cutoff = 0.5F;
        bool double_sided = false;
    };

    struct image_row
    {
        std::string name;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t components = 4;
        std::vector<std::byte> pixels;
    };

    struct sampler_row
    {
        sampler_filter min_filter = sampler_filter::linear;
        sampler_filter mag_filter = sampler_filter::linear;
        sampler_wrap wrap_u = sampler_wrap::repeat;
        sampler_wrap wrap_v = sampler_wrap::repeat;
    };

    struct primitive_row
    {
        std::uint32_t mesh = invalid_asset_index;
        std::uint32_t material = invalid_asset_index;
        std::uint32_t vertex_offset = 0;
        std::uint32_t vertex_count = 0;
        std::uint32_t index_offset = 0;
        std::uint32_t index_count = 0;
    };

    struct mesh_row
    {
        std::string name;
        std::uint32_t first_primitive = 0;
        std::uint32_t primitive_count = 0;
        glm::vec3 bounds_min{};
        glm::vec3 bounds_max{};
    };

    struct node_row
    {
        std::string name;
        std::uint32_t parent = invalid_asset_index;
        std::uint32_t mesh = invalid_asset_index;
        glm::mat4 local_transform{1.0F};
    };

    struct asset_database
    {
        std::string name;
        std::vector<node_row> nodes;
        std::vector<mesh_row> meshes;
        std::vector<primitive_row> primitives;
        std::vector<material_row> materials;
        std::vector<image_row> images;
        std::vector<sampler_row> samplers;
        std::vector<vertex> vertex_blob;
        std::vector<std::uint32_t> index_blob;
        std::vector<std::string> warnings;
    };
} // namespace engine
