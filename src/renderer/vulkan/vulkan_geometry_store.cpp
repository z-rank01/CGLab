#include "renderer/vulkan/vulkan_backend_internal.h"

namespace
{
    constexpr VkDeviceSize arena_vertex_capacity = 64ull * 1024 * 1024;
    constexpr VkDeviceSize arena_index_capacity  = 16ull * 1024 * 1024;

    bool arena_alloc(VkDeviceSize size,
                     VkDeviceSize alignment,
                     VkDeviceSize& cursor,
                     VkDeviceSize capacity,
                     std::vector<std::pair<VkDeviceSize, VkDeviceSize>>& free_list,
                     VkDeviceSize& out_offset)
    {
        for (std::size_t i = 0; i < free_list.size(); ++i)
        {
            const VkDeviceSize span_end = free_list[i].first + free_list[i].second;
            const VkDeviceSize aligned = (free_list[i].first + alignment - 1) / alignment * alignment;
            if (aligned + size > span_end)
            {
                continue;
            }
            out_offset = aligned;
            const VkDeviceSize head_bytes = aligned - free_list[i].first;
            const VkDeviceSize tail_bytes = span_end - (aligned + size);
            free_list.erase(free_list.begin() + static_cast<std::ptrdiff_t>(i));
            if (tail_bytes > 0)
            {
                free_list.emplace_back(aligned + size, tail_bytes);
            }
            if (head_bytes >= alignment)
            {
                free_list.emplace_back(aligned - head_bytes, head_bytes);
            }
            return true;
        }
        const VkDeviceSize aligned = (cursor + alignment - 1) / alignment * alignment;
        if (aligned + size > capacity)
        {
            return false;
        }
        out_offset = aligned;
        cursor = aligned + size;
        return true;
    }
} // namespace

bool vulkan_backend::create_geometry_arena()
{
    VmaAllocationCreateInfo allocation_create_info{};
    allocation_create_info.usage = VMA_MEMORY_USAGE_AUTO;

    VkBufferCreateInfo vertex_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    vertex_info.size        = arena_vertex_capacity;
    vertex_info.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertex_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!Logger::LogWithVkResult(vmaCreateBuffer(vma_allocator,
                                                 &vertex_info,
                                                 &allocation_create_info,
                                                 reinterpret_cast<VkBuffer*>(&arena_vertex_buffer),
                                                 &arena_vertex_allocation,
                                                 &arena_vertex_allocation_info),
                                 "Failed to create geometry arena vertex buffer",
                                 "Succeeded in creating geometry arena vertex buffer"))
    {
        return false;
    }

    VkBufferCreateInfo index_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    index_info.size        = arena_index_capacity;
    index_info.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    index_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return Logger::LogWithVkResult(vmaCreateBuffer(vma_allocator,
                                                   &index_info,
                                                   &allocation_create_info,
                                                   reinterpret_cast<VkBuffer*>(&arena_index_buffer),
                                                   &arena_index_allocation,
                                                   &arena_index_allocation_info),
                                   "Failed to create geometry arena index buffer",
                                   "Succeeded in creating geometry arena index buffer");
}
void vulkan_backend::destroy_runtime_upload(VmaAllocator allocator, runtime_upload& upload) noexcept
{
    if (allocator != VK_NULL_HANDLE && upload.staging != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator, upload.staging, upload.staging_allocation);
    }
    upload.staging            = VK_NULL_HANDLE;
    upload.staging_allocation = VK_NULL_HANDLE;
}

bool vulkan_backend::stage_runtime_geometry(const std::vector<engine::geometry_primitive>& primitives, staged_geometry& out)
{
    if (primitives.empty())
    {
        Logger::LogError("stage_runtime_geometry: empty primitive list");
        return false;
    }

    struct primitive_span
    {
        VkDeviceSize vertex_offset = 0;
        VkDeviceSize vertex_bytes  = 0;
        VkDeviceSize index_offset  = 0;
        VkDeviceSize index_bytes   = 0;
    };
    std::vector<primitive_span> spans(primitives.size());
    staged_geometry result;

    // 1) 分配 arena 区间；任一失败则回滚本批次已分配区间
    const auto rollback = [this, &result]()
    {
        for (const auto& span : result.vertex_spans)
        {
            arena_vertex_free_list.push_back(span);
        }
        for (const auto& span : result.index_spans)
        {
            arena_index_free_list.push_back(span);
        }
    };

    for (std::size_t i = 0; i < primitives.size(); ++i)
    {
        spans[i].vertex_bytes = sizeof(engine::vertex) * primitives[i].vertices.size();
        spans[i].index_bytes  = sizeof(uint32_t) * primitives[i].indices.size();
        if (spans[i].vertex_bytes == 0 || spans[i].index_bytes == 0)
        {
            Logger::LogError("stage_runtime_geometry: primitive with empty vertex/index data");
            rollback();
            return false;
        }
        // 顶点区间按 sizeof(Vertex) 对齐，保证 vertex_offset 可整除换算为顶点下标
        if (!arena_alloc(spans[i].vertex_bytes, sizeof(engine::vertex), arena_vertex_cursor, arena_vertex_capacity,
                         arena_vertex_free_list, spans[i].vertex_offset) ||
            !arena_alloc(spans[i].index_bytes, sizeof(uint32_t), arena_index_cursor, arena_index_capacity,
                         arena_index_free_list, spans[i].index_offset))
        {
            Logger::LogError("stage_runtime_geometry: geometry arena capacity exceeded");
            rollback();
            return false;
        }
        result.vertex_spans.emplace_back(spans[i].vertex_offset, spans[i].vertex_bytes);
        result.index_spans.emplace_back(spans[i].index_offset, spans[i].index_bytes);
    }

    // 2) 创建并填充 staging（布局：全部顶点块在前，全部索引块在后）
    VkDeviceSize total_vertex_bytes = 0;
    VkDeviceSize total_index_bytes  = 0;
    for (const primitive_span& span : spans)
    {
        total_vertex_bytes += span.vertex_bytes;
        total_index_bytes += span.index_bytes;
    }

    runtime_upload upload;
    upload.staging_size = total_vertex_bytes + total_index_bytes;
    VkBufferCreateInfo staging_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    staging_info.size        = upload.staging_size;
    staging_info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo staging_alloc_info{};
    staging_alloc_info.usage         = VMA_MEMORY_USAGE_AUTO;
    staging_alloc_info.flags         = vra_data_batcher->GetSuggestVmaMemoryFlags(vra::VraDataMemoryPattern::CPU_GPU,
                                                                                  vra::VraDataUpdateRate::RarelyOrNever);
    staging_alloc_info.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (vmaCreateBuffer(vma_allocator,
                        &staging_info,
                        &staging_alloc_info,
                        reinterpret_cast<VkBuffer*>(&upload.staging),
                        &upload.staging_allocation,
                        nullptr) != VK_SUCCESS)
    {
        Logger::LogError("stage_runtime_geometry: failed to create staging buffer");
        rollback();
        return false;
    }

    void* mapped = nullptr;
    vmaMapMemory(vma_allocator, upload.staging_allocation, &mapped);
    auto* staging_bytes = static_cast<uint8_t*>(mapped);
    VkDeviceSize staging_vertex_cursor = 0;
    VkDeviceSize staging_index_cursor  = total_vertex_bytes;
    upload.vertex_copies.reserve(primitives.size());
    upload.index_copies.reserve(primitives.size());
    for (std::size_t i = 0; i < primitives.size(); ++i)
    {
        std::memcpy(staging_bytes + staging_vertex_cursor, primitives[i].vertices.data(), spans[i].vertex_bytes);
        upload.vertex_copies.push_back(VkBufferCopy{staging_vertex_cursor, spans[i].vertex_offset, spans[i].vertex_bytes});
        staging_vertex_cursor += spans[i].vertex_bytes;

        std::memcpy(staging_bytes + staging_index_cursor, primitives[i].indices.data(), spans[i].index_bytes);
        upload.index_copies.push_back(VkBufferCopy{staging_index_cursor, spans[i].index_offset, spans[i].index_bytes});
        staging_index_cursor += spans[i].index_bytes;
    }
    vmaUnmapMemory(vma_allocator, upload.staging_allocation);
    vmaFlushAllocation(vma_allocator, upload.staging_allocation, 0, VK_WHOLE_SIZE);

    // 3) 回填 draw ranges（vkCmdDrawIndexed 语义：first_index 单位 index，vertex_offset 单位 vertex）
    result.draws.reserve(primitives.size());
    for (std::size_t i = 0; i < primitives.size(); ++i)
    {
        result.draws.push_back(engine::draw_range{
            .first_index   = static_cast<uint32_t>(spans[i].index_offset / sizeof(uint32_t)),
            .index_count   = static_cast<uint32_t>(primitives[i].indices.size()),
            .vertex_offset = static_cast<int32_t>(spans[i].vertex_offset / sizeof(engine::vertex)),
        });
    }

    // 4) 入队，等待并入下一帧的图变体
    queued_uploads.push_back(std::move(upload));
    runtime_upload_pending = true;
    ++upload_serial;
    out = std::move(result);
    return true;
}

void vulkan_backend::retire_runtime_geometry(staged_geometry geometry)
{
    if (geometry.vertex_spans.empty() && geometry.index_spans.empty())
    {
        return;
    }
    deferred_frees.push_back(deferred_arena_free{submitted_frame, std::move(geometry)});
}

void vulkan_backend::collect_deferred_resources()
{
    // staging：对应提交帧完成后销毁（in_flight 按提交顺序入队，front 最旧）
    while (!in_flight_uploads.empty() && completed_frame >= in_flight_uploads.front().first)
    {
        destroy_runtime_upload(vma_allocator, in_flight_uploads.front().second);
        in_flight_uploads.pop_front();
    }

    // arena 区间：对应提交帧完成后回到空闲链表
    std::size_t kept = 0;
    for (std::size_t i = 0; i < deferred_frees.size(); ++i)
    {
        if (completed_frame >= deferred_frees[i].gate_frame)
        {
            for (const auto& span : deferred_frees[i].geometry.vertex_spans)
            {
                arena_vertex_free_list.push_back(span);
            }
            for (const auto& span : deferred_frees[i].geometry.index_spans)
            {
                arena_index_free_list.push_back(span);
            }
        }
        else
        {
            deferred_frees[kept++] = std::move(deferred_frees[i]);
        }
    }
    deferred_frees.resize(kept);
}
