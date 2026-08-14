// Extract-scale micro benchmark: synthesizes N scene objects and measures the
// column-based extract chain
//   refresh_matrices -> active_slots -> hot-column reads -> instance/transform rows
// which mirrors engine_runtime::extract_render_packet's per-frame path.
// Reports one data line per run — deliberately no pass/fail threshold, not a CI
// gate (仅作重构前后对比数据，不进 CI 门槛).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "engine/render_backend.h"
#include "scene/scene_registry.h"

namespace
{
    constexpr std::size_t object_count = 4096;
    constexpr int iterations = 20;  // timed runs (after 1 warmup)

    scene::scene_registry synthesize()
    {
        scene::scene_registry registry;
        for (std::size_t i = 0; i < object_count; ++i)
        {
            // 有效 geometry 句柄：列式 extract 会过滤 invalid，保持与对象路径同口径（4096 可见）
            registry.register_object("obj" + std::to_string(i),
                                     scene::aabb{glm::vec3(-0.5F), glm::vec3(0.5F)},
                                     {}, false, static_cast<engine::geometry_handle>(i + 1));
        }
        return registry;
    }

    struct extract_result
    {
        std::vector<engine::instance_row> instances;
        std::vector<glm::mat4> transforms;
        std::uint64_t visible = 0;
    };

    // Mirrors the column-based extract path: refresh dirty matrices, then walk
    // active_slots reading hot columns (no scene_object pointer chasing).
    extract_result run_extract(scene::scene_registry& registry)
    {
        registry.refresh_matrices();
        extract_result out;
        const auto& slots = registry.slot_indices();
        out.instances.reserve(slots.size());
        out.transforms.reserve(slots.size());
        for (const std::size_t slot : slots)
        {
            if (registry.visible_at(slot) == 0 ||
                registry.geometry_at(slot) == engine::invalid_geometry_handle)
                continue;
            out.transforms.push_back(registry.matrix_at(slot));
            out.instances.push_back(
                {.mesh = registry.geometry_at(slot),
                 .transform = static_cast<std::uint32_t>(out.transforms.size() - 1)});
        }
        out.visible = out.instances.size();
        return out;
    }
} // namespace

int main()
{
    scene::scene_registry registry = synthesize();
    (void)run_extract(registry);  // warmup

    std::vector<std::uint64_t> samples;
    samples.reserve(iterations);
    for (int i = 0; i < iterations; ++i)
    {
        const auto begin = std::chrono::steady_clock::now();
        const auto result = run_extract(registry);
        samples.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count()));
        if (result.visible == 0)
        {
            return 1;  // keep the compiler honest about the result being used
        }
    }

    std::sort(samples.begin(), samples.end());
    std::cout << "[bench] extract objects=" << object_count
              << " best_us=" << samples.front()
              << " median_us=" << samples[iterations / 2] << '\n';
    return 0;
}
