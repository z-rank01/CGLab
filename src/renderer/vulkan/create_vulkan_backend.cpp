#include "renderer/vulkan/create_vulkan_backend.h"

#include "renderer/vulkan/vulkan_backend.h"

namespace engine::vulkan
{
    std::unique_ptr<render_backend> create_backend(render_program program)
    {
        return std::make_unique<vulkan_backend>(std::move(program));
    }
}
