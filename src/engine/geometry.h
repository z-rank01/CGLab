#pragma once

#include <cstdint>
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
    };

    struct geometry_asset
    {
        std::string name;
        std::vector<geometry_primitive> primitives;
        glm::vec3 bounds_min{};
        glm::vec3 bounds_max{};

        [[nodiscard]] bool empty() const noexcept { return primitives.empty(); }
    };
} // namespace engine
