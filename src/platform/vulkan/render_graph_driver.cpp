#include "platform/vulkan/render_graph_driver.h"

#include <memory>
#include <iostream>

#include "_interface/window.h"
#include "platform/vulkan/sdl_vulkan_surface_adapter.h"
#include "render_graph/backend/vulkan/device.h"

namespace platform::vulkan
{
    namespace
    {
        struct driver_state
        {
            render_recipe recipe;
            render_graph::render_device device;
            const engine::render_frame_packet* packet = nullptr;
            uint64_t steady_descriptor_baseline = 0;
            bool render_started = false;
            bool shutdown = false;
        };

        driver_state& driver(void* value) { return *static_cast<driver_state*>(value); }
        const driver_state& driver(const void* value) { return *static_cast<const driver_state*>(value); }

        render_graph::frame_build_result build_recipe(void* value,
                                                       const render_graph::frame_environment& environment,
                                                       render_graph::frame_plan& plan)
        {
            auto& state = *static_cast<driver_state*>(value);
            return state.recipe.api->build_frame(state.recipe.state, state.device, *state.packet, environment, plan);
        }

        const engine::render_driver_api driver_api{
            .initialize = [](void* value, interface::window& window, const engine::backend_config& config)
            {
                auto& state = driver(value);
                auto created = render_graph::vulkan::create_device({
                    .application_name = config.application_name,
                    .frames_in_flight = config.frames_in_flight,
                    .validation = config.validation,
                    .surface = make_sdl_surface_provider(window),
                });
                if (!created) return engine::result<bool>{.error = created.error};
                state.device = std::move(created.device);
                auto result = state.recipe.api->initialize(state.recipe.state, state.device, config);
                return result;
            },
            .apply_resource_changes = [](void* value, engine::resource_change_batch batch)
            {
                auto& state = driver(value);
                return state.recipe.api->apply_resource_changes(state.recipe.state, state.device, batch);
            },
            .render = [](void* value, const engine::render_frame_packet& packet)
            {
                auto& state = driver(value);
                if (!state.render_started)
                {
                    state.steady_descriptor_baseline = state.device.statistics().descriptor_updates;
                    state.render_started = true;
                }
                state.packet = &packet;
                const auto result = state.device.render({.state = &state, .build = &build_recipe});
                state.packet = nullptr;
                if (result.status == render_graph::frame_status::rendered) return engine::frame_status::rendered;
                if (result.status == render_graph::frame_status::skipped) return engine::frame_status::skipped;
                if (!result.error.empty()) std::cerr << "[RenderGraph] " << result.error << '\n';
                return engine::frame_status::failed;
            },
            .request_resize = [](void* value) noexcept { driver(value).device.request_resize(); },
            .shutdown = [](void* value) noexcept
            {
                auto& state = driver(value);
                if (state.shutdown) return;
                state.shutdown = true;
                state.recipe.api->shutdown(state.recipe.state, state.device);
                state.device.shutdown();
            },
            .statistics = [](const void* value) noexcept
            {
                const auto& state = driver(value);
                const auto stats = state.device.statistics();
                return engine::render_statistics{
                    .upload_pass_executions = stats.upload_pass_executions,
                    .draw_pass_executions = stats.draw_pass_executions,
                    .presented_frames = stats.presented_frames,
                    .steady_frame_descriptor_updates = stats.descriptor_updates -
                                                       state.steady_descriptor_baseline,
                    .pipeline_creations = stats.pipeline_creations,
                    .indirect_groups = stats.indirect_groups,
                };
            },
            .validation_error_count = [](const void* value) noexcept
            { return driver(value).device.validation_error_count(); },
            .destroy = [](void* value) noexcept
            {
                auto* state = static_cast<driver_state*>(value);
                if (state->recipe.api && state->recipe.api->destroy)
                    state->recipe.api->destroy(state->recipe.state);
                delete state;
            },
        };
    } // namespace

    engine::render_driver create_render_graph_driver(render_recipe recipe)
    {
        return {new driver_state{.recipe = recipe}, &driver_api};
    }
} // namespace platform::vulkan
