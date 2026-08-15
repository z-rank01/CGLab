// Load-path micro benchmark: synthesizes an asset with N primitives and measures
// the CPU chain of one geometry upload transaction:
//   plan_geometry_uploads -> recipe row construction -> backend consume
// The backend consume is profiled in two shapes: the current per-primitive
// buffer_upload_row shape (validate handle lookup + memcpy per row) and a
// hypothetical coalesced shape (one destination resolve + a regions column),
// to quantify per-row overhead for gigabyte-scale scene submissions.
// Reports one data line per run — deliberately no pass/fail threshold, not a CI
// gate (仅作重构前后对比数据，不进 CI 门槛).

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

#include "engine/geometry_upload_plan.h"

namespace
{
    constexpr std::size_t vertices_per_primitive = 32;
    constexpr std::size_t indices_per_primitive = 48;
    constexpr std::uint32_t primitives_per_mesh = 8;
    constexpr std::uint32_t destination_handle = 0;

    // 上传行形状与 RG buffer_upload_row 一致（句柄 + 偏移 + 字节 span），
    // 基准只测形状，不引入渲染层依赖。
    struct upload_row
    {
        std::uint32_t destination = 0;
        std::uint64_t offset = 0;
        std::span<const std::byte> bytes;
    };

    // 模拟后端资源表：目的地数少（几何全进一个 arena buffer），find 为小表线性扫，
    // 与 Vulkan 后端 find_handle 同形态。
    struct mock_buffer_row
    {
        std::size_t size = 0;
        std::vector<std::byte> bytes;
    };

    struct mock_device
    {
        std::vector<mock_buffer_row> buffers;

        [[nodiscard]] mock_buffer_row* find(std::uint32_t handle)
        {
            if (handle >= buffers.size())
            {
                return nullptr;
            }
            return &buffers[handle];
        }

        // 当前形状：逐行 validate（查表 + 范围检查）后 memcpy。
        bool consume_per_row(std::span<const upload_row> uploads)
        {
            for (const auto& row : uploads)
            {
                mock_buffer_row* destination = find(row.destination);
                if (destination == nullptr || row.offset + row.bytes.size() > destination->size)
                {
                    return false;
                }
                std::memcpy(destination->bytes.data() + row.offset, row.bytes.data(), row.bytes.size());
            }
            return true;
        }

        struct region
        {
            std::uint64_t offset = 0;
            std::span<const std::byte> bytes;
        };

        // 假设的合并形状：单目的地一次查表 + 逐 region 范围检查（无逐行查表）。
        bool consume_coalesced(std::span<const region> regions)
        {
            mock_buffer_row* destination = find(destination_handle);
            if (destination == nullptr)
            {
                return false;
            }
            for (const auto& r : regions)
            {
                if (r.offset + r.bytes.size() > destination->size)
                {
                    return false;
                }
                std::memcpy(destination->bytes.data() + r.offset, r.bytes.data(), r.bytes.size());
            }
            return true;
        }
    };

    engine::asset_database make_asset(std::size_t primitive_count)
    {
        engine::asset_database asset;
        asset.name = "bench";
        asset.vertex_blob.resize(primitive_count * vertices_per_primitive);
        asset.index_blob.resize(primitive_count * indices_per_primitive);
        std::uint32_t next_vertex = 0;
        std::uint32_t next_index = 0;
        const std::size_t mesh_count = (primitive_count + primitives_per_mesh - 1) / primitives_per_mesh;
        for (std::size_t m = 0; m < mesh_count; ++m)
        {
            const std::uint32_t first = static_cast<std::uint32_t>(asset.primitives.size());
            std::uint32_t count = 0;
            while (count < primitives_per_mesh && asset.primitives.size() < primitive_count)
            {
                asset.primitives.push_back({.mesh = static_cast<std::uint32_t>(m),
                                            .material = 0,
                                            .vertex_offset = next_vertex,
                                            .vertex_count = static_cast<std::uint32_t>(vertices_per_primitive),
                                            .index_offset = next_index,
                                            .index_count = static_cast<std::uint32_t>(indices_per_primitive)});
                next_vertex += static_cast<std::uint32_t>(vertices_per_primitive);
                next_index += static_cast<std::uint32_t>(indices_per_primitive);
                ++count;
            }
            asset.meshes.push_back(
                {.name = "m" + std::to_string(m), .first_primitive = first, .primitive_count = count});
        }
        return asset;
    }

    // 镜像 recipe apply_changes 的 geometry 分支：计划 → 上传行 + draw 列。
    std::vector<upload_row> build_rows(const engine::asset_database& asset,
                                       const engine::geometry_upload_plan& layout,
                                       std::vector<engine::draw_range>& created_draws)
    {
        std::vector<upload_row> uploads;
        uploads.reserve(layout.primitive_plan_rows.size() * 2);
        created_draws.clear();
        created_draws.reserve(layout.primitive_plan_rows.size());
        for (const auto& pp : layout.primitive_plan_rows)
        {
            uploads.push_back({.destination = destination_handle,
                               .offset = pp.vertex_byte_offset,
                               .bytes = std::as_bytes(std::span(asset.vertex_blob)
                                                          .subspan(pp.vertex_element_offset, pp.vertex_count))});
            uploads.push_back({.destination = destination_handle,
                               .offset = pp.index_byte_offset,
                               .bytes = std::as_bytes(std::span(asset.index_blob)
                                                          .subspan(pp.index_element_offset, pp.index_count))});
            created_draws.push_back(pp.draw);
        }
        return uploads;
    }

    // 假设的合并形状：同批字节，一行目的地 + region 列。
    std::vector<mock_device::region> build_regions(const engine::asset_database& asset,
                                                   const engine::geometry_upload_plan& layout)
    {
        std::vector<mock_device::region> regions;
        regions.reserve(layout.primitive_plan_rows.size() * 2);
        for (const auto& pp : layout.primitive_plan_rows)
        {
            regions.push_back({pp.vertex_byte_offset,
                               std::as_bytes(std::span(asset.vertex_blob)
                                                 .subspan(pp.vertex_element_offset, pp.vertex_count))});
            regions.push_back({pp.index_byte_offset,
                               std::as_bytes(std::span(asset.index_blob)
                                                 .subspan(pp.index_element_offset, pp.index_count))});
        }
        return regions;
    }

    std::uint64_t now_us()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    struct phase_us
    {
        std::uint64_t plan = 0;
        std::uint64_t build = 0;
        std::uint64_t consume = 0;
        std::uint64_t coalesced = 0;
    };

    struct arena_pool_snapshot
    {
        std::size_t count = 1;
        std::uint64_t used_bytes = 0;
        std::uint64_t reserved_bytes = 0;
    };

    // Run repeated logical loads through the same fallback policy as the glTF
    // recipe, without allocating another payload buffer. This makes arena growth
    // and utilization visible in the benchmark output at modest host-memory cost.
    arena_pool_snapshot simulate_arena_pool(const engine::asset_database& asset,
                                            std::uint64_t arena_capacity,
                                            std::uint32_t logical_loads)
    {
        std::vector<std::uint64_t> high_water_marks{0};
        std::size_t arena = 0;
        std::uint64_t cursor = 0;
        for (std::uint32_t load = 0; load < logical_loads; ++load)
        {
            auto plan = engine::plan_geometry_uploads(
                asset, 0, static_cast<std::uint32_t>(asset.meshes.size()), 0, arena_capacity, cursor);
            if (!plan)
            {
                plan = engine::plan_geometry_uploads(
                    asset, 0, static_cast<std::uint32_t>(asset.meshes.size()), 0, arena_capacity, 0);
                if (!plan)
                {
                    return {};
                }
                arena = high_water_marks.size();
                high_water_marks.push_back(0);
            }
            cursor = plan.value.cursor;
            high_water_marks[arena] = cursor;
        }
        std::uint64_t used = 0;
        for (const std::uint64_t high_water : high_water_marks)
        {
            used += high_water;
        }
        return {.count = high_water_marks.size(),
                .used_bytes = used,
                .reserved_bytes = high_water_marks.size() * arena_capacity};
    }

    void run_scale(std::size_t primitive_count, int iterations)
    {
        const engine::asset_database asset = make_asset(primitive_count);
        const std::size_t payload_bytes =
            asset.vertex_blob.size() * sizeof(engine::vertex) + asset.index_blob.size() * sizeof(std::uint32_t);
        mock_device device;
        device.buffers.push_back({.size = payload_bytes * 2, .bytes = std::vector<std::byte>(payload_bytes * 2)});

        std::vector<phase_us> samples;
        samples.reserve(iterations);
        for (int i = 0; i < iterations + 1; ++i)
        {
            phase_us phases;
            std::vector<engine::draw_range> draws;

            const std::uint64_t plan_begin = now_us();
            const auto plan = engine::plan_geometry_uploads(asset, 0, static_cast<std::uint32_t>(asset.meshes.size()),
                                                            0, payload_bytes * 2, 0);
            phases.plan = now_us() - plan_begin;
            if (!plan)
            {
                return;
            }

            const std::uint64_t build_begin = now_us();
            const std::vector<upload_row> uploads = build_rows(asset, plan.value, draws);
            const std::vector<mock_device::region> regions = build_regions(asset, plan.value);
            phases.build = now_us() - build_begin;

            const std::uint64_t consume_begin = now_us();
            const bool consumed = device.consume_per_row(uploads);
            phases.consume = now_us() - consume_begin;

            const std::uint64_t coalesced_begin = now_us();
            const bool coalesced = device.consume_coalesced(regions);
            phases.coalesced = now_us() - coalesced_begin;

            if (!consumed || !coalesced || draws.empty())
            {
                return; // keep the compiler honest about results being used
            }
            if (i > 0)
            {
                samples.push_back(phases);
            }
        }

        const auto median = [&](std::uint64_t phase_us::*member)
        {
            std::vector<std::uint64_t> values;
            values.reserve(samples.size());
            for (const auto& sample : samples)
            {
                values.push_back(sample.*member);
            }
            std::sort(values.begin(), values.end());
            return values[values.size() / 2];
        };

        const std::uint64_t plan_median = median(&phase_us::plan);
        const std::uint64_t build_median = median(&phase_us::build);
        const std::uint64_t consume_median = median(&phase_us::consume);
        const std::uint64_t coalesced_median = median(&phase_us::coalesced);
        const std::uint64_t per_row_total = plan_median + build_median + consume_median;
        const std::uint64_t coalesced_total = plan_median + build_median + coalesced_median;
        // 行元数据体积（静态量）：每 primitive 2 条 upload_row + 1 条 draw_range
        const std::size_t row_meta_bytes = sizeof(upload_row) * 2 + sizeof(engine::draw_range);
        const std::size_t primitive_payload_bytes =
            vertices_per_primitive * sizeof(engine::vertex) + indices_per_primitive * sizeof(std::uint32_t);
        constexpr std::uint64_t arena_capacity = 256ull * 1024ull * 1024ull;
        constexpr std::uint32_t logical_loads = 3;
        const arena_pool_snapshot pool = simulate_arena_pool(asset, arena_capacity, logical_loads);
        std::cout << "[bench] load N=" << primitive_count
                  << " per_row_us=" << per_row_total
                  << " coalesced_us=" << coalesced_total
                  << " (plan=" << plan_median << " build=" << build_median
                  << " consume=" << consume_median << " coalesced_consume=" << coalesced_median << ')'
                  << " payload_MB=" << payload_bytes / (1024 * 1024)
                  << " meta_vs_payload=" << row_meta_bytes * 100 / primitive_payload_bytes << '%'
                  << " per_row_ns=" << per_row_total * 1000 / primitive_count
                  << " arena_loads=" << logical_loads
                  << " arena_count=" << pool.count
                  << " arena_reserved_MB=" << pool.reserved_bytes / (1024 * 1024)
                  << " arena_used_MB=" << pool.used_bytes / (1024 * 1024)
                  << " arena_utilization_pct="
                  << (pool.reserved_bytes == 0 ? 0 : pool.used_bytes * 100 / pool.reserved_bytes)
                  << '\n';
    }
} // namespace

int main()
{
    run_scale(4096, 20);
    run_scale(65536, 5);
    return 0;
}
