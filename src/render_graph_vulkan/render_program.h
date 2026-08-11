#pragma once

#include <string>

#include <glm/glm.hpp>

namespace engine::vulkan
{
    // Vulkan renderer extension point for the current single-raster-pass
    // samples. It is deliberately narrower than an RHI.
    struct render_program
    {
        std::string pass_name = "DrawPass";
        glm::vec4 clear_color{0.1F, 0.1F, 0.1F, 1.0F};
    };
}
