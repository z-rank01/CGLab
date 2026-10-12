#pragma once

#include <SDL3/SDL.h>
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

        [[nodiscard]] native_window_handle native_handle() const noexcept override;

        // Window properties implementation

        void get_extent(int& width, int& height) const override;

        float get_aspect_ratio() const override;

        // A3-1：事件驱动的最小化状态（MINIMIZED → true，RESTORED/MAXIMIZED/SHOWN → false），
        // 比每帧查 SDL_WindowFlags 更早生效——最小化事件在本帧 poll 内到达。
        [[nodiscard]] bool is_minimized() const override { return minimized_internal; }

    private:
        SDL_Window* window = nullptr;
        bool should_close_internal  = false;
        bool minimized_internal     = false;

        static key_code translate_key_code(SDL_Keycode key);

        static mouse_button translate_mouse_button(uint8_t button);

        bool translate_event(const SDL_Event& source, input_event& destination);
    };

} // namespace interface
