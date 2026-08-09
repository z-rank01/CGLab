#include "renderer/vulkan/create_vulkan_renderer.h"

#include "renderer/vulkan/vulkan_renderer.h"

namespace engine::vulkan
{
    std::unique_ptr<render_backend> create_renderer(render_program program)
    {
        return std::make_unique<vulkan_sample>(std::move(program));
    }
}
