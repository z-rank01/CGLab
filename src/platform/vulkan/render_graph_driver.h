#pragma once

#include "engine/render_backend.h"
#include "render_graph/render_device.h"

namespace platform::vulkan
{
    struct render_recipe_api
    {
        engine::result<bool> (*initialize)(void*, render_graph::render_device&, const engine::backend_config&);
        engine::result<engine::resource_change_result> (*apply_resource_changes)(
            void*, render_graph::render_device&, engine::resource_change_batch);
        render_graph::frame_build_result (*build_frame)(
            void*, render_graph::render_device&, const engine::render_frame_packet&,
            const render_graph::frame_environment&, render_graph::frame_plan&);
        void (*shutdown)(void*, render_graph::render_device&) noexcept;
        void (*destroy)(void*) noexcept;
    };

    struct render_recipe
    {
        void* state = nullptr;
        const render_recipe_api* api = nullptr;
    };

    [[nodiscard]] engine::render_driver create_render_graph_driver(render_recipe);
} // namespace platform::vulkan
