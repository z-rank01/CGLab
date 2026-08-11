#include "renderer/vulkan/vulkan_backend_internal.h"

namespace
{
    constexpr uint64_t upload_arena_capacity = 64ull * 1024ull * 1024ull;
    constexpr uint64_t geometry_arena_capacity = 128ull * 1024ull * 1024ull;
}

bool vulkan_backend::build_render_graph(uint32_t image_index)
{
    using setup_context = frame_render_graph::pass_setup_context;
    using execute_context = frame_render_graph::pass_execute_context;

    const auto extent = comm_vk_swapchain_context.swapchain_info_.extent_;
    const auto native_format = static_cast<VkFormat>(comm_vk_swapchain_context.swapchain_info_.surface_format_.format);
    const bool swapchain_initialized = swapchain_image_states.is_initialized(image_index);
    const uint64_t graph_cache_key = (static_cast<uint64_t>(extent.width) << 32) ^
                                     static_cast<uint64_t>(extent.height) ^
                                     (static_cast<uint64_t>(native_format) << 2) ^
                                     static_cast<uint64_t>(swapchain_initialized);
    frame_graph->begin_frame(submitted_frame, completed_frame, graph_cache_key);
    if (!frame_graph->needs_recompile()) return true;
    frame_graph->clear();

    const render_graph::buffer_desc upload_desc{
        .size = upload_arena_capacity,
        .usage = render_graph::buffer_usage::TRANSFER_SRC,
        .memory = render_graph::memory_domain::upload,
        .mapping = render_graph::mapping_policy::persistent,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::imported,
    };
    const render_graph::buffer_desc geometry_desc{
        .size = geometry_arena_capacity,
        .usage = render_graph::buffer_usage::TRANSFER_DST |
                 render_graph::buffer_usage::VERTEX_BUFFER |
                 render_graph::buffer_usage::INDEX_BUFFER,
        .memory = render_graph::memory_domain::device_local,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::imported,
    };
    const render_graph::buffer_desc uniform_desc{
        .size = uniform_stride * config.frame_count,
        .usage = render_graph::buffer_usage::UNIFORM_BUFFER,
        .memory = render_graph::memory_domain::upload,
        .mapping = render_graph::mapping_policy::persistent,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::imported,
    };

    frame_graph->add_copy_pass("UploadPass", [this, upload_desc, geometry_desc](setup_context& ctx)
    {
        rg_upload = ctx.import_buffer("UploadArena", upload_desc);
        rg_geometry = ctx.import_buffer("GeometryArena", geometry_desc);
        const render_graph::buffer_access_desc transfer_src{
            .usage = render_graph::buffer_usage::TRANSFER_SRC,
            .domain = render_graph::pipeline_domain::copy,
        };
        ctx.set_initial_state(rg_upload, transfer_src, render_graph::access_type::read,
                              render_graph::contents_policy::preserve);
        ctx.read_buffer(rg_upload, transfer_src);
        ctx.set_initial_state(rg_geometry,
                              render_graph::buffer_access_desc{
                                  .usage = render_graph::buffer_usage::TRANSFER_DST,
                                  .domain = render_graph::pipeline_domain::copy,
                              },
                              render_graph::access_type::write,
                              render_graph::contents_policy::preserve);
        ctx.write_buffer(rg_geometry,
                         render_graph::buffer_access_desc{
                             .usage = render_graph::buffer_usage::TRANSFER_DST,
                             .domain = render_graph::pipeline_domain::copy,
                         });
    }, [this](execute_context& ctx)
    {
        if (runtime->has_pending_uploads())
        {
            ++run_statistics.upload_pass_executions;
            if (!runtime->record_pending_uploads(ctx.commands()))
                throw std::runtime_error(runtime->last_error());
        }
    });

    const render_graph::image_desc swapchain_desc{
        .fmt = render_graph::normalize_vk_format(native_format),
        .extent = {extent.width, extent.height, 1},
        .usage = render_graph::image_usage::COLOR_ATTACHMENT | render_graph::image_usage::PRESENT,
        .memory = render_graph::memory_domain::device_local,
        .aliasing = render_graph::aliasing_policy::forbidden,
        .lifetime = render_graph::resource_lifetime_class::imported,
    };
    const render_graph::image_desc depth_desc{
        .fmt = render_graph::format::D32_SFLOAT,
        .extent = {extent.width, extent.height, 1},
        .usage = render_graph::image_usage::DEPTH_STENCIL_ATTACHMENT,
        .memory = render_graph::memory_domain::device_local,
        .lifetime = render_graph::resource_lifetime_class::transient,
    };

    frame_graph->add_raster_pass(render_program.pass_name,
                                 [this, uniform_desc, swapchain_desc, depth_desc, extent, swapchain_initialized](setup_context& ctx)
    {
        const render_graph::buffer_access_desc mesh_read{
            .usage = render_graph::buffer_usage::VERTEX_BUFFER | render_graph::buffer_usage::INDEX_BUFFER,
            .domain = render_graph::pipeline_domain::graphics,
        };
        ctx.read_buffer(rg_geometry, mesh_read);

        rg_uniform = ctx.import_buffer("FrameUniform", uniform_desc);
        const render_graph::buffer_access_desc uniform_read{
            .usage = render_graph::buffer_usage::UNIFORM_BUFFER,
            .domain = render_graph::pipeline_domain::graphics,
        };
        ctx.set_initial_state(rg_uniform, uniform_read, render_graph::access_type::read,
                              render_graph::contents_policy::preserve);
        ctx.read_buffer(rg_uniform, uniform_read);

        rg_swapchain = ctx.import_image("Swapchain", swapchain_desc);
        const render_graph::image_access_desc present{
            .usage = render_graph::image_usage::PRESENT,
            .domain = render_graph::pipeline_domain::graphics,
        };
        ctx.set_initial_state(rg_swapchain,
                              swapchain_initialized
                                  ? present
                                  : render_graph::image_access_desc{
                                        .usage = render_graph::image_usage::NONE,
                                        .domain = render_graph::pipeline_domain::graphics,
                                    },
                              render_graph::access_type::read,
                              swapchain_initialized ? render_graph::contents_policy::preserve
                                                    : render_graph::contents_policy::discard);
        ctx.set_final_state(rg_swapchain, present, render_graph::access_type::read);

        rg_depth = ctx.create_image("Depth", depth_desc);
        ctx.set_render_area({.width = extent.width, .height = extent.height});
        ctx.add_color_attachment(rg_swapchain,
                                 render_graph::attachment_load_op::clear,
                                 render_graph::attachment_store_op::store,
                                 render_graph::clear_value{.color = {render_program.clear_color.r,
                                                                     render_program.clear_color.g,
                                                                     render_program.clear_color.b,
                                                                     render_program.clear_color.a}});
        ctx.set_depth_stencil_attachment(rg_depth,
                                         render_graph::attachment_load_op::clear,
                                         render_graph::attachment_store_op::dont_care,
                                         render_graph::clear_value{.depth = 1.0F});
        ctx.declare_image_output(rg_swapchain);
    }, [this, extent](execute_context& ctx)
    {
        ++run_statistics.draw_pass_executions;
        const VkCommandBuffer commands = ctx.commands();
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          static_cast<VkPipeline>(vk_pipeline_helper->GetPipeline()));

        const uint32_t dynamic_offset = static_cast<uint32_t>(uniform_stride * frame_index);
        const VkDescriptorSet descriptor_set = static_cast<VkDescriptorSet>(descriptor_sets.front());
        vkCmdBindDescriptorSets(commands,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                static_cast<VkPipelineLayout>(vk_pipeline_helper->GetPipelineLayout()),
                                0,
                                1,
                                &descriptor_set,
                                1,
                                &dynamic_offset);

        const VkViewport viewport{
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0F,
            .maxDepth = 1.0F,
        };
        vkCmdSetViewport(commands, 0, 1, &viewport);
        const VkRect2D scissor{.extent = {.width = extent.width, .height = extent.height}};
        vkCmdSetScissor(commands, 0, 1, &scissor);

        const VkBuffer arena = ctx.resources.buffer(rg_geometry);
        const VkDeviceSize base_offset = 0;
        vkCmdBindVertexBuffers(commands, 0, 1, &arena, &base_offset);
        vkCmdBindIndexBuffer(commands, arena, 0, VK_INDEX_TYPE_UINT32);
        if (current_snapshot == nullptr) return;
        for (const engine::render_object& object : current_snapshot->objects)
        {
            const auto allocation = geometry_allocations.find(object.geometry);
            if (allocation == geometry_allocations.end()) continue;
            vkCmdPushConstants(commands,
                               static_cast<VkPipelineLayout>(vk_pipeline_helper->GetPipelineLayout()),
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0,
                               sizeof(glm::mat4),
                               &object.model);
            for (const engine::draw_range& range : allocation->second.draws)
                vkCmdDrawIndexed(commands, range.index_count, 1, range.first_index, range.vertex_offset, 0);
        }
    });

    const auto result = frame_graph->compile();
    if (result.succeeded()) return true;
    for (const auto& diagnostic : result.diagnostics)
        Logger::LogError("Render graph compile failed: " + diagnostic.message);
    return false;
}

bool vulkan_backend::record_command(uint32_t image_index, VkCommandBuffer command_buffer)
{
    try
    {
        if (!build_render_graph(image_index))
        {
            frame_graph->abort_frame();
            return false;
        }
        frame_graph->bind_imported_buffer(rg_upload, runtime->buffer(runtime->resources().upload_arena));
        frame_graph->bind_imported_buffer(rg_geometry, geometry_buffer);
        frame_graph->bind_imported_buffer(rg_uniform, uniform_buffer);
        frame_graph->bind_imported_image(rg_swapchain,
                                         static_cast<VkImage>(comm_vk_swapchain_context.swapchain_images_[image_index]));
        const auto result = frame_graph->execute(command_buffer);
        if (result.succeeded()) return true;
        for (const auto& diagnostic : result.diagnostics)
            Logger::LogError("Render graph execute failed: " + diagnostic.message);
        frame_graph->abort_frame();
        return false;
    }
    catch (const std::exception& error)
    {
        Logger::LogError(std::string("Failed to record render graph commands: ") + error.what());
        frame_graph->abort_frame();
        return false;
    }
}
