#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <asset_database.h>

// The CPU asset row-table model is owned by digital-content-loader (dcl::core)
// so every asset loader speaks one format-agnostic language. Engine re-exports
// the model under namespace engine to keep existing call sites unchanged.
namespace engine
{
    using geometry_handle = std::uint32_t;
    inline constexpr geometry_handle invalid_geometry_handle = std::numeric_limits<geometry_handle>::max();

    using vertex = dcl::vertex;

    struct draw_range
    {
        std::uint32_t first_index = 0;
        std::uint32_t index_count = 0;
        std::int32_t vertex_offset = 0;
        std::uint32_t material_index = 0;
    };

    inline constexpr std::uint32_t invalid_asset_index = dcl::invalid_asset_index;

    using alpha_mode = dcl::alpha_mode;
    using sampler_filter = dcl::sampler_filter;
    using sampler_wrap = dcl::sampler_wrap;
    using texture_ref = dcl::texture_ref;
    using material_row = dcl::material_row;
    using image_row = dcl::image_row;
    using sampler_row = dcl::sampler_row;
    using primitive_row = dcl::primitive_row;
    using mesh_row = dcl::mesh_row;
    using node_row = dcl::node_row;
    using asset_database = dcl::asset_database;
} // namespace engine
