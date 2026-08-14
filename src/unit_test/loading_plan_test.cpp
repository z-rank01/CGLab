// 加载路径：geometry 批量上传规划单测
// 覆盖：offset 对齐、句柄边界（mesh 图元计数）、material_base 映射、游标推进、
// 源 blob 越界 / 容量耗尽 / mesh 范围越界报错、多 mesh 顺序展平。

#include <cstdlib>
#include <iostream>

#include "engine/geometry_upload_plan.h"

namespace
{
    int failures = 0;

    void check(bool condition, const char* name)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "[FAIL] " << name << '\n';
        }
    }

    // 两个 mesh、共 3 个图元的资产：mesh0 = {prim0(3 顶点/3 索引), prim1(2 顶点/4 索引)}，
    // mesh1 = {prim2(4 顶点/6 索引)}；blob 尺寸恰好容纳。
    engine::asset_database make_asset()
    {
        engine::asset_database asset;
        asset.name = "plan test";
        asset.vertex_blob.resize(9);
        asset.index_blob.resize(13);
        asset.meshes.push_back({.name = "m0", .first_primitive = 0, .primitive_count = 2});
        asset.meshes.push_back({.name = "m1", .first_primitive = 2, .primitive_count = 1});
        asset.primitives.push_back({.mesh = 0, .material = 0, .vertex_offset = 0, .vertex_count = 3, .index_offset = 0, .index_count = 3});
        asset.primitives.push_back({.mesh = 0, .material = 1, .vertex_offset = 3, .vertex_count = 2, .index_offset = 3, .index_count = 4});
        asset.primitives.push_back({.mesh = 1, .material = engine::invalid_asset_index, .vertex_offset = 5, .vertex_count = 4, .index_offset = 7, .index_count = 6});
        return asset;
    }

    void test_alignment_and_cursor()
    {
        auto asset = make_asset();
        const auto plan = engine::plan_geometry_uploads(asset, 0, 2, 10, 1ull << 30, 0);
        check(static_cast<bool>(plan), "plan succeeds");
        check(plan.value.primitive_plan_rows.size() == 3, "three primitives flattened");
        check(plan.value.mesh_primitive_counts.size() == 2, "two mesh boundaries");
        check(plan.value.mesh_primitive_counts[0] == 2 && plan.value.mesh_primitive_counts[1] == 1, "mesh counts correct");

        // 对齐：顶点偏移为顶点步长倍数，索引偏移为 4 字节倍数
        for (const auto& pp : plan.value.primitive_plan_rows)
        {
            check(pp.vertex_byte_offset % sizeof(engine::vertex) == 0, "vertex offset aligned to vertex stride");
            check(pp.index_byte_offset % 4 == 0, "index offset aligned to 4 bytes");
        }
        // 首图元从游标 0 开始；索引切片紧邻顶点切片（无填充）
        check(plan.value.primitive_plan_rows[0].vertex_byte_offset == 0, "first primitive starts at cursor");
        const auto& p0 = plan.value.primitive_plan_rows[0];
        check(p0.index_byte_offset == p0.vertex_byte_offset + p0.vertex_count * sizeof(engine::vertex),
              "index slice follows vertex slice");
        // 游标 = 末图元末尾
        const auto& p2 = plan.value.primitive_plan_rows[2];
        check(plan.value.cursor == p2.index_byte_offset + p2.index_count * sizeof(std::uint32_t), "cursor at plan end");

        // draw 语义：first_index / vertex_offset 换算正确（含跨图元的顶点步长对齐填充）
        check(p0.draw.first_index == 3 * sizeof(engine::vertex) / 4 && p0.draw.vertex_offset == 0,
              "draw units of first primitive");
        const std::uint64_t cursor_after_p0 = 3 * sizeof(engine::vertex) + 3 * sizeof(std::uint32_t);
        const auto& p1 = plan.value.primitive_plan_rows[1];
        check(p1.vertex_byte_offset == (cursor_after_p0 + sizeof(engine::vertex) - 1) / sizeof(engine::vertex) * sizeof(engine::vertex),
              "vertex slice aligned to vertex stride after padding");
        check(p1.draw.vertex_offset == static_cast<std::int32_t>(p1.vertex_byte_offset / sizeof(engine::vertex)),
              "vertex offset in vertex units");
        check(plan.value.primitive_plan_rows[2].draw.first_index == p2.index_byte_offset / 4, "first index in index units");
    }

    void test_material_mapping()
    {
        auto asset = make_asset();
        const auto plan = engine::plan_geometry_uploads(asset, 0, 2, 10, 1ull << 30, 0);
        check(plan.value.primitive_plan_rows[0].draw.material_index == 10, "material_base applied");
        check(plan.value.primitive_plan_rows[1].draw.material_index == 11, "material_base plus row material");
        check(plan.value.primitive_plan_rows[2].draw.material_index == 10, "invalid material falls back to base");
    }

    void test_range_errors()
    {
        auto asset = make_asset();
        check(!engine::plan_geometry_uploads(asset, 0, 0, 0, 1ull << 30, 0), "zero mesh count rejected");
        check(!engine::plan_geometry_uploads(asset, 2, 1, 0, 1ull << 30, 0), "first mesh out of bounds rejected");
        check(!engine::plan_geometry_uploads(asset, 1, 2, 0, 1ull << 30, 0), "mesh range overflow rejected");

        // 容量耗尽：仅能容纳首图元
        const std::uint64_t tiny = 3 * sizeof(engine::vertex) + 3 * sizeof(std::uint32_t);
        check(!engine::plan_geometry_uploads(asset, 0, 1, 0, tiny, 0), "capacity exhaustion rejected");

        // 源 blob 越界：primitive 切片超出 blob
        auto broken = make_asset();
        broken.primitives[0].vertex_count = 100;
        check(!engine::plan_geometry_uploads(broken, 0, 1, 0, 1ull << 30, 0), "blob slice overflow rejected");
    }

    void test_cursor_respect()
    {
        auto asset = make_asset();
        // 非零游标：首图元顶点偏移对齐到游标之后
        const auto plan = engine::plan_geometry_uploads(asset, 1, 1, 0, 1ull << 30, 7);
        check(static_cast<bool>(plan), "plan with nonzero cursor succeeds");
        check(plan.value.primitive_plan_rows.size() == 1, "single mesh range");
        check(plan.value.primitive_plan_rows[0].vertex_byte_offset % sizeof(engine::vertex) == 0, "cursor alignment respected");
        check(plan.value.mesh_primitive_counts[0] == 1, "single mesh count");
    }
} // namespace

int main()
{
    test_alignment_and_cursor();
    test_material_mapping();
    test_range_errors();
    test_cursor_respect();

    if (failures != 0)
    {
        std::cerr << failures << " loading plan test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All loading plan tests passed\n";
    return EXIT_SUCCESS;
}
