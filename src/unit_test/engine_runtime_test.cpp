#include "engine/engine_runtime.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace
{
    void check(bool condition, const char* expression, int line)
    {
        if (!condition)
        {
            std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

#define CHECK(expression) check((expression), #expression, __LINE__)

    class fake_window final : public interface::window
    {
    public:
        bool open(const interface::window_config& requested) override
        {
            width = requested.width;
            height = requested.height;
            opened = true;
            return true;
        }
        void close() override { closed = true; }
        void tick(interface::input_event& event) override { event = {}; }
        void poll_events(std::vector<interface::input_event>& events) override { events.clear(); }
        bool should_close() const override { return false; }
        interface::native_window_handle native_handle() const noexcept override { return {}; }
        void get_extent(int& out_width, int& out_height) const override
        {
            out_width = width;
            out_height = height;
        }
        float get_aspect_ratio() const override { return static_cast<float>(width) / static_cast<float>(height); }

        bool opened = false;
        bool closed = false;
        int width = 1;
        int height = 1;
    };

    struct backend_state
    {
        int initialize_calls = 0;
        int upload_calls = 0;
        int render_calls = 0;
        int shutdown_calls = 0;
        std::size_t last_object_count = 0;
        std::vector<std::size_t> object_counts;
        engine::frame_status status = engine::frame_status::rendered;
    };

    struct fake_backend
    {
        explicit fake_backend(std::shared_ptr<backend_state> state) : state(std::move(state)) {}
        std::shared_ptr<backend_state> state;
    };

    engine::render_driver make_fake_driver(std::shared_ptr<backend_state> state)
    {
        static const engine::render_driver_api api{
            .initialize = [](void* value, interface::window&, const engine::backend_config&)
            {
                ++static_cast<fake_backend*>(value)->state->initialize_calls;
                return engine::result<bool>{.value = true};
            },
            .apply_resource_changes = [](void* value, engine::resource_change_batch batch)
            {
                engine::resource_change_result changed;
                for ([[maybe_unused]] const auto& row : batch.material_uploads) changed.material_bases.push_back(0);
                for ([[maybe_unused]] const auto& row : batch.geometry_uploads)
                {
                    ++static_cast<fake_backend*>(value)->state->upload_calls;
                    changed.geometry_handles.push_back(7);
                }
                return engine::result<engine::resource_change_result>{.value = std::move(changed)};
            },
            .render = [](void* value, const engine::render_frame_packet& packet)
            {
                auto& state = *static_cast<fake_backend*>(value)->state;
                ++state.render_calls;
                state.last_object_count = packet.instance_rows.size();
                state.object_counts.push_back(packet.instance_rows.size());
                return state.status;
            },
            .request_resize = [](void*) noexcept {},
            .shutdown = [](void* value) noexcept { ++static_cast<fake_backend*>(value)->state->shutdown_calls; },
            .statistics = [](const void* value) noexcept
            {
                const auto& state = *static_cast<const fake_backend*>(value)->state;
                return engine::render_statistics{.draw_pass_executions = static_cast<std::uint64_t>(state.render_calls),
                                                 .presented_frames = static_cast<std::uint64_t>(state.render_calls)};
            },
            .validation_error_count = [](const void*) noexcept { return 0u; },
            .destroy = [](void* value) noexcept { delete static_cast<fake_backend*>(value); },
        };
        return {new fake_backend(std::move(state)), &api};
    }

    engine::geometry_asset triangle();

    struct asset_state
    {
        int start_calls = 0;
        int request_calls = 0;
        int shutdown_calls = 0;
        std::vector<engine::completed_asset_request> completed;
    };

    class fake_asset_service final : public engine::asset_service
    {
    public:
        explicit fake_asset_service(std::shared_ptr<asset_state> state) : state(std::move(state)) {}

        void start(std::filesystem::path) override { ++state->start_calls; }
        engine::result<engine::asset_request_id> request(std::filesystem::path) override
        {
            ++state->request_calls;
            engine::geometry_asset geometry = triangle();
            engine::asset_database asset{.name = geometry.name};
            asset.vertex_blob = geometry.primitives[0].vertices;
            asset.index_blob = geometry.primitives[0].indices;
            asset.primitives.push_back({.mesh = 0, .material = 0, .vertex_count = 3, .index_count = 3});
            asset.meshes.push_back({.name = geometry.name, .primitive_count = 1,
                                    .bounds_min = geometry.bounds_min, .bounds_max = geometry.bounds_max});
            asset.nodes.push_back({.name = geometry.name, .mesh = 0});
            asset.materials.emplace_back();
            state->completed.push_back({.id = 1, .result = {.value = std::move(asset)}});
            return {.value = 1};
        }
        std::vector<engine::completed_asset_request> drain_completed() override
        {
            return std::exchange(state->completed, {});
        }
        void shutdown() noexcept override { ++state->shutdown_calls; }

    private:
        std::shared_ptr<asset_state> state;
    };

    engine::geometry_asset triangle()
    {
        engine::geometry_asset asset;
        asset.name = "test triangle";
        asset.bounds_min = {-1.0F, -1.0F, 0.0F};
        asset.bounds_max = {1.0F, 1.0F, 0.0F};
        engine::geometry_primitive primitive;
        primitive.indices = {0, 1, 2};
        primitive.vertices.resize(3);
        asset.primitives.push_back(std::move(primitive));
        return asset;
    }
}

int main()
{
    auto state = std::make_shared<backend_state>();
    engine::runtime_config config{
        .window = {.title = "runtime test", .width = 640, .height = 480},
        .working_directory = ".",
    };
    engine::engine_runtime runtime(
        config,
        make_fake_driver(state),
        std::make_unique<fake_window>());
    runtime.set_initial_geometry(triangle());
    runtime.initialize();
    CHECK(runtime.tick(2));
    CHECK(state->initialize_calls == 1);
    CHECK(state->upload_calls == 1);
    CHECK(state->render_calls == 2);
    CHECK(state->last_object_count == 1);
    runtime.shutdown();
    runtime.shutdown();
    CHECK(state->shutdown_calls == 1);

    auto failing_state = std::make_shared<backend_state>();
    failing_state->status = engine::frame_status::failed;
    engine::engine_runtime failing(
        config,
        make_fake_driver(failing_state),
        std::make_unique<fake_window>());
    failing.initialize();
    CHECK(!failing.tick(1));
    failing.shutdown();

    auto boundary_backend_state = std::make_shared<backend_state>();
    auto boundary_asset_state = std::make_shared<asset_state>();
    engine::engine_runtime boundary_runtime(
        config,
        make_fake_driver(boundary_backend_state),
        std::make_unique<fake_window>(),
        std::make_unique<fake_asset_service>(boundary_asset_state));
    bool requested = false;
    boundary_runtime.configure_sample(engine::sample{
        .name = "asset boundary test",
        .update = [&requested](engine::runtime_services& services, float)
        {
            if (!requested)
            {
                CHECK(static_cast<bool>(services.request_asset("triangle.gltf")));
                requested = true;
            }
        },
    });
    boundary_runtime.initialize();
    CHECK(boundary_runtime.tick(2));
    boundary_runtime.shutdown();
    CHECK(boundary_asset_state->start_calls == 1);
    CHECK(boundary_asset_state->request_calls == 1);
    CHECK(boundary_asset_state->shutdown_calls == 1);
    CHECK(boundary_backend_state->upload_calls == 1);
    CHECK(boundary_backend_state->object_counts.size() == 2);
    CHECK(boundary_backend_state->object_counts[0] == 0);
    CHECK(boundary_backend_state->object_counts[1] == 1);
    return EXIT_SUCCESS;
}
