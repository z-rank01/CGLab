#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "input.h"


namespace interface
{

    enum class native_window_kind : std::uint8_t
    {
        unknown,
        sdl3,
    };

    struct native_window_handle
    {
        native_window_kind kind = native_window_kind::unknown;
        void* value = nullptr;
    };

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

        // Backend-specific integrations consume this opaque, tagged handle.
        [[nodiscard]] virtual native_window_handle native_handle() const noexcept = 0;

        // Window properties

        virtual void get_extent(int& width, int& height) const = 0;

        virtual float get_aspect_ratio() const = 0;
    };

} // namespace interface
