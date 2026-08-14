#pragma once

// engine::plan_geometry_uploads —— 加载路径的 geometry 批量上传布局规划（纯函数）。
//
// 把"整资产 mesh 行段 → arena 内字节布局"从 recipe 复制粘贴中提取为可单测的纯函数：
// - 输入：共享 blob 行模型（dcl::asset_database）+ mesh 范围 + material_base + arena 容量/游标；
// - 输出：按 mesh 展平的 primitive 布局计划行（对齐后字节偏移、源 blob 元素偏移、draw 语义）
//   与每 mesh 图元计数（句柄边界）；任何越界/容量耗尽返回结构化 error（engine::result）。
// - 无副作用、无 RG 类型依赖；recipe 消费该计划直接构造上传行（零拷贝 span 引用 blob）。
//
// 行结构说明：计划行是一次性行——消费方整行读取全部字段并即时转成上传行/draw 行，
// 逐行整读无逐列复用，拆列无收益；行名遵循行表命名约定（单元素 *_row、同质 vector *_rows），
// draw 语义字段复用 engine::draw_range 不重复声明。

#include <cstdint>
#include <vector>

#include "engine/geometry.h"
#include "engine/render_backend.h"

namespace engine
{
    // 每 primitive 的 arena 布局计划行
    struct primitive_plan_row
    {
        std::uint64_t vertex_byte_offset = 0;   // arena 内顶点切片起点（按顶点步长对齐）
        std::uint64_t index_byte_offset = 0;    // arena 内索引切片起点（4 字节对齐）
        std::uint32_t vertex_element_offset = 0; // 源 vertex_blob 元素偏移（subspan 用）
        std::uint32_t vertex_count = 0;
        std::uint32_t index_element_offset = 0;  // 源 index_blob 元素偏移（subspan 用）
        std::uint32_t index_count = 0;
        engine::draw_range draw{};               // draw 语义（first_index/index_count/vertex_offset/material_index）
    };

    struct geometry_upload_plan
    {
        std::vector<primitive_plan_row> primitive_plan_rows; // 按 mesh 顺序展平
        std::vector<std::uint32_t> mesh_primitive_counts;    // 每 mesh 图元数（句柄边界）
        std::uint64_t cursor = 0;                            // 计划末尾 arena 游标（成功时）
    };

    // 为 [first_mesh, first_mesh + mesh_count) 的 mesh 行段生成布局计划。
    // material_base 由材质上传结果传入；capacity 为 arena 容量，cursor 为当前游标。
    [[nodiscard]] inline engine::result<geometry_upload_plan> plan_geometry_uploads(
        const asset_database& asset,
        std::uint32_t first_mesh,
        std::uint32_t mesh_count,
        std::uint32_t material_base,
        std::uint64_t capacity,
        std::uint64_t cursor)
    {
        engine::result<geometry_upload_plan> result;
        geometry_upload_plan& plan = result.value;
        if (mesh_count == 0 || first_mesh >= asset.meshes.size() ||
            static_cast<std::uint64_t>(first_mesh) + mesh_count > asset.meshes.size())
        {
            result.error = "Mesh range out of bounds";
            return result;
        }

        // 上界预留（最坏情况整资产图元全部落入范围），稳态零分配
        plan.primitive_plan_rows.reserve(asset.primitives.size());
        plan.mesh_primitive_counts.reserve(mesh_count);

        for (std::uint32_t m = 0; m < mesh_count; ++m)
        {
            const auto& mesh = asset.meshes[first_mesh + m];
            std::uint32_t primitive_count = 0;
            for (std::uint32_t r = 0; r < mesh.primitive_count; ++r)
            {
                const auto& primitive = asset.primitives[mesh.first_primitive + r];
                // 源 blob 切片越界检查（元素语义）
                if (static_cast<std::uint64_t>(primitive.vertex_offset) + primitive.vertex_count > asset.vertex_blob.size() ||
                    static_cast<std::uint64_t>(primitive.index_offset) + primitive.index_count > asset.index_blob.size())
                {
                    result.error = "Primitive slice out of blob bounds";
                    return result;
                }

                primitive_plan_row row;
                const std::uint64_t vertex_size = static_cast<std::uint64_t>(primitive.vertex_count) * sizeof(vertex);
                row.vertex_byte_offset = (cursor + sizeof(vertex) - 1) / sizeof(vertex) * sizeof(vertex);
                const std::uint64_t index_size = static_cast<std::uint64_t>(primitive.index_count) * sizeof(std::uint32_t);
                row.index_byte_offset =
                    (row.vertex_byte_offset + vertex_size + sizeof(std::uint32_t) - 1) / sizeof(std::uint32_t) *
                    sizeof(std::uint32_t);
                if (row.index_byte_offset + index_size > capacity)
                {
                    result.error = "Geometry arena exhausted";
                    return result;
                }
                row.vertex_element_offset = primitive.vertex_offset;
                row.vertex_count = primitive.vertex_count;
                row.index_element_offset = primitive.index_offset;
                row.index_count = primitive.index_count;
                row.draw = {
                    .first_index = static_cast<std::uint32_t>(row.index_byte_offset / sizeof(std::uint32_t)),
                    .index_count = row.index_count,
                    .vertex_offset = static_cast<std::int32_t>(row.vertex_byte_offset / sizeof(vertex)),
                    .material_index = primitive.material == invalid_asset_index ? material_base
                                                                                : material_base + primitive.material,
                };
                plan.primitive_plan_rows.push_back(row);
                cursor = row.index_byte_offset + index_size;
                ++primitive_count;
            }
            plan.mesh_primitive_counts.push_back(primitive_count);
        }
        plan.cursor = cursor;
        return result;
    }
} // namespace engine
