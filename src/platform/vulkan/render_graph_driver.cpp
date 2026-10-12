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
            const render_graph::frame_plan* plan = nullptr;
            uint64_t steady_descriptor_baseline = 0;
            bool render_started = false;
            bool shutdown = false;
            // A3-2：末帧成功提交图的客观规模（缓存，不在 statistics() 调用点重算）
            uint64_t last_frame_raster_passes = 0;
            uint64_t last_frame_draw_groups = 0;
        };

        driver_state& driver(void* value) { return *static_cast<driver_state*>(value); }
        const driver_state& driver(const void* value) { return *static_cast<const driver_state*>(value); }

        render_graph::frame_build_result build_recipe(void* value,
                                                       const render_graph::frame_environment& environment,
                                                       render_graph::frame_plan& plan)
        {
            auto& state = *static_cast<driver_state*>(value);
            state.plan = &plan;
            return state.recipe.api->build_frame(state.recipe.state, state.device, *state.packet, environment, plan);
        }

        // A3-2：从**计划**（recipe 的声明，driver 归约）求末帧成功提交图的客观规模。
        // 分组口径与后端记录口径一致：按各 pass 的 indexed_indirect_draws 切片累加，
        // 只计 draw_count > 0 的行（vulkan_device.cpp 的 indirect_groups 同口径）；
        // 不能拿 plan.indexed_indirect_draws.size() —— 它含不参与分组的命令区行。
        void capture_plan_scale(driver_state& state)
        {
            if (state.plan == nullptr) return;
            const render_graph::frame_plan& plan = *state.plan;
            std::uint64_t raster_passes = 0;
            std::uint64_t draw_groups = 0;
            for (const render_graph::frame_pass_row& pass : plan.passes)
            {
                if (pass.kind != render_graph::pass_kind::raster) continue;
                ++raster_passes;
                const std::size_t begin = pass.indexed_indirect_draws.begin;
                const std::size_t count = pass.indexed_indirect_draws.count;
                if (begin > plan.indexed_indirect_draws.size() ||
                    count > plan.indexed_indirect_draws.size() - begin)
                {
                    continue; // 越界由后端校验负责报错，这里不制造第二处失败点
                }
                for (const render_graph::draw_indexed_indirect_row& draw :
                     plan.indexed_indirect_draws.subspan(begin, count))
                    if (draw.draw_count != 0) ++draw_groups;
            }
            state.last_frame_raster_passes = raster_passes;
            state.last_frame_draw_groups = draw_groups;
            state.plan = nullptr;
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
                    .diagnostics = {nullptr, [](void*, render_graph::diagnostic_severity severity, std::string_view message)
                    {
                        std::cerr << "[RenderGraph][" << (severity == render_graph::diagnostic_severity::error ? "Error"
                                                            : severity == render_graph::diagnostic_severity::warning ? "Warning"
                                                                                                                     : "Info")
                                  << "] " << message << '\n';
                    }},
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
                state.packet = &packet;
                const auto result = state.device.render({.state = &state, .build = &build_recipe});
                state.packet = nullptr;
                if (!state.render_started)
                {
                    // 稳态基线：首帧渲染完成后捕获——首帧的一次性资源建立
                    // （如 M6 半分辨率 RT 的 bindless 发布）计入基线；
                    // 稳态帧（第 2 帧起）必须零 descriptor 更新（bindless 复用）。
                    state.steady_descriptor_baseline = state.device.statistics().descriptor_updates;
                    state.render_started = true;
                }
                if (result.status == render_graph::frame_status::rendered)
                {
                    capture_plan_scale(state);
                    return engine::frame_status::rendered;
                }
                if (result.status == render_graph::frame_status::skipped)
                {
                    state.plan = nullptr; // 未提交：规模不更新（statistics 里保留上一次成功帧）
                    return engine::frame_status::skipped;
                }
                if (!result.error.empty()) std::cerr << "[RenderGraph] " << result.error << '\n';
                state.plan = nullptr;
                return engine::frame_status::failed;
            },
            .graph_debug_dump = [](void* value)
            { return engine::result<std::string>{.value = driver(value).device.debug_dump()}; },
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
                    .graph_compiles = stats.graph_compiles,
                    // A3-2：末帧成功提交图的规模（driver 归约，供 smoke 契约独立交叉校验）
                    .last_frame_raster_passes = state.last_frame_raster_passes,
                    .last_frame_draw_groups = state.last_frame_draw_groups,
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
