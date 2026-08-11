#include "platform/vulkan/sdl_vulkan_surface_adapter.h"

#include <SDL3/SDL_vulkan.h>

#include "_interface/window.h"

namespace platform::vulkan
{
    namespace
    {
        bool instance_extensions(void*, const char* const*& names, uint32_t& count, std::string& error)
        {
            names = SDL_Vulkan_GetInstanceExtensions(&count);
            if (names == nullptr || count == 0)
            {
                error = "SDL_Vulkan_GetInstanceExtensions returned no extensions";
                return false;
            }
            return true;
        }

        bool create_surface(void* state, VkInstance instance, VkSurfaceKHR& surface, std::string& error)
        {
            auto* render_window = static_cast<interface::window*>(state);
            const interface::native_window_handle native = render_window->native_handle();
            if (native.kind != interface::native_window_kind::sdl3 || native.value == nullptr)
            {
                error = "SDL Vulkan surface provider received a non-SDL window";
                return false;
            }
            if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(native.value), instance, nullptr, &surface))
            {
                error = SDL_GetError();
                return false;
            }
            return true;
        }

        VkExtent2D drawable_extent(void* state)
        {
            auto* render_window = static_cast<interface::window*>(state);
            int width = 0;
            int height = 0;
            render_window->get_extent(width, height);
            return {
                .width = width > 0 ? static_cast<uint32_t>(width) : 0,
                .height = height > 0 ? static_cast<uint32_t>(height) : 0,
            };
        }
    } // namespace

    render_graph::vk_surface_provider make_sdl_surface_provider(interface::window& window)
    {
        return {
            .state = &window,
            .instance_extensions = &instance_extensions,
            .create_surface = &create_surface,
            .drawable_extent = &drawable_extent,
        };
    }
} // namespace platform::vulkan
