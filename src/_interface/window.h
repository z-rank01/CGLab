#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include "input.h"


namespace interface
{

    struct window_config
    {
        std::string title;
        int width;
        int height;
        bool resizable = true;
    };

    class window
    {
    public:
        virtual ~window() = default;

        virtual bool open(const window_config& config) = 0;

        virtual void close() = 0;

        virtual void tick(input_event& e) = 0;

        // Poll every pending platform event into reusable caller-owned storage.
        virtual void poll_events(std::vector<input_event>& events) = 0;

        virtual bool should_close() const = 0;

        // Vulkan integration

        virtual std::vector<const char*> get_required_instance_extensions() const = 0;

        virtual bool create_vulkan_surface(VkInstance instance, VkSurfaceKHR* surface) const = 0;

        // Window properties

        virtual void get_extent(int& width, int& height) const = 0;

        virtual float get_aspect_ratio() const = 0;
    };

} // namespace interface
