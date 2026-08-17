#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <string>
#include <vector>
#include <functional>
#include <unordered_set>
#include <algorithm>
#include "utility/logger.h"
#include "builder.h"

struct SVulkanSDLWindowConfig
{
    const char* window_name_;
    int width_;
    int height_;
    SDL_WindowFlags window_flags_;
    SDL_InitFlags init_flags_;
};

class VulkanWindowBuilder : public Builder
{
private:
    typedef struct WindowInfo
    {
        std::string window_name;
        int width;
        int height;
        SDL_WindowFlags window_flags;
        SDL_InitFlags init_flags;
    } WindowInfo;

    WindowInfo window_info_;
    std::vector<std::function<void(SDL_Window*)>> build_callbacks_;

public:
    VulkanWindowBuilder();
    ~VulkanWindowBuilder();

    // Build the SDL window
    bool Build() override;

    // Add callback listener
    void AddListener(std::function<void(SDL_Window*)> callback)
    {
        build_callbacks_.push_back(callback);
    }

    // Window configuration methods
    VulkanWindowBuilder& SetWindowName(const std::string& name)
    {
        window_info_.window_name = name;
        return *this;
    }

    VulkanWindowBuilder& SetWindowSize(int width, int height)
    {
        window_info_.width = width;
        window_info_.height = height;
        return *this;
    }

    VulkanWindowBuilder& SetWindowFlags(SDL_WindowFlags flags)
    {
        window_info_.window_flags = flags;
        return *this;
    }

    VulkanWindowBuilder& SetInitFlags(SDL_InitFlags flags)
    {
        window_info_.init_flags = flags;
        return *this;
    }
};

class VulkanSDLWindowHelper
{
public:
    VulkanSDLWindowHelper() = default;
    ~VulkanSDLWindowHelper();

    bool CreateSurface(VkInstance vkInstance);
    VkSurfaceKHR GetSurface() const { return surface_; }
    SDL_Window* GetWindow() const { return window_; }
    VkExtent2D GetCurrentWindowExtent() const;
    
    // return constant reference to extensions vector
    const std::vector<const char*>& GetWindowExtensions() const { return extensions_; }
    
    VulkanWindowBuilder& GetWindowBuilder() 
    {
        windowBuilder_.AddListener([this](SDL_Window* window) {
            window_ = window;
            // get SDL provided Vulkan instance extensions
            window_extension_names_ = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count_);
            if (!window_extension_names_) {
                Logger::LogWarning("SDL_Vulkan_GetInstanceExtensions failed: " + std::string(SDL_GetError()));
                sdl_extension_count_ = 0;
            }

            // generate window extensions vector
            extensions_.clear();
            extensions_.reserve(sdl_extension_count_ + 1);

            // debug utility extension
            auto debug_ext = SDL_strdup(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            if (debug_ext) extensions_.push_back(debug_ext);

            // add sdl required extensions
            for (unsigned int i = 0; i < sdl_extension_count_; ++i) {
                auto ext_copy = SDL_strdup(window_extension_names_[i]);
                if (ext_copy) extensions_.push_back(ext_copy);
            }
        });
        return windowBuilder_;
    }

private:
    // sdl
    SDL_Window* window_ = nullptr;
    unsigned int sdl_extension_count_ = 0;
    const char* const* window_extension_names_ = nullptr;
    std::vector<const char*> extensions_;

    // vulkan handle
    VkInstance vk_instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    VulkanWindowBuilder windowBuilder_;
};

struct SVulkanSwapChainConfig
{
    VkSurfaceFormatKHR target_surface_format_;
    VkPresentModeKHR target_present_mode_;
    VkExtent2D target_swap_extent_;
    uint32_t target_image_count_;
    std::vector<const char*> device_extensions_;

    // Default constructor
    SVulkanSwapChainConfig() 
    {
        target_surface_format_.format = VK_FORMAT_B8G8R8A8_UNORM;
        target_surface_format_.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        target_present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
        target_swap_extent_.width = 800;
        target_swap_extent_.height = 600;
        target_image_count_ = 2; // Double buffering
        device_extensions_.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
};


// --------------------------------------

class VulkanSwapChainHelper
{
public:
    VulkanSwapChainHelper();
    ~VulkanSwapChainHelper();

    void Setup(SVulkanSwapChainConfig config, VkDevice device, VkPhysicalDevice physical_device, VkSurfaceKHR surface, SDL_Window* window);
    bool CreateSwapChain();
    int GetSwapChainExtensions(VkPhysicalDevice physical_device, std::vector<const char*>& extensions) 
    {
        physical_device_ = physical_device;
        CheckExtensions();
        extensions = swap_chain_config_.device_extensions_; 
        return swap_chain_config_.device_extensions_.size(); 
    }

    const VkSwapchainKHR GetSwapChain() const { return swap_chain_; }
    const std::vector<VkImage>* GetSwapChainImages() const { return &swap_chain_images_; }
    const std::vector<VkImageView>* GetSwapChainImageViews() const { return &swap_chain_image_views_; }
    const SVulkanSwapChainConfig* GetSwapChainConfig() const { return &swap_chain_config_; }

    bool AcquireNextImage(uint32_t& image_index, VkSemaphore semaphore = VK_NULL_HANDLE, VkFence fence = VK_NULL_HANDLE);
    bool AcquireNextImage(uint32_t& image_index, bool& resize_request, VkSemaphore semaphore = VK_NULL_HANDLE, VkFence fence = VK_NULL_HANDLE);
    bool DestroySwapChain();

private:
    VkSwapchainKHR swap_chain_;
    SVulkanSwapChainConfig swap_chain_config_;

    // swapchain supported properties
    std::vector<VkSurfaceFormatKHR> surface_formats_;
    std::vector<VkPresentModeKHR> present_modes_;
    VkSurfaceCapabilitiesKHR surface_capabilities_;

    // swapchain image
    std::vector<VkImage> swap_chain_images_;
    std::vector<VkImageView> swap_chain_image_views_;

    VkDevice device_;
    VkPhysicalDevice physical_device_;
    VkSurfaceKHR surface_;
    SDL_Window* window_;

    void CheckExtensions();
    void GetSwapChainSupport();
    bool CheckSurfaceFormat();
    bool CheckPresentMode();
    bool CheckSwapExtent();
    void CreateSwapChainInternal();
    void CreateImageViews();
};