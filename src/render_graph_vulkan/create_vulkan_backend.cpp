#include "render_graph_vulkan/create_vulkan_backend.h"

#include "render_graph_vulkan/vulkan_backend.h"

namespace engine::vulkan
{
    namespace
    {
        vulkan_backend& backend(void* state) { return *static_cast<vulkan_backend*>(state); }
        const vulkan_backend& backend(const void* state) { return *static_cast<const vulkan_backend*>(state); }

        const render_driver_api api{
            .initialize = [](void* state, interface::window& window, const backend_config& config)
            { return backend(state).initialize(window, config); },
            .apply_resource_changes = [](void* state, resource_change_batch batch)
            {
                result<resource_change_result> output;
                for (const auto& row : batch.material_uploads)
                {
                    const auto uploaded = backend(state).upload_materials(*row.asset);
                    if (!uploaded) return result<resource_change_result>{.error = uploaded.error};
                    output.value.material_bases.push_back(uploaded.value);
                }
                for (const auto& row : batch.geometry_uploads)
                {
                    const auto uploaded = backend(state).upload_geometry(*row.asset);
                    if (!uploaded) return result<resource_change_result>{.error = uploaded.error};
                    output.value.geometry_handles.push_back(uploaded.value);
                }
                for (const auto& row : batch.geometry_retires) backend(state).retire_geometry(row.handle);
                return output;
            },
            .render = [](void* state, const render_frame_packet& packet)
            { return backend(state).render(packet); },
            .request_resize = [](void* state) noexcept { backend(state).request_resize(); },
            .shutdown = [](void* state) noexcept { backend(state).shutdown(); },
            .statistics = [](const void* state) noexcept { return backend(state).statistics(); },
            .validation_error_count = [](const void* state) noexcept { return backend(state).validation_error_count(); },
            .destroy = [](void* state) noexcept { delete static_cast<vulkan_backend*>(state); },
        };
    }

    render_driver create_backend(render_program program)
    {
        return {new vulkan_backend(std::move(program)), &api};
    }
}
