#include "sdl_window.h"

#include <iostream>


namespace interface
{

    sdl_window::sdl_window() = default;

    sdl_window::~sdl_window()
    {
        sdl_window::close();
    }

    bool sdl_window::open(const window_config& config)
    {
        if (static_cast<int>(SDL_Init(SDL_INIT_VIDEO)) < 0)
        {
            std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << '\n';
            return false;
        }

        auto window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        window = SDL_CreateWindow(config.title.c_str(), config.width, config.height, window_flags);

        if (window == nullptr)
        {
            std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << '\n';
            return false;
        }

        return true;
    }

    void sdl_window::close()
    {
        if (window != nullptr)
        {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        SDL_Quit();
    }

    void sdl_window::tick(input_event& e)
    {
        e = input_event{};
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (translate_event(event, e))
            {
                return;
            }
        }
    }

    void sdl_window::poll_events(std::vector<input_event>& events)
    {
        events.clear();
        SDL_Event source{};
        while (SDL_PollEvent(&source))
        {
            input_event destination{};
            if (translate_event(source, destination))
            {
                events.push_back(destination);
            }
            if (should_close_internal)
            {
                return;
            }
        }
    }

    bool sdl_window::translate_event(const SDL_Event& source, input_event& destination)
    {
        destination = input_event{};
        switch (source.type)
        {
        case SDL_EVENT_QUIT:
            destination.type = event_type::quit;
            should_close_internal = true;
            return true;
        case SDL_EVENT_WINDOW_RESIZED:
            destination.type = event_type::resize;
            destination.resize.width  = source.window.data1;
            destination.resize.height = source.window.data2;
            return true;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            destination.type = event_type::focus_lost;
            return true;
        case SDL_EVENT_KEY_DOWN:
            destination.type = event_type::key_down;
            destination.key.key = translate_key_code(source.key.key);
            return true;
        case SDL_EVENT_KEY_UP:
            destination.type = event_type::key_up;
            destination.key.key = translate_key_code(source.key.key);
            return true;
        case SDL_EVENT_MOUSE_MOTION:
            destination.type = event_type::mouse_move;
            destination.mouse_move.x    = source.motion.x;
            destination.mouse_move.y    = source.motion.y;
            destination.mouse_move.xrel = source.motion.xrel;
            destination.mouse_move.yrel = source.motion.yrel;
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            destination.type = event_type::mouse_wheel;
            destination.mouse_wheel.x = source.wheel.x;
            destination.mouse_wheel.y = source.wheel.y;
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            destination.type = event_type::mouse_button_down;
            destination.mouse_button.button  = translate_mouse_button(source.button.button);
            destination.mouse_button.x       = source.button.x;
            destination.mouse_button.y       = source.button.y;
            destination.mouse_button.pressed = true;
            return true;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            destination.type = event_type::mouse_button_up;
            destination.mouse_button.button  = translate_mouse_button(source.button.button);
            destination.mouse_button.x       = source.button.x;
            destination.mouse_button.y       = source.button.y;
            destination.mouse_button.pressed = false;
            return true;
        default:
            return false;
        }
    }

    bool sdl_window::should_close() const
    {
        return should_close_internal;
    }

    std::vector<const char*> sdl_window::get_required_instance_extensions() const
    {
        uint32_t count           = 0;
        const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
        std::vector<const char*> extensions(names, names + count);
        return extensions;
    }

    bool sdl_window::create_vulkan_surface(VkInstance instance, VkSurfaceKHR* surface) const
    {
        return SDL_Vulkan_CreateSurface(window, instance, nullptr, surface);
    }

    void sdl_window::get_extent(int& width, int& height) const
    {
        SDL_GetWindowSize(window, &width, &height);
    }

    float sdl_window::get_aspect_ratio() const
    {
        int width = 0;
        int height = 0;
        get_extent(width, height);
        return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0F;
    }

    key_code sdl_window::translate_key_code(SDL_Keycode key)
    {
        switch (key)
        {
        case SDLK_W:
            return key_code::w;
        case SDLK_A:
            return key_code::a;
        case SDLK_S:
            return key_code::s;
        case SDLK_D:
            return key_code::d;
        case SDLK_Q:
            return key_code::q;
        case SDLK_E:
            return key_code::e;
        case SDLK_F:
            return key_code::f;
        case SDLK_SPACE:
            return key_code::space;
        case SDLK_LSHIFT:
            return key_code::lshift;
        case SDLK_LCTRL:
            return key_code::lctrl;
        case SDLK_ESCAPE:
            return key_code::escape;
        case SDLK_UP:
            return key_code::up;
        case SDLK_DOWN:
            return key_code::down;
        case SDLK_LEFT:
            return key_code::left;
        case SDLK_RIGHT:
            return key_code::right;
        default:
            return key_code::unknown;
        }
    }

    mouse_button sdl_window::translate_mouse_button(uint8_t button)
    {
        switch (button)
        {
        case SDL_BUTTON_LEFT:
            return mouse_button::left;
        case SDL_BUTTON_MIDDLE:
            return mouse_button::middle;
        case SDL_BUTTON_RIGHT:
            return mouse_button::right;
        default:
            return mouse_button::unknown;
        }
    }

} // namespace interface
