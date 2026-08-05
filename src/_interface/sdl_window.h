#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "window.h"

namespace interface
{

    class sdl_window : public window
    {
    public:
        sdl_window();

        ~sdl_window() override;

        bool open(const window_config& config) override;

        void close() override;

        void tick(input_event& e) override;

        void poll_events(std::vector<input_event>& events) override;

        bool should_close() const override;

        // Vulkan integration implementation

        std::vector<const char*> get_required_instance_extensions() const override;

        bool create_vulkan_surface(VkInstance instance, VkSurfaceKHR* surface) const override;

        // Window properties implementation

        void get_extent(int& width, int& height) const override;

        float get_aspect_ratio() const override;

    private:
        SDL_Window* window = nullptr;
        bool should_close_internal  = false;

        static key_code translate_key_code(SDL_Keycode key);

        static mouse_button translate_mouse_button(uint8_t button);

        bool translate_event(const SDL_Event& source, input_event& destination);
    };

} // namespace interface
