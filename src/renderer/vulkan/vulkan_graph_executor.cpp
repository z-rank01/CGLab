#include "renderer/vulkan/vulkan_backend_internal.h"

bool vulkan_backend::build_render_graph(uint32_t image_index)
{
    using setup_context = frame_render_graph::pass_setup_context;
    using execute_context = frame_render_graph::pass_execute_context;

    const auto extent = comm_vk_swapchain_context.swapchain_info_.extent_;
    const auto format = static_cast<uint64_t>(comm_vk_swapchain_context.swapchain_info_.surface_format_.format);
    const bool swapchain_initialized = swapchain_image_states.is_initialized(image_index);
    // P2：runtime upload 批次并入 cache key —— 有待上传批次时编译含 RuntimeUploadPass 的图变体；
    // upload_serial 区分连续批次，避免复用到过期 staging 句柄的旧变体。
    const uint64_t graph_cache_key = (static_cast<uint64_t>(extent.width) << 32) ^
                                     static_cast<uint64_t>(extent.height) ^
                                     (format << 2) ^
                                     (static_cast<uint64_t>(mesh_upload_pending) << 1) ^
                                     static_cast<uint64_t>(swapchain_initialized) ^
                                     (static_cast<uint64_t>(runtime_upload_pending) << 48) ^
                                     (upload_serial << 49);
    frame_graph->begin_frame(submitted_frame, completed_frame, graph_cache_key);
    if (!frame_graph->needs_recompile())
    {
        return true;
    }
    frame_graph->clear();

    const VkBufferCreateInfo staging_desc{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = staging_buffer_allocation_info.size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VkBufferCreateInfo local_desc{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = local_buffer_allocation_info.size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VkBufferCreateInfo uniform_desc{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = uniform_buffer_allocation_info.size,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (mesh_upload_pending)
    {
        frame_graph->add_copy_pass("UploadPass", [this, staging_desc, local_desc](setup_context& ctx)
        {
            rg_staging = ctx.create_buffer("MeshStaging", staging_desc, render_graph::resource_lifetime_class::imported);
            rg_local = ctx.create_buffer("MeshLocal", local_desc, render_graph::resource_lifetime_class::imported);
            const render_graph::buffer_access_desc transfer_src{
                .usage = render_graph::buffer_usage::TRANSFER_SRC,
                .domain = render_graph::pipeline_domain::copy,
            };
            ctx.set_initial_state(rg_staging,
                                  transfer_src,
                                  render_graph::access_type::read,
                                  render_graph::contents_policy::preserve);
            ctx.read_buffer(rg_staging, transfer_src);
            ctx.write_buffer(rg_local,
                             render_graph::buffer_access_desc{
                                 .usage = render_graph::buffer_usage::TRANSFER_DST,
                                 .domain = render_graph::pipeline_domain::copy,
                             });
        }, [this](execute_context& ctx)
        {
            ++run_statistics.upload_pass_executions;
            const VkBufferCopy copy{
                .srcOffset = 0,
                .dstOffset = 0,
                .size = local_host_batch_handle[vra::VraBuiltInBatchIds::CPU_GPU_Rarely].consolidated_data.size(),
            };
            vkCmdCopyBuffer(ctx.commands(), ctx.resources.buffer(rg_staging), ctx.resources.buffer(rg_local), 1, &copy);
        });
    }

    const VkBufferCreateInfo arena_vertex_desc{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = arena_vertex_allocation_info.size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VkBufferCreateInfo arena_index_desc{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = arena_index_allocation_info.size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    // P2：运行时加载对象的上传通道。每个批次一个 copy pass：
    // 各对象 staging -> geometry arena（与 legacy UploadPass 同一模式，一次性拷贝）。
    if (runtime_upload_pending)
    {
        frame_graph->add_copy_pass("RuntimeUploadPass", [this, arena_vertex_desc, arena_index_desc](setup_context& ctx)
        {
            rg_arena_vertex = ctx.create_buffer("ArenaVertex", arena_vertex_desc, render_graph::resource_lifetime_class::imported);
            rg_arena_index  = ctx.create_buffer("ArenaIndex", arena_index_desc, render_graph::resource_lifetime_class::imported);
            const render_graph::buffer_access_desc transfer_src{
                .usage = render_graph::buffer_usage::TRANSFER_SRC,
                .domain = render_graph::pipeline_domain::copy,
            };
            const render_graph::buffer_access_desc transfer_dst{
                .usage = render_graph::buffer_usage::TRANSFER_DST,
                .domain = render_graph::pipeline_domain::copy,
            };
            rg_runtime_stagings.clear();
            rg_runtime_stagings.reserve(queued_uploads.size());
            for (std::size_t i = 0; i < queued_uploads.size(); ++i)
            {
                const VkBufferCreateInfo staging_desc_i{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .size = queued_uploads[i].staging_size,
                    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                };
                const render_graph::buffer_handle handle =
                    ctx.create_buffer(std::string("RuntimeStaging") + std::to_string(i),
                                      staging_desc_i,
                                      render_graph::resource_lifetime_class::imported);
                ctx.set_initial_state(handle,
                                      transfer_src,
                                      render_graph::access_type::read,
                                      render_graph::contents_policy::preserve);
                ctx.read_buffer(handle, transfer_src);
                rg_runtime_stagings.push_back(handle);
            }
            ctx.write_buffer(rg_arena_vertex, transfer_dst);
            ctx.write_buffer(rg_arena_index, transfer_dst);
        }, [this](execute_context& ctx)
        {
            ++run_statistics.upload_pass_executions;
            const VkBuffer arena_v = ctx.resources.buffer(rg_arena_vertex);
            const VkBuffer arena_i = ctx.resources.buffer(rg_arena_index);
            for (std::size_t i = 0; i < queued_uploads.size(); ++i)
            {
                const runtime_upload& upload = queued_uploads[i];
                const VkBuffer staging       = ctx.resources.buffer(rg_runtime_stagings[i]);
                if (!upload.vertex_copies.empty())
                {
                    vkCmdCopyBuffer(ctx.commands(), staging, arena_v,
                                    static_cast<uint32_t>(upload.vertex_copies.size()), upload.vertex_copies.data());
                }
                if (!upload.index_copies.empty())
                {
                    vkCmdCopyBuffer(ctx.commands(), staging, arena_i,
                                    static_cast<uint32_t>(upload.index_copies.size()), upload.index_copies.data());
                }
            }
        });
    }

    const VkImageCreateInfo swapchain_desc{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = static_cast<VkFormat>(comm_vk_swapchain_context.swapchain_info_.surface_format_.format),
        .extent = {.width = extent.width, .height = extent.height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VkImageCreateInfo depth_desc{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = static_cast<VkFormat>(depth_format),
        .extent = {.width = extent.width, .height = extent.height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    frame_graph->add_raster_pass(render_program.pass_name, [this, local_desc, uniform_desc, swapchain_desc, depth_desc, extent, swapchain_initialized, arena_vertex_desc, arena_index_desc](setup_context& ctx)
    {
        const render_graph::buffer_access_desc mesh_read{
            .usage = render_graph::buffer_usage::VERTEX_BUFFER | render_graph::buffer_usage::INDEX_BUFFER,
            .domain = render_graph::pipeline_domain::graphics,
        };
        if (!mesh_upload_pending)
        {
            rg_local = ctx.create_buffer("MeshLocal", local_desc, render_graph::resource_lifetime_class::imported);
            ctx.set_initial_state(rg_local,
                                  mesh_read,
                                  render_graph::access_type::read,
                                  render_graph::contents_policy::preserve);
        }
        ctx.read_buffer(rg_local, mesh_read);

        // P2：geometry arena（运行时对象）。无待上传批次时由本 pass 创建句柄并声明初始状态；
        // 有批次时句柄由 RuntimeUploadPass 创建（初始状态 = 本图内先写后读）。
        if (!runtime_upload_pending)
        {
            rg_arena_vertex = ctx.create_buffer("ArenaVertex", arena_vertex_desc, render_graph::resource_lifetime_class::imported);
            rg_arena_index  = ctx.create_buffer("ArenaIndex", arena_index_desc, render_graph::resource_lifetime_class::imported);
            ctx.set_initial_state(rg_arena_vertex,
                                  mesh_read,
                                  render_graph::access_type::read,
                                  render_graph::contents_policy::preserve);
            ctx.set_initial_state(rg_arena_index,
                                  mesh_read,
                                  render_graph::access_type::read,
                                  render_graph::contents_policy::preserve);
        }
        ctx.read_buffer(rg_arena_vertex, mesh_read);
        ctx.read_buffer(rg_arena_index, mesh_read);

        rg_uniform = ctx.create_buffer("FrameUniform", uniform_desc, render_graph::resource_lifetime_class::imported);
        const render_graph::buffer_access_desc uniform_read{
            .usage = render_graph::buffer_usage::UNIFORM_BUFFER,
            .domain = render_graph::pipeline_domain::graphics,
        };
        ctx.set_initial_state(rg_uniform,
                              uniform_read,
                              render_graph::access_type::read,
                              render_graph::contents_policy::preserve);
        ctx.read_buffer(rg_uniform, uniform_read);

        rg_swapchain = ctx.create_image("Swapchain", swapchain_desc, render_graph::resource_lifetime_class::imported);
        const render_graph::image_access_desc present{
            .usage = render_graph::image_usage::PRESENT,
            .domain = render_graph::pipeline_domain::graphics,
        };
        const render_graph::image_access_desc initial_swapchain_state = swapchain_initialized
            ? present
            : render_graph::image_access_desc{
                  .usage = render_graph::image_usage::NONE,
                  .domain = render_graph::pipeline_domain::graphics,
              };
        ctx.set_initial_state(rg_swapchain,
                              initial_swapchain_state,
                              render_graph::access_type::read,
                              swapchain_initialized ? render_graph::contents_policy::preserve
                                                    : render_graph::contents_policy::discard);
        ctx.set_final_state(rg_swapchain, present, render_graph::access_type::read);

        rg_depth = ctx.create_image("Depth", depth_desc, render_graph::resource_lifetime_class::transient);
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
        const auto commands = ctx.commands();
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(vk_pipeline_helper->GetPipeline()));

        const auto offset = uniform_batch_handle[vra::VraBuiltInBatchIds::CPU_GPU_Frequently].offsets[uniform_buffer_id[frame_index]];
        const auto dynamic_offset = static_cast<uint32_t>(offset);
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
        const VkRect2D scissor{.offset = {.x = 0, .y = 0}, .extent = {.width = extent.width, .height = extent.height}};
        vkCmdSetScissor(commands, 0, 1, &scissor);

        const VkBuffer mesh_buffer = ctx.resources.buffer(rg_local);
        const VkDeviceSize vertex_offset = local_host_batch_handle[vra::VraBuiltInBatchIds::GPU_Only].offsets[vertex_buffer_id];
        vkCmdBindVertexBuffers(commands, 0, 1, &mesh_buffer, &vertex_offset);
        vkCmdBindIndexBuffer(commands,
                             mesh_buffer,
                             local_host_batch_handle[vra::VraBuiltInBatchIds::GPU_Only].offsets[index_buffer_id],
                             VK_INDEX_TYPE_UINT32);

        // P2：对象级 model 走 push constant；legacy 轨道恒为 identity（保持原行为）
        const glm::mat4 identity_model(1.0F);
        vkCmdPushConstants(commands,
                           static_cast<VkPipelineLayout>(vk_pipeline_helper->GetPipelineLayout()),
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0,
                           sizeof(glm::mat4),
                           &identity_model);

        // Draw immutable frame data through opaque geometry handles.
        if (current_snapshot != nullptr)
        {
            const VkBuffer arena_vertex = ctx.resources.buffer(rg_arena_vertex);
            const VkBuffer arena_index  = ctx.resources.buffer(rg_arena_index);
            const VkDeviceSize arena_base_offset = 0;
            vkCmdBindVertexBuffers(commands, 0, 1, &arena_vertex, &arena_base_offset);
            vkCmdBindIndexBuffer(commands, arena_index, 0, VK_INDEX_TYPE_UINT32);

            for (const engine::render_object& object : current_snapshot->objects)
            {
                const auto allocation = geometry_allocations.find(object.geometry);
                if (allocation == geometry_allocations.end())
                {
                    continue;
                }
                vkCmdPushConstants(commands,
                                   static_cast<VkPipelineLayout>(vk_pipeline_helper->GetPipelineLayout()),
                                   VK_SHADER_STAGE_VERTEX_BIT,
                                   0,
                                   sizeof(glm::mat4),
                                   &object.model);
                for (const engine::draw_range& range : allocation->second.draws)
                {
                    vkCmdDrawIndexed(commands, range.index_count, 1, range.first_index, range.vertex_offset, 0);
                }
            }
        }
    });

    const auto result = frame_graph->compile();
    if (!result.succeeded())
    {
        for (const auto& diagnostic : result.diagnostics)
        {
            Logger::LogError("Render graph compile failed: " + diagnostic.message);
        }
        return false;
    }
    return true;
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

        frame_graph->bind_imported_buffer(rg_local, static_cast<VkBuffer>(local_buffer));
        frame_graph->bind_imported_buffer(rg_uniform, static_cast<VkBuffer>(uniform_buffer));
        if (mesh_upload_pending)
        {
            frame_graph->bind_imported_buffer(rg_staging, static_cast<VkBuffer>(staging_buffer));
        }
        frame_graph->bind_imported_buffer(rg_arena_vertex, static_cast<VkBuffer>(arena_vertex_buffer));
        frame_graph->bind_imported_buffer(rg_arena_index, static_cast<VkBuffer>(arena_index_buffer));
        if (runtime_upload_pending)
        {
            for (std::size_t i = 0; i < queued_uploads.size(); ++i)
            {
                frame_graph->bind_imported_buffer(rg_runtime_stagings[i], static_cast<VkBuffer>(queued_uploads[i].staging));
            }
        }
        frame_graph->bind_imported_image(rg_swapchain,
                                         static_cast<VkImage>(comm_vk_swapchain_context.swapchain_images_[image_index]));

        const auto execute_result = frame_graph->execute(command_buffer);
        if (!execute_result.succeeded())
        {
            for (const auto& diagnostic : execute_result.diagnostics)
            {
                Logger::LogError("Render graph execute failed: " + diagnostic.message);
            }
            frame_graph->abort_frame();
            return false;
        }
        return true;
    }
    catch (const vk::SystemError& error)
    {
        Logger::LogError(std::string("Failed to record render graph commands: ") + error.what());
        frame_graph->abort_frame();
        return false;
    }
}
