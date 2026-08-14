// Extract-scale micro benchmark (D0 baseline, EngineLayerDoDPlan D0): synthesizes
// N scene objects and measures the extract chain
//   scene_registry.objects() -> scene::model_matrix -> instance/transform rows
// which mirrors engine_runtime::extract_render_packet's per-object path.
// Reports one data line per run — deliberately no pass/fail threshold, not a CI
// gate (计划 D0："仅作前后对比数据，不进 CI 门槛").

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
            registry.register_object("obj" + std::to_string(i),
                                     scene::aabb{glm::vec3(-0.5F), glm::vec3(0.5F)},
                                     {}, false, engine::invalid_geometry_handle);
        }
        return registry;
    }

    struct extract_result
    {
        std::vector<engine::instance_row> instances;
        std::vector<glm::mat4> transforms;
        std::uint64_t visible = 0;
    };

    // Mirrors the per-object extract path (D0 baseline; rewritten to columns in D2).
    extract_result run_extract(const scene::scene_registry& registry)
    {
        extract_result out;
        out.instances.reserve(object_count);
        out.transforms.reserve(object_count);
        for (const scene::scene_object* object : registry.objects())
        {
            out.transforms.push_back(scene::model_matrix(*object));
            out.instances.push_back(
                {.mesh = object->render_geometry,
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
