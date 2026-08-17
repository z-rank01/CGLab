#ifndef COMMON_H
#define COMMON_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "_callable/callable.h"

namespace templates::common
{

    /// -------------------------------------------
    /// vulkan instance templated functions: common
    /// -------------------------------------------

    /// @brief Vulkan instance context with lazy evaluation support
    struct CommVkInstanceContext
    {
        // application info
        struct ApplicationInfo
        {
            std::string application_name_ = "Vulkan Engine";
            std::string engine_name_      = "Vulkan Engine";
            uint32_t application_version_ = VK_MAKE_VERSION(1, 0, 0);
            uint32_t engine_version_      = VK_MAKE_VERSION(1, 0, 0);
            uint32_t highest_api_version_ = VK_API_VERSION_1_3;
            void* p_next_                 = nullptr;
        } app_info_;

        // instance info
        struct InstanceInfo
        {
            ApplicationInfo app_info_;
            std::vector<const char*> required_layers_;
            std::vector<const char*> required_extensions_;
        } instance_info_;

        // vulkan natives
        vk::Instance vk_instance_ = VK_NULL_HANDLE;
    };

    /// @brief Instance functions using Monad-Like Chain
    namespace instance
    {

        /// @brief Creates initial context
        inline auto create_context()
        {
            return callable::make_chain(CommVkInstanceContext{});
        }

        /// @brief Sets application name
        inline auto set_application_name(const std::string& name)
        {
            return [name](CommVkInstanceContext ctx) -> callable::Chainable<CommVkInstanceContext>
            {
                ctx.app_info_.application_name_ = name;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets engine name
        inline auto set_engine_name(const std::string& name)
        {
            return [name](CommVkInstanceContext ctx) -> callable::Chainable<CommVkInstanceContext>
            {
                ctx.app_info_.engine_name_ = name;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets application version
        inline auto set_application_version(uint32_t major, uint32_t minor, uint32_t patch)
        {
            return [major, minor, patch](CommVkInstanceContext ctx) -> callable::Chainable<CommVkInstanceContext>
            {
                ctx.app_info_.application_version_ = VK_MAKE_VERSION(major, minor, patch);
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets engine version
        inline auto set_engine_version(uint32_t major, uint32_t minor, uint32_t patch)
        {
            return [major, minor, patch](CommVkInstanceContext ctx) -> callable::Chainable<CommVkInstanceContext>
            {
                ctx.app_info_.engine_version_ = VK_MAKE_VERSION(major, minor, patch);
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets API version
        inline auto set_api_version(uint32_t major, uint32_t minor, uint32_t patch)
        {
            return [major, minor, patch](CommVkInstanceContext ctx) -> callable::Chainable<CommVkInstanceContext>
            {
                ctx.app_info_.highest_api_version_ = VK_MAKE_VERSION(major, minor, patch);
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Adds validation layers
        inline auto add_validation_layers(const std::vector<const char*>& layers)
        {
            return [layers](CommVkInstanceContext ctx) -> callable::Chainable<CommVkInstanceContext>
            {
                ctx.instance_info_.required_layers_.insert(ctx.instance_info_.required_layers_.end(), layers.begin(), layers.end());
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Adds extensions
        inline auto add_extensions(const std::vector<const char*>& extensions)
        {
            return [extensions](CommVkInstanceContext ctx) -> callable::Chainable<CommVkInstanceContext>
            {
                ctx.instance_info_.required_extensions_.insert(ctx.instance_info_.required_extensions_.end(), extensions.begin(), extensions.end());
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Creates the Vulkan instance (final step)
        inline auto create_vk_instance()
        {
            return [](CommVkInstanceContext ctx) -> callable::Chainable<CommVkInstanceContext>
            {
                vk::ApplicationInfo app_info;
                app_info.setPApplicationName(ctx.app_info_.application_name_.c_str())
                    .setApplicationVersion(ctx.app_info_.application_version_)
                    .setPEngineName(ctx.app_info_.engine_name_.c_str())
                    .setEngineVersion(ctx.app_info_.engine_version_)
                    .setApiVersion(ctx.app_info_.highest_api_version_);

                vk::InstanceCreateInfo create_info;
                create_info.setPApplicationInfo(&app_info)
                    .setPEnabledLayerNames(ctx.instance_info_.required_layers_)
                    .setPEnabledExtensionNames(ctx.instance_info_.required_extensions_);

                ctx.vk_instance_ = vk::createInstance(create_info, nullptr);
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Validates context before creation
        inline auto validate_context()
        {
            return [](CommVkInstanceContext ctx) -> callable::Chainable<CommVkInstanceContext>
            {
                if (ctx.app_info_.application_name_.empty())
                {
                    return callable::Chainable<CommVkInstanceContext>(callable::error<CommVkInstanceContext>("Application name cannot be empty"));
                }
                if (ctx.app_info_.engine_name_.empty())
                {
                    return callable::Chainable<CommVkInstanceContext>(callable::error<CommVkInstanceContext>("Engine name cannot be empty"));
                }
                return callable::make_chain(std::move(ctx));
            };
        }

    } // namespace instance

    /// --------------------------------------------------
    /// vulkan physical device templated functions: common
    /// --------------------------------------------------

    /// @brief Vulkan physical device context with lazy evaluation support
    struct CommVkPhysicalDeviceContext
    {
        // parent instance context
        vk::Instance vk_instance_ = VK_NULL_HANDLE;

        // physical device selection criteria
        struct SelectionCriteria
        {
            std::optional<vk::PhysicalDeviceType> preferred_device_type_ = std::nullopt;
            std::optional<uint32_t> minimum_api_version_                 = std::nullopt;
            vk::SurfaceKHR surface_                                      = VK_NULL_HANDLE;

            // required features
            vk::PhysicalDeviceFeatures required_features_{};
            vk::PhysicalDeviceVulkan11Features required_features_11_;
            vk::PhysicalDeviceVulkan12Features required_features_12_;
            vk::PhysicalDeviceVulkan13Features required_features_13_;

            // required extensions
            std::vector<const char*> required_extensions_;

            // queue requirements
            struct QueueRequirement
            {
                vk::QueueFlags queue_flags_;
                uint32_t min_queue_count_     = 1;
                bool require_present_support_ = false;
            };
            std::vector<QueueRequirement> queue_requirements_;

            // memory requirements
            std::optional<vk::DeviceSize> minimum_device_memory_ = std::nullopt;
            std::optional<vk::DeviceSize> minimum_host_memory_   = std::nullopt;

            // scoring preferences
            bool prefer_discrete_gpu_             = true;
            bool prefer_dedicated_graphics_queue_ = true;
        } selection_criteria_;

        // vulkan natives
        vk::PhysicalDevice vk_physical_device_ = VK_NULL_HANDLE;
        vk::PhysicalDeviceProperties device_properties_{};
        vk::PhysicalDeviceFeatures device_features_{};
        vk::PhysicalDeviceMemoryProperties memory_properties_{};
        std::vector<vk::QueueFamilyProperties> queue_family_properties_;
        std::vector<vk::ExtensionProperties> available_extensions_;

        // swapchain support info (added for swapchain creation)
        vk::SurfaceCapabilitiesKHR surface_capabilities_{};
        std::vector<vk::SurfaceFormatKHR> surface_formats_;
        std::vector<vk::PresentModeKHR> present_modes_;
        bool swapchain_support_queried_ = false;
    };

    namespace physicaldevice
    {

        /// @brief Creates initial physical device context from instance context
        /// @param instance Vulkan instance handle
        /// @return CommVkPhysicalDeviceContext with instance set
        inline auto create_physical_device_context(vk::Instance instance)
        {
            return callable::make_chain(
                [instance]() -> CommVkPhysicalDeviceContext
                {
                    CommVkPhysicalDeviceContext ctx;
                    ctx.vk_instance_ = instance;
                    return ctx;
                }());
        }

        /// @brief Sets surface for present support checking
        /// @param surface Vulkan surface handle
        /// @return Callable that has set the surface in the context
        inline auto set_surface(vk::SurfaceKHR surface)
        {
            return [surface](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                ctx.selection_criteria_.surface_ = surface;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets minimum API version requirement
        /// @param major Major version number
        /// @param minor Minor version number
        /// @param patch Patch version number (default is 0)
        /// @return Callable that has set the minimum API version in the context
        inline auto require_api_version(uint32_t major, uint32_t minor, uint32_t patch = 0)
        {
            return [major, minor, patch](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                ctx.selection_criteria_.minimum_api_version_ = VK_MAKE_VERSION(major, minor, patch);
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Adds required device extensions
        /// @param extensions List of extension names to require
        /// @return Callable that has added the required extensions to the context
        inline auto require_extensions(const std::vector<const char*>& extensions)
        {
            return [extensions](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                ctx.selection_criteria_.required_extensions_.insert(
                    ctx.selection_criteria_.required_extensions_.end(), extensions.begin(), extensions.end());
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets required Vulkan 1.0 features
        /// @param features Vulkan physical device features to require
        /// @return Callable that has set the required features in the context
        inline auto require_features(const vk::PhysicalDeviceFeatures& features)
        {
            return [features](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                ctx.selection_criteria_.required_features_ = features;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets required Vulkan 1.1 features
        /// @param features Vulkan 1.1 physical device features to require
        /// @return Callable that has set the required features in the context
        inline auto require_features_11(const vk::PhysicalDeviceVulkan11Features& features)
        {
            return [features](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                ctx.selection_criteria_.required_features_11_ = features;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets required Vulkan 1.2 features
        /// @param features Vulkan 1.2 physical device features to require
        /// @return Callable that has set the required features in the context
        inline auto require_features_12(const vk::PhysicalDeviceVulkan12Features& features)
        {
            return [features](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                ctx.selection_criteria_.required_features_12_ = features;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets required Vulkan 1.3 features
        /// @param features Vulkan 1.3 physical device features to require
        /// @return Callable that has set the required features in the context
        inline auto require_features_13(const vk::PhysicalDeviceVulkan13Features& features)
        {
            return [features](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                ctx.selection_criteria_.required_features_13_ = features;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Adds queue family requirement
        /// @param queue_flags Vulkan queue flags to require (e.g., VK_QUEUE_GRAPHICS_BIT)
        /// @param min_count Minimum number of queues required (default is 1)
        /// @param require_present Whether the queue must support present (default is true)
        /// @return Callable that has added the queue requirement to the context
        inline auto require_queue(vk::QueueFlags queue_flags, uint32_t min_count = 1, bool require_present = true)
        {
            return [queue_flags, min_count, require_present](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                CommVkPhysicalDeviceContext::SelectionCriteria::QueueRequirement req;
                req.queue_flags_             = queue_flags;
                req.min_queue_count_         = min_count;
                req.require_present_support_ = require_present;
                ctx.selection_criteria_.queue_requirements_.push_back(req);
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets minimum device memory requirement
        /// @param min_memory Minimum device memory size in bytes
        /// @return Callable that has set the minimum device memory in the context
        inline auto require_minimum_device_memory(vk::DeviceSize min_memory)
        {
            return [min_memory](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                ctx.selection_criteria_.minimum_device_memory_ = min_memory;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Enables discrete GPU preference
        /// @param prefer Whether to prefer discrete GPUs (default is true)
        /// @return Callable that has set the discrete GPU preference in the context
        inline auto prefer_discrete_gpu(bool prefer = true)
        {
            return [prefer](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                ctx.selection_criteria_.prefer_discrete_gpu_ = prefer;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Validates device meets all requirements
        /// @return Callable that checks if the selected physical device meets all requirements
        inline auto validate_device_requirements()
        {
            return [](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                if (ctx.vk_physical_device_ == VK_NULL_HANDLE)
                {
                    return callable::Chainable<CommVkPhysicalDeviceContext>(
                        callable::error<CommVkPhysicalDeviceContext>("No physical device selected"));
                }

                // Check API version
                if (ctx.selection_criteria_.minimum_api_version_.has_value())
                {
                    if (ctx.device_properties_.apiVersion < ctx.selection_criteria_.minimum_api_version_.value())
                    {
                        return callable::Chainable<CommVkPhysicalDeviceContext>(
                            callable::error<CommVkPhysicalDeviceContext>("Device API version insufficient"));
                    }
                }

                // Check required extensions
                const auto kMissingExtensions = std::ranges::find_if(
                    ctx.selection_criteria_.required_extensions_,
                    [&ctx](const char* required_ext)
                    {
                        return !std::ranges::any_of(ctx.available_extensions_,
                                                    [required_ext](const auto& available_ext)
                                                    { return std::string_view(required_ext) == std::string_view(available_ext.extensionName); });
                    });

                if (kMissingExtensions != std::end(ctx.selection_criteria_.required_extensions_))
                {
                    return callable::Chainable<CommVkPhysicalDeviceContext>(
                        callable::error<CommVkPhysicalDeviceContext>(std::string("Required extension not available: ") + *kMissingExtensions));
                }

                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Selects the best physical device (final step)
        inline auto select_physical_device()
        {
            return [](CommVkPhysicalDeviceContext ctx) -> callable::Chainable<CommVkPhysicalDeviceContext>
            {
                // Enumerate physical devices
                auto physical_device = ctx.vk_instance_.enumeratePhysicalDevices();
                std::ranges::for_each(physical_device,
                                      [](const auto& device)
                                      {
                                          // Just print device names for debugging
                                          std::cout << "Found device: " << device.getProperties().deviceName << '\n';
                                      });

                if (physical_device.empty())
                {
                    return callable::Chainable<CommVkPhysicalDeviceContext>(
                        callable::error<CommVkPhysicalDeviceContext>("No physical devices found"));
                }

                std::vector<vk::PhysicalDevice> devices(physical_device.size());
                std::ranges::copy(physical_device, devices.begin());

                // Create a scoring function for devices
                auto score_device = [&ctx](vk::PhysicalDevice device) -> std::optional<std::pair<int, CommVkPhysicalDeviceContext>>
                {
                    vk::PhysicalDeviceProperties properties         = device.getProperties();
                    vk::PhysicalDeviceFeatures features             = device.getFeatures();
                    vk::PhysicalDeviceMemoryProperties memory_props = device.getMemoryProperties();

                    // Get queue family properties
                    std::vector<vk::QueueFamilyProperties> queue_families = device.getQueueFamilyProperties();

                    // Get available extensions
                    std::vector<vk::ExtensionProperties> extensions = device.enumerateDeviceExtensionProperties();

                    // Get Vulkan 1.1, 1.2, 1.3 features if API version supports them
                    vk::PhysicalDeviceVulkan11Features features_11;
                    vk::PhysicalDeviceVulkan12Features features_12;
                    vk::PhysicalDeviceVulkan13Features features_13;
                    vk::PhysicalDeviceFeatures2 features2;
                    features2.setPNext(&features_11);
                    features2.setPNext(&features_12);
                    features2.setPNext(&features_13);
                    device.getFeatures2(&features2);

                    // Check basic requirements

                    auto meets_api_version = [&]()
                    {
                        return !ctx.selection_criteria_.minimum_api_version_.has_value() ||
                               properties.apiVersion >= ctx.selection_criteria_.minimum_api_version_.value();
                    };

                    auto meets_extension_requirements = [&]()
                    {
                        return std::ranges::all_of(ctx.selection_criteria_.required_extensions_,
                                                   [&extensions](const char* required_ext)
                                                   {
                                                       return std::ranges::any_of(extensions,
                                                                                  [required_ext](const auto& available_ext)
                                                                                  {
                                                                                      return std::string_view(required_ext) ==
                                                                                             std::string_view(available_ext.extensionName);
                                                                                  });
                                                   });
                    };

                    auto meets_queue_requirements = [&]()
                    {
                        return std::ranges::all_of(
                            ctx.selection_criteria_.queue_requirements_,
                            [&](const auto& queue_req)
                            {
                                return std::ranges::any_of(
                                    std::views::iota(0U, static_cast<unsigned int>(queue_families.size())),
                                    [&](unsigned int idx)
                                    {
                                        const auto& family    = queue_families[idx];
                                        bool flags_match      = (family.queueFlags & queue_req.queue_flags_) == queue_req.queue_flags_;
                                        bool count_sufficient = family.queueCount >= queue_req.min_queue_count_;

                                        if (!flags_match || !count_sufficient)
                                            return false;

                                        if (queue_req.require_present_support_ && ctx.selection_criteria_.surface_ != VK_NULL_HANDLE)
                                        {
                                            auto result = device.getSurfaceSupportKHR(idx, ctx.selection_criteria_.surface_);
                                            return result == vk::True;
                                        }
                                        return true;
                                    });
                            });
                    };

                    // Check features requirements - helper lambda to check if required features are supported
                    auto meets_features_requirements = [&]() -> bool
                    {
                        // Check Vulkan 1.0 features
                        auto check_features_1_0 = [](const VkPhysicalDeviceFeatures& required, const VkPhysicalDeviceFeatures& available) -> bool
                        {
                            // Use a simple approach: check each feature field
                            const auto* req_array          = reinterpret_cast<const VkBool32*>(&required);
                            const auto* avail_array        = reinterpret_cast<const VkBool32*>(&available);
                            constexpr size_t feature_count = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);

                            for (size_t i = 0; i < feature_count; ++i)
                            {
                                if (req_array[i] == VK_TRUE && avail_array[i] != VK_TRUE)
                                {
                                    return false;
                                }
                            }
                            return true;
                        };

// Check Vulkan 1.1 features
#define CHECK_FEATURE(feature)                                                                                                                       \
    if (required.feature == VK_TRUE && available.feature != VK_TRUE)                                                                                 \
        return false;
                        auto check_features_1_1 = [](const vk::PhysicalDeviceVulkan11Features& required,
                                                     const vk::PhysicalDeviceVulkan11Features& available) -> bool
                        {
                            CHECK_FEATURE(storageBuffer16BitAccess)
                            CHECK_FEATURE(uniformAndStorageBuffer16BitAccess)
                            CHECK_FEATURE(storagePushConstant16)
                            CHECK_FEATURE(storageInputOutput16)
                            CHECK_FEATURE(multiview)
                            CHECK_FEATURE(multiviewGeometryShader)
                            CHECK_FEATURE(multiviewTessellationShader)
                            CHECK_FEATURE(variablePointersStorageBuffer)
                            CHECK_FEATURE(variablePointers)
                            CHECK_FEATURE(protectedMemory)
                            CHECK_FEATURE(samplerYcbcrConversion)
                            CHECK_FEATURE(shaderDrawParameters)

                            return true;
                        };

                        // Check Vulkan 1.2 features
                        auto check_features_1_2 = [](const VkPhysicalDeviceVulkan12Features& required,
                                                     const VkPhysicalDeviceVulkan12Features& available) -> bool
                        {
                            CHECK_FEATURE(samplerMirrorClampToEdge)
                            CHECK_FEATURE(drawIndirectCount)
                            CHECK_FEATURE(storageBuffer8BitAccess)
                            CHECK_FEATURE(uniformAndStorageBuffer8BitAccess)
                            CHECK_FEATURE(storagePushConstant8)
                            CHECK_FEATURE(shaderBufferInt64Atomics)
                            CHECK_FEATURE(shaderSharedInt64Atomics)
                            CHECK_FEATURE(shaderFloat16)
                            CHECK_FEATURE(shaderInt8)
                            CHECK_FEATURE(descriptorIndexing)
                            CHECK_FEATURE(shaderInputAttachmentArrayDynamicIndexing)
                            CHECK_FEATURE(shaderUniformTexelBufferArrayDynamicIndexing)
                            CHECK_FEATURE(shaderStorageTexelBufferArrayDynamicIndexing)
                            CHECK_FEATURE(shaderUniformBufferArrayNonUniformIndexing)
                            CHECK_FEATURE(shaderSampledImageArrayNonUniformIndexing)
                            CHECK_FEATURE(shaderStorageBufferArrayNonUniformIndexing)
                            CHECK_FEATURE(shaderStorageImageArrayNonUniformIndexing)
                            CHECK_FEATURE(shaderInputAttachmentArrayNonUniformIndexing)
                            CHECK_FEATURE(shaderUniformTexelBufferArrayNonUniformIndexing)
                            CHECK_FEATURE(shaderStorageTexelBufferArrayNonUniformIndexing)
                            CHECK_FEATURE(descriptorBindingUniformBufferUpdateAfterBind)
                            CHECK_FEATURE(descriptorBindingSampledImageUpdateAfterBind)
                            CHECK_FEATURE(descriptorBindingStorageImageUpdateAfterBind)
                            CHECK_FEATURE(descriptorBindingStorageBufferUpdateAfterBind)
                            CHECK_FEATURE(descriptorBindingUniformTexelBufferUpdateAfterBind)
                            CHECK_FEATURE(descriptorBindingStorageTexelBufferUpdateAfterBind)
                            CHECK_FEATURE(descriptorBindingUpdateUnusedWhilePending)
                            CHECK_FEATURE(descriptorBindingPartiallyBound)
                            CHECK_FEATURE(descriptorBindingVariableDescriptorCount)
                            CHECK_FEATURE(runtimeDescriptorArray)
                            CHECK_FEATURE(samplerFilterMinmax)
                            CHECK_FEATURE(scalarBlockLayout)
                            CHECK_FEATURE(imagelessFramebuffer)
                            CHECK_FEATURE(uniformBufferStandardLayout)
                            CHECK_FEATURE(shaderSubgroupExtendedTypes)
                            CHECK_FEATURE(separateDepthStencilLayouts)
                            CHECK_FEATURE(hostQueryReset)
                            CHECK_FEATURE(timelineSemaphore)
                            CHECK_FEATURE(bufferDeviceAddress)
                            CHECK_FEATURE(bufferDeviceAddressCaptureReplay)
                            CHECK_FEATURE(bufferDeviceAddressMultiDevice)
                            CHECK_FEATURE(vulkanMemoryModel)
                            CHECK_FEATURE(vulkanMemoryModelDeviceScope)
                            CHECK_FEATURE(vulkanMemoryModelAvailabilityVisibilityChains)
                            CHECK_FEATURE(shaderOutputViewportIndex)
                            CHECK_FEATURE(shaderOutputLayer)
                            CHECK_FEATURE(subgroupBroadcastDynamicId)

#undef CHECK_FEATURE_12
                            return true;
                        };

                        // Check Vulkan 1.3 features
                        auto check_features_1_3 = [](const VkPhysicalDeviceVulkan13Features& required,
                                                     const VkPhysicalDeviceVulkan13Features& available) -> bool
                        {
                            CHECK_FEATURE(robustImageAccess)
                            CHECK_FEATURE(inlineUniformBlock)
                            CHECK_FEATURE(descriptorBindingInlineUniformBlockUpdateAfterBind)
                            CHECK_FEATURE(pipelineCreationCacheControl)
                            CHECK_FEATURE(privateData)
                            CHECK_FEATURE(shaderDemoteToHelperInvocation)
                            CHECK_FEATURE(shaderTerminateInvocation)
                            CHECK_FEATURE(subgroupSizeControl)
                            CHECK_FEATURE(computeFullSubgroups)
                            CHECK_FEATURE(synchronization2)
                            CHECK_FEATURE(textureCompressionASTC_HDR)
                            CHECK_FEATURE(shaderZeroInitializeWorkgroupMemory)
                            CHECK_FEATURE(dynamicRendering)
                            CHECK_FEATURE(shaderIntegerDotProduct)
                            CHECK_FEATURE(maintenance4)

#undef CHECK_FEATURE_13
                            return true;
                        };

                        // Perform checks
                        if (!check_features_1_0(ctx.selection_criteria_.required_features_, features))
                        {
                            return false;
                        }

                        if (properties.apiVersion >= VK_API_VERSION_1_1)
                        {
                            if (!check_features_1_1(ctx.selection_criteria_.required_features_11_, features_11))
                            {
                                return false;
                            }
                        }

                        if (properties.apiVersion >= VK_API_VERSION_1_2)
                        {
                            if (!check_features_1_2(ctx.selection_criteria_.required_features_12_, features_12))
                            {
                                return false;
                            }
                        }

                        if (properties.apiVersion >= VK_API_VERSION_1_3)
                        {
                            if (!check_features_1_3(ctx.selection_criteria_.required_features_13_, features_13))
                            {
                                return false;
                            }
                        }

                        return true;
                    };

                    // Check all requirements including features
                    if (!meets_api_version() || !meets_extension_requirements() || !meets_queue_requirements() || !meets_features_requirements())
                    {
                        return std::nullopt;
                    }

                    // Calculate score
                    int score = 0;

                    // Device type score
                    if (ctx.selection_criteria_.prefer_discrete_gpu_ && properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
                    {
                        score += 10000;
                    }
                    else if (properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu)
                    {
                        score += 500;
                    }

                    // Memory score
                    auto device_memory_heaps = std::views::iota(0U, memory_props.memoryHeapCount) |
                                               std::views::transform(
                                                   [&memory_props](uint32_t idx) -> vk::MemoryHeap
                                                   {
                                                       assert(idx < VK_MAX_MEMORY_HEAPS && "Memory heap index out of bounds");
                                                       return memory_props.memoryHeaps[idx];
                                                   }) |
                                               std::views::filter([](const vk::MemoryHeap& heap)
                                                                  { return static_cast<bool>(heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal); });

                    vk::DeviceSize total_memory = std::accumulate(device_memory_heaps.begin(),
                                                                  device_memory_heaps.end(),
                                                                  vk::DeviceSize{0},
                                                                  [](vk::DeviceSize sum, const auto& heap) { return sum + heap.size; });

                    // Memory score: 100 points per GB
                    int memory_score = static_cast<int>(total_memory / (static_cast<vk::DeviceSize>(1024 * 1024 * 1024))) * 100;
                    // Reduce memory score weight for integrated GPUs as they use shared system memory
                    if (properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu)
                    {
                        memory_score /= 2;
                    }

                    score += memory_score;

                    // Create context copy with updated data
                    CommVkPhysicalDeviceContext result_ctx = ctx;
                    result_ctx.vk_physical_device_         = device;
                    result_ctx.device_properties_          = properties;
                    result_ctx.device_features_            = features;
                    result_ctx.memory_properties_          = memory_props;
                    result_ctx.queue_family_properties_    = queue_families;
                    result_ctx.available_extensions_       = extensions;

                    return std::make_pair(score, std::move(result_ctx));
                };

                // Score all devices and find the best one
                auto scored_devices = devices | std::views::transform(score_device) |
                                      std::views::filter([](const auto& opt) { return opt.has_value(); }) |
                                      std::views::transform([](const auto& opt) { return opt.value(); });

                auto best_device_it =
                    std::ranges::max_element(scored_devices, [](const auto& left, const auto& right) { return left.first < right.first; });

                if (best_device_it == scored_devices.end())
                {
                    return callable::Chainable<CommVkPhysicalDeviceContext>(
                        callable::error<CommVkPhysicalDeviceContext>("No suitable physical device found"));
                }

                ctx = std::move((*best_device_it).second);
                if (ctx.vk_physical_device_ == VK_NULL_HANDLE)
                {
                    return callable::Chainable<CommVkPhysicalDeviceContext>(
                        callable::error<CommVkPhysicalDeviceContext>("No suitable physical device found"));
                }
                std::cout << "Selected device: " << ctx.device_properties_.deviceName << '\n';
                return callable::make_chain(std::move(ctx));
            };
        }

    } // namespace physicaldevice

    /// -------------------------------------------------
    /// vulkan logical device templated functions: common
    /// -------------------------------------------------

    /// @brief Vulkan logical device context with lazy evaluation support
    struct CommVkLogicalDeviceContext
    {
        // parent physical device context - now includes the validated features
        vk::PhysicalDevice vk_physical_device_ = VK_NULL_HANDLE;
        vk::PhysicalDeviceProperties device_properties_;
        vk::PhysicalDeviceFeatures device_features_;
        std::vector<vk::QueueFamilyProperties> queue_family_properties_;

        // features from physical device selection (already validated)
        vk::PhysicalDeviceFeatures validated_features_;
        vk::PhysicalDeviceVulkan11Features validated_features_11_;
        vk::PhysicalDeviceVulkan12Features validated_features_12_;
        vk::PhysicalDeviceVulkan13Features validated_features_13_;

        // logical device creation info
        struct DeviceInfo
        {
            std::vector<vk::DeviceQueueCreateInfo> queue_create_infos_;
            std::vector<const char*> required_extensions_;
            void* p_next_ = nullptr;
        } device_info_;

        // queue configuration
        struct QueueInfo
        {
            uint32_t queue_family_index_;
            uint32_t queue_count_;
            std::vector<float> queue_priorities_;
            vk::QueueFlags queue_flags_;
            std::string queue_name_; // for identification
        };
        std::vector<QueueInfo> queue_infos_;

        // vulkan natives
        vk::Device vk_logical_device_ = VK_NULL_HANDLE;
        std::unordered_map<std::string, vk::Queue> named_queues_;
        std::unordered_map<uint32_t, std::vector<vk::Queue>> family_queues_;
    };

    namespace logicaldevice
    {

        /// @brief Creates initial logical device context from physical device context
        inline auto create_logical_device_context(const CommVkPhysicalDeviceContext& physical_device_ctx)
        {
            return callable::make_chain(
                [physical_device_ctx]() -> CommVkLogicalDeviceContext
                {
                    CommVkLogicalDeviceContext ctx;
                    ctx.vk_physical_device_      = physical_device_ctx.vk_physical_device_;
                    ctx.device_properties_       = physical_device_ctx.device_properties_;
                    ctx.device_features_         = physical_device_ctx.device_features_;
                    ctx.queue_family_properties_ = physical_device_ctx.queue_family_properties_;

                    // Copy the validated features from physical device context
                    ctx.validated_features_    = physical_device_ctx.selection_criteria_.required_features_;
                    ctx.validated_features_11_ = physical_device_ctx.selection_criteria_.required_features_11_;
                    ctx.validated_features_12_ = physical_device_ctx.selection_criteria_.required_features_12_;
                    ctx.validated_features_13_ = physical_device_ctx.selection_criteria_.required_features_13_;

                    return ctx;
                }());
        }

        /// @brief Adds required device extensions
        inline auto require_extensions(const std::vector<const char*>& extensions)
        {
            return [extensions](CommVkLogicalDeviceContext ctx) -> callable::Chainable<CommVkLogicalDeviceContext>
            {
                ctx.device_info_.required_extensions_.insert(ctx.device_info_.required_extensions_.end(), extensions.begin(), extensions.end());
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Adds a queue request with name for easy identification
        inline auto add_queue(const std::string& queue_name,
                              uint32_t queue_family_index,
                              uint32_t queue_count                 = 1,
                              const std::vector<float>& priorities = {1.0F})
        {
            return [queue_name, queue_family_index, queue_count, priorities](
                       CommVkLogicalDeviceContext ctx) -> callable::Chainable<CommVkLogicalDeviceContext>
            {
                // Validate queue family index
                if (queue_family_index >= ctx.queue_family_properties_.size())
                {
                    return callable::Chainable<CommVkLogicalDeviceContext>(callable::error<CommVkLogicalDeviceContext>("Invalid queue family index"));
                }

                // Validate queue count
                if (queue_count > ctx.queue_family_properties_[queue_family_index].queueCount)
                {
                    return callable::Chainable<CommVkLogicalDeviceContext>(
                        callable::error<CommVkLogicalDeviceContext>("Requested queue count exceeds available queues"));
                }

                // Create queue info
                CommVkLogicalDeviceContext::QueueInfo queue_info;
                queue_info.queue_family_index_ = queue_family_index;
                queue_info.queue_count_        = queue_count;
                queue_info.queue_priorities_   = priorities.empty() ? std::vector<float>(queue_count, 1.0F) : priorities;
                queue_info.queue_flags_        = ctx.queue_family_properties_[queue_family_index].queueFlags;
                queue_info.queue_name_         = queue_name;

                // Ensure priorities match queue count
                if (queue_info.queue_priorities_.size() != queue_count)
                {
                    queue_info.queue_priorities_.resize(queue_count, 1.0F);
                }

                ctx.queue_infos_.push_back(queue_info);
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Adds a graphics queue automatically finding suitable family
        inline auto add_graphics_queue(const std::string& queue_name = "graphics", vk::SurfaceKHR surface = VK_NULL_HANDLE, uint32_t queue_count = 1)
        {
            return [queue_name, surface, queue_count](CommVkLogicalDeviceContext ctx) -> callable::Chainable<CommVkLogicalDeviceContext>
            {
                // Find graphics queue family
                std::optional<uint32_t> graphics_family;

                auto graphics_families = std::views::iota(0U, static_cast<uint32_t>(ctx.queue_family_properties_.size())) |
                                         std::views::filter(
                                             [&](uint32_t idx)
                                             {
                                                 const auto& family = ctx.queue_family_properties_[idx];

                                                 // Check for graphics support
                                                 if (!(family.queueFlags & vk::QueueFlagBits::eGraphics))
                                                     return false;

                                                 // If surface provided, check for present support
                                                 if (surface != VK_NULL_HANDLE)
                                                 {
                                                     vk::Bool32 present_support = ctx.vk_physical_device_.getSurfaceSupportKHR(idx, surface);
                                                     return present_support == vk::True;
                                                 }

                                                 return true;
                                             });

                auto first_suitable_graphics = graphics_families.begin();
                if (first_suitable_graphics != graphics_families.end())
                {
                    graphics_family = *first_suitable_graphics;
                }

                if (!graphics_family.has_value())
                {
                    return callable::Chainable<CommVkLogicalDeviceContext>(
                        callable::error<CommVkLogicalDeviceContext>("No suitable graphics queue family found"));
                }

                return add_queue(queue_name, graphics_family.value(), queue_count)(std::move(ctx));
            };
        }

        /// @brief Adds a compute queue automatically finding suitable family
        /// @param queue_name Name for the compute queue (default is "compute")
        /// @param queue_count Number of compute queues to create (default is 1)
        inline auto add_compute_queue(const std::string& queue_name = "compute", uint32_t queue_count = 1)
        {
            return [queue_name, queue_count](CommVkLogicalDeviceContext ctx) -> callable::Chainable<CommVkLogicalDeviceContext>
            {
                // find dedicated compute queue family first
                auto dedicated_compute =
                    std::ranges::find_if(std::views::iota(0U, static_cast<uint32_t>(ctx.queue_family_properties_.size())),
                                         [&](uint32_t idx)
                                         {
                                             const auto& family = ctx.queue_family_properties_[idx];
                                             return (family.queueFlags & vk::QueueFlagBits::eCompute) &&
                                                    !(family.queueFlags & vk::QueueFlagBits::eGraphics);
                                         });
                if (dedicated_compute != std::ranges::end(std::views::iota(0U, static_cast<uint32_t>(ctx.queue_family_properties_.size()))))
                {
                    return add_queue(queue_name, *dedicated_compute, queue_count)(std::move(ctx));
                }

                // Otherwise find graphics queue that supports compute
                auto compute_graphics = std::ranges::find_if(std::views::iota(0U, static_cast<uint32_t>(ctx.queue_family_properties_.size())),
                                                             [&](uint32_t idx)
                                                             {
                                                                 const auto& family = ctx.queue_family_properties_[idx];
                                                                 return (family.queueFlags & vk::QueueFlagBits::eCompute) &&
                                                                        (family.queueFlags & vk::QueueFlagBits::eGraphics);
                                                             });
                if (compute_graphics != std::ranges::end(std::views::iota(0U, static_cast<uint32_t>(ctx.queue_family_properties_.size()))))
                {
                    return add_queue(queue_name, *compute_graphics, queue_count)(std::move(ctx));
                }

                // fallback
                return callable::Chainable<CommVkLogicalDeviceContext>(
                    callable::error<CommVkLogicalDeviceContext>("No suitable compute queue family found"));
            };
        }

        /// @brief Adds a transfer queue automatically finding suitable family
        /// @param queue_name Name for the transfer queue (default is "transfer")
        /// @param queue_count Number of transfer queues to create (default is 1)
        inline auto add_transfer_queue(const std::string& queue_name = "transfer", uint32_t queue_count = 1)
        {
            return [queue_name, queue_count](CommVkLogicalDeviceContext ctx) -> callable::Chainable<CommVkLogicalDeviceContext>
            {
                // find dedicated compute queue family first
                auto dedicated_transfer =
                    std::ranges::find_if(std::views::iota(0U, static_cast<uint32_t>(ctx.queue_family_properties_.size())),
                                         [&](uint32_t idx)
                                         {
                                             const auto& family = ctx.queue_family_properties_[idx];
                                             return (family.queueFlags & vk::QueueFlagBits::eTransfer) &&
                                                    !(family.queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute));
                                         });
                if (dedicated_transfer != std::ranges::end(std::views::iota(0U, static_cast<uint32_t>(ctx.queue_family_properties_.size()))))
                {
                    return add_queue(queue_name, *dedicated_transfer, queue_count)(std::move(ctx));
                }

                // Otherwise find graphics queue that supports transfer
                auto transfer_graphics = std::ranges::find_if(std::views::iota(0U, static_cast<uint32_t>(ctx.queue_family_properties_.size())),
                                                              [&](uint32_t idx)
                                                              {
                                                                  const auto& family = ctx.queue_family_properties_[idx];
                                                                  return (family.queueFlags & vk::QueueFlagBits::eTransfer) &&
                                                                         (family.queueFlags & vk::QueueFlagBits::eGraphics);
                                                              });
                if (transfer_graphics != std::ranges::end(std::views::iota(0U, static_cast<uint32_t>(ctx.queue_family_properties_.size()))))
                {
                    return add_queue(queue_name, *transfer_graphics, queue_count)(std::move(ctx));
                }

                // fallback
                return callable::Chainable<CommVkLogicalDeviceContext>(
                    callable::error<CommVkLogicalDeviceContext>("No suitable transfer queue family found"));
            };
        }

        /// @brief Validates device configuration
        inline auto validate_device_configuration()
        {
            return [](CommVkLogicalDeviceContext ctx) -> callable::Chainable<CommVkLogicalDeviceContext>
            {
                if (ctx.queue_infos_.empty())
                {
                    return callable::Chainable<CommVkLogicalDeviceContext>(
                        callable::error<CommVkLogicalDeviceContext>("No queues specified for device creation"));
                }

                // Check for duplicate queue names
                std::unordered_set<std::string> queue_names;
                for (const auto& queue_info : ctx.queue_infos_)
                {
                    if (!queue_names.insert(queue_info.queue_name_).second)
                    {
                        return callable::Chainable<CommVkLogicalDeviceContext>(
                            callable::error<CommVkLogicalDeviceContext>("Duplicate queue name: " + queue_info.queue_name_));
                    }
                }

                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Creates the logical device and retrieves queues
        inline auto create_logical_device()
        {
            return [](CommVkLogicalDeviceContext ctx) -> callable::Chainable<CommVkLogicalDeviceContext>
            {
                // Consolidate queue create infos by family
                std::unordered_map<uint32_t, vk::DeviceQueueCreateInfo> family_queue_infos;
                std::unordered_map<uint32_t, std::vector<float>> family_priorities;

                for (const auto& queue_info : ctx.queue_infos_)
                {
                    uint32_t family_index = queue_info.queue_family_index_;

                    if (!family_queue_infos.contains(family_index))
                    {
                        // First queue for this family
                        vk::DeviceQueueCreateInfo queue_create_info;
                        queue_create_info.setQueueFamilyIndex(family_index);
                        queue_create_info.setQueueCount(queue_info.queue_count_);

                        family_queue_infos[family_index] = queue_create_info;
                        family_priorities[family_index]  = queue_info.queue_priorities_;
                    }
                    else
                    {
                        // Additional queues for existing family
                        auto& existing_info       = family_queue_infos[family_index];
                        auto& existing_priorities = family_priorities[family_index];

                        existing_info.setQueueCount(existing_info.queueCount + queue_info.queue_count_);
                        existing_priorities.insert(
                            existing_priorities.end(), queue_info.queue_priorities_.begin(), queue_info.queue_priorities_.end());
                    }
                }

                // Validate and clamp queue counts
                for (auto& [family_index, queue_info] : family_queue_infos)
                {
                    uint32_t available_count = ctx.queue_family_properties_[family_index].queueCount;
                    if (queue_info.queueCount > available_count)
                    {
                        std::cerr << "Warning: Requested " << queue_info.queueCount << " queues for family " << family_index << ", but only "
                                  << available_count << " are available. "
                                  << "Queues will be shared.\n";
                        queue_info.setQueueCount(available_count);
                    }
                }

                // Update priority pointers
                for (auto& [family_index, queue_info] : family_queue_infos)
                {
                    queue_info.pQueuePriorities = family_priorities[family_index].data();
                }

                // Convert to vector for device creation
                std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
                queue_create_infos.reserve(family_queue_infos.size());
                for (const auto& [family_index, queue_info] : family_queue_infos)
                {
                    queue_create_infos.push_back(queue_info);
                }

                // Setup feature chain using validated features from physical device
                void* feature_chain = nullptr;
                if (ctx.device_properties_.apiVersion >= VK_API_VERSION_1_3)
                {
                    // Chain: features_13 -> features_12 -> features_11 -> nullptr
                    ctx.validated_features_13_.pNext = nullptr;
                    ctx.validated_features_12_.pNext = &ctx.validated_features_13_;
                    ctx.validated_features_11_.pNext = &ctx.validated_features_12_;
                    feature_chain                    = &ctx.validated_features_11_;
                }
                else if (ctx.device_properties_.apiVersion >= VK_API_VERSION_1_2)
                {
                    // Chain: features_12 -> features_11 -> nullptr
                    ctx.validated_features_12_.pNext = nullptr;
                    ctx.validated_features_11_.pNext = &ctx.validated_features_12_;
                    feature_chain                    = &ctx.validated_features_11_;
                }
                else if (ctx.device_properties_.apiVersion >= VK_API_VERSION_1_1)
                {
                    // Chain: features_11 -> nullptr
                    ctx.validated_features_11_.pNext = nullptr;
                    feature_chain                    = &ctx.validated_features_11_;
                }

                // Create device
                vk::DeviceCreateInfo device_create_info;
                device_create_info.setQueueCreateInfos(queue_create_infos)
                    .setEnabledExtensionCount(static_cast<uint32_t>(ctx.device_info_.required_extensions_.size()))
                    .setPpEnabledExtensionNames(ctx.device_info_.required_extensions_.empty() ? nullptr
                                                                                              : ctx.device_info_.required_extensions_.data())
                    .setPEnabledFeatures(&ctx.validated_features_) // Use validated features
                    .setPNext(feature_chain);                      // Add Vulkan 1.1 features
                ctx.vk_logical_device_ = ctx.vk_physical_device_.createDevice(device_create_info, nullptr);

                // Retrieve queues
                std::unordered_map<uint32_t, uint32_t> family_queue_counters;
                for (const auto& queue_info : ctx.queue_infos_)
                {
                    uint32_t family_index = queue_info.queue_family_index_;
                    uint32_t& counter     = family_queue_counters[family_index];

                    // Get the actual number of queues created for this family (clamped value)
                    uint32_t created_count = family_queue_infos[family_index].queueCount;

                    for (uint32_t i = 0; i < queue_info.queue_count_; ++i)
                    {
                        // Use modulo to map logical request to available physical queue
                        uint32_t physical_index = counter % created_count;

                        vk::Queue queue = ctx.vk_logical_device_.getQueue(family_index, physical_index);

                        // Store in named queues (use index suffix for multiple queues)
                        std::string queue_name = queue_info.queue_name_;
                        if (queue_info.queue_count_ > 1)
                        {
                            queue_name += "_" + std::to_string(i);
                        }
                        ctx.named_queues_[queue_name] = queue;

                        // Store in family queues
                        ctx.family_queues_[family_index].emplace_back(queue);

                        // Log queue capabilities
                        const auto& family_props = ctx.queue_family_properties_[family_index];
                        std::cout << "Queue '" << queue_name << "' (Family " << family_index << ", Index " << physical_index
                                  << ") created. Capabilities: ";
                        if (family_props.queueFlags & vk::QueueFlagBits::eGraphics)
                            std::cout << "Graphics ";
                        if (family_props.queueFlags & vk::QueueFlagBits::eCompute)
                            std::cout << "Compute ";
                        if (family_props.queueFlags & vk::QueueFlagBits::eTransfer)
                            std::cout << "Transfer ";
                        if (family_props.queueFlags & vk::QueueFlagBits::eSparseBinding)
                            std::cout << "SparseBinding ";
                        std::cout << "\n";

                        ++counter;
                    }
                }

                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Helper function to get queue by name
        inline VkQueue get_queue(const CommVkLogicalDeviceContext& ctx, const std::string& queue_name)
        {
            auto it = ctx.named_queues_.find(queue_name);
            return (it != ctx.named_queues_.end()) ? it->second : VK_NULL_HANDLE;
        }

        /// @brief Helper function to get queue family index by queue flags (DEPRECATED - use find_queue_family_by_name instead)
        inline std::optional<uint32_t> find_queue_family(const CommVkLogicalDeviceContext& ctx, vk::QueueFlags queue_flags)
        {
            for (uint32_t i = 0; i < ctx.queue_family_properties_.size(); i++)
            {
                if ((ctx.queue_family_properties_[i].queueFlags & queue_flags) == queue_flags)
                {
                    return i;
                }
            }
            return std::nullopt;
        }

        /// @brief Helper function to find queue family index by queue name (searches in created queues)
        /// @param ctx The logical device context
        /// @param queue_name Name of the queue to find family index for
        /// @return Queue family index if found, std::nullopt otherwise
        inline std::optional<uint32_t> find_queue_family_by_name(const CommVkLogicalDeviceContext& ctx, const std::string& queue_name)
        {
            // Search through created queue infos to find the queue family index
            for (const auto& queue_info : ctx.queue_infos_)
            {
                // Check exact match first
                if (queue_info.queue_name_ == queue_name)
                {
                    return queue_info.queue_family_index_;
                }

                // Check if it's a multi-queue name (e.g., "graphics_0", "graphics_1")
                if (queue_info.queue_count_ > 1)
                {
                    for (uint32_t i = 0; i < queue_info.queue_count_; ++i)
                    {
                        std::string indexed_name = queue_info.queue_name_ + "_" + std::to_string(i);
                        if (indexed_name == queue_name)
                        {
                            return queue_info.queue_family_index_;
                        }
                    }
                }
            }
            return std::nullopt;
        }

        /// @brief Helper function to find queue family index by queue flags with preference for dedicated queues
        /// @param ctx The logical device context
        /// @param queue_flags Required queue flags
        /// @param prefer_dedicated Whether to prefer dedicated queues (default true)
        /// @return Queue family index if found, std::nullopt otherwise
        inline std::optional<uint32_t> find_optimal_queue_family(const CommVkLogicalDeviceContext& ctx,
                                                                 vk::QueueFlags queue_flags,
                                                                 bool prefer_dedicated = true)
        {
            std::vector<uint32_t> suitable_families;
            std::vector<uint32_t> created_families; // Families that user actually created queues for

            // Collect all created queue family indices
            created_families.reserve(ctx.queue_infos_.size());
            for (const auto& queue_info : ctx.queue_infos_)
            {
                created_families.push_back(queue_info.queue_family_index_);
            }

            // Find all suitable families
            for (uint32_t i = 0; i < ctx.queue_family_properties_.size(); i++)
            {
                if ((ctx.queue_family_properties_[i].queueFlags & queue_flags) == queue_flags)
                {
                    // Only consider families that user actually created queues for
                    if (std::ranges::find(created_families, i) != created_families.end())
                    {
                        suitable_families.push_back(i);
                    }
                }
            }

            if (suitable_families.empty())
            {
                return std::nullopt;
            }

            if (!prefer_dedicated)
            {
                return suitable_families[0]; // Return first suitable
            }

            // Try to find dedicated queue family first
            for (uint32_t family_idx : suitable_families)
            {
                vk::QueueFlags family_flags = ctx.queue_family_properties_[family_idx].queueFlags;

                // For transfer queues, prefer families that only have transfer (and possibly sparse binding)
                if (queue_flags == vk::QueueFlagBits::eTransfer)
                {
                    vk::QueueFlags non_transfer_flags = family_flags & ~(vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding);
                    if (non_transfer_flags == vk::QueueFlags())
                    {
                        return family_idx; // Found dedicated transfer queue
                    }
                }
                // For compute queues, prefer families that only have compute (and possibly transfer/sparse)
                else if (queue_flags == vk::QueueFlagBits::eCompute)
                {
                    vk::QueueFlags non_compute_flags =
                        family_flags & ~(vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding);
                    if (non_compute_flags == vk::QueueFlags())
                    {
                        return family_idx; // Found dedicated compute queue
                    }
                }
                // For graphics queues, any graphics queue is fine since graphics usually includes everything
                else if (queue_flags & vk::QueueFlagBits::eGraphics)
                {
                    return family_idx; // Graphics queues are typically shared anyway
                }
            }

            // If no dedicated queue found, return the first suitable one
            return suitable_families[0];
        }

        /// @brief Helper function to get queue info by name
        /// @param ctx The logical device context
        /// @param queue_name Name of the queue
        /// @return Pointer to queue info if found, nullptr otherwise
        inline const CommVkLogicalDeviceContext::QueueInfo* find_queue_info_by_name(const CommVkLogicalDeviceContext& ctx,
                                                                                    const std::string& queue_name)
        {
            for (const auto& queue_info : ctx.queue_infos_)
            {
                // Check exact match first
                if (queue_info.queue_name_ == queue_name)
                {
                    return &queue_info;
                }

                // Check if it's a multi-queue name (e.g., "graphics_0", "graphics_1")
                if (queue_info.queue_count_ > 1)
                {
                    for (uint32_t i = 0; i < queue_info.queue_count_; ++i)
                    {
                        std::string indexed_name = queue_info.queue_name_ + "_" + std::to_string(i);
                        if (indexed_name == queue_name)
                        {
                            return &queue_info;
                        }
                    }
                }
            }
            return nullptr;
        }

        /// @brief Helper function to list all created queues with their family indices
        /// @param ctx The logical device context
        /// @return Map of queue names to their family indices
        inline std::unordered_map<std::string, uint32_t> get_all_queue_families(const CommVkLogicalDeviceContext& ctx)
        {
            std::unordered_map<std::string, uint32_t> queue_families;

            for (const auto& queue_info : ctx.queue_infos_)
            {
                if (queue_info.queue_count_ == 1)
                {
                    queue_families[queue_info.queue_name_] = queue_info.queue_family_index_;
                }
                else
                {
                    for (uint32_t i = 0; i < queue_info.queue_count_; ++i)
                    {
                        std::string indexed_name     = queue_info.queue_name_ + "_" + std::to_string(i);
                        queue_families[indexed_name] = queue_info.queue_family_index_;
                    }
                }
            }

            return queue_families;
        }

        /// @brief Helper function to get all queues from a family
        inline std::vector<vk::Queue> get_family_queues(const CommVkLogicalDeviceContext& ctx, uint32_t family_index)
        {
            auto it = ctx.family_queues_.find(family_index);
            return (it != ctx.family_queues_.end()) ? it->second : std::vector<vk::Queue>{};
        }

    } // namespace logicaldevice

    /// -------------------------------------
    /// swapchain templated functions: common
    /// -------------------------------------

    /// @brief Vulkan swapchain context with lazy evaluation support
    struct CommVkSwapchainContext
    {
        // parent contexts
        vk::Device vk_logical_device_          = VK_NULL_HANDLE;
        vk::PhysicalDevice vk_physical_device_ = VK_NULL_HANDLE;
        vk::SurfaceKHR vk_surface_             = VK_NULL_HANDLE;

        // swapchain configuration
        struct SwapchainConfig
        {
            // surface preferences
            vk::SurfaceFormatKHR preferred_surface_format_{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear};
            vk::PresentModeKHR preferred_present_mode_ = vk::PresentModeKHR::eFifo;

            // extent configuration
            vk::Extent2D desired_extent_{800, 600};
            bool use_current_extent_ = true; // use surface's current extent

            // image configuration
            uint32_t min_image_count_        = 2;
            uint32_t desired_image_count_    = 3; // triple buffering
            vk::ImageUsageFlags image_usage_ = vk::ImageUsageFlagBits::eColorAttachment;

            // sharing mode
            vk::SharingMode sharing_mode_ = vk::SharingMode::eExclusive;
            std::vector<uint32_t> queue_family_indices_;

            // transform and alpha
            vk::SurfaceTransformFlagBitsKHR pre_transform_ = vk::SurfaceTransformFlagBitsKHR::eIdentity;
            vk::CompositeAlphaFlagBitsKHR composite_alpha_ = vk::CompositeAlphaFlagBitsKHR::eOpaque;

            // misc
            vk::Bool32 clipped_             = VK_TRUE;
            vk::SwapchainKHR old_swapchain_ = VK_NULL_HANDLE;

            // fallback options
            std::vector<vk::SurfaceFormatKHR> fallback_surface_formats_;
            std::vector<vk::PresentModeKHR> fallback_present_modes_;
        } swapchain_config_;

        // surface support info
        vk::SurfaceCapabilitiesKHR surface_capabilities_{};
        std::vector<vk::SurfaceFormatKHR> available_surface_formats_;
        std::vector<vk::PresentModeKHR> available_present_modes_;

        // final swapchain info
        struct SwapchainInfo
        {
            vk::SurfaceFormatKHR surface_format_{};
            vk::PresentModeKHR present_mode_{};
            vk::Extent2D extent_{};
            uint32_t image_count_ = 0;
        } swapchain_info_;

        // vulkan natives
        vk::SwapchainKHR vk_swapchain_ = VK_NULL_HANDLE;
        std::vector<vk::Image> swapchain_images_;
        std::vector<vk::ImageView> swapchain_image_views_;
    };

    namespace swapchain
    {

        /// @brief Creates initial swapchain context from logical device context
        inline auto create_swapchain_context(const CommVkLogicalDeviceContext& logical_device_ctx, VkSurfaceKHR surface)
        {
            return callable::make_chain(
                [logical_device_ctx, surface]() -> CommVkSwapchainContext
                {
                    CommVkSwapchainContext ctx;
                    ctx.vk_logical_device_  = logical_device_ctx.vk_logical_device_;
                    ctx.vk_physical_device_ = logical_device_ctx.vk_physical_device_;
                    ctx.vk_surface_         = surface;
                    return ctx;
                }());
        }

        /// @brief Sets preferred surface format
        inline auto set_surface_format(vk::Format format, vk::ColorSpaceKHR color_space)
        {
            return [format, color_space](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.preferred_surface_format_.format     = format;
                ctx.swapchain_config_.preferred_surface_format_.colorSpace = color_space;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets preferred present mode
        inline auto set_present_mode(vk::PresentModeKHR present_mode)
        {
            return [present_mode](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.preferred_present_mode_ = present_mode;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets desired extent (only used if use_current_extent is false)
        inline auto set_desired_extent(uint32_t width, uint32_t height)
        {
            return [width, height](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.desired_extent_     = {.width = width, .height = height};
                ctx.swapchain_config_.use_current_extent_ = false;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets whether to use surface's current extent
        inline auto use_current_extent(bool use_current = true)
        {
            return [use_current](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.use_current_extent_ = use_current;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets desired image count
        inline auto set_image_count(uint32_t min_images, uint32_t desired_images = 0)
        {
            return [min_images, desired_images](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.min_image_count_     = min_images;
                ctx.swapchain_config_.desired_image_count_ = (desired_images == 0) ? min_images : desired_images;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets image usage flags
        inline auto set_image_usage(vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eColorAttachment)
        {
            return [usage](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.image_usage_ = usage;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets sharing mode and queue family indices
        inline auto set_sharing_mode(vk::SharingMode sharing_mode                      = vk::SharingMode::eExclusive,
                                     const std::vector<uint32_t>& queue_family_indices = {})
        {
            return [sharing_mode, queue_family_indices](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.sharing_mode_         = sharing_mode;
                ctx.swapchain_config_.queue_family_indices_ = queue_family_indices;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets composite alpha
        inline auto set_composite_alpha(vk::CompositeAlphaFlagBitsKHR composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque)
        {
            return [composite_alpha](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.composite_alpha_ = composite_alpha;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Sets old swapchain for recreation
        inline auto set_old_swapchain(vk::SwapchainKHR old_swapchain)
        {
            return [old_swapchain](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.old_swapchain_ = old_swapchain;
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Adds fallback surface formats
        inline auto add_fallback_surface_formats(const std::vector<vk::SurfaceFormatKHR>& formats)
        {
            return [formats](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.fallback_surface_formats_.insert(
                    ctx.swapchain_config_.fallback_surface_formats_.end(), formats.begin(), formats.end());
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Adds fallback present modes
        inline auto add_fallback_present_modes(const std::vector<vk::PresentModeKHR>& present_modes)
        {
            return [present_modes](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_config_.fallback_present_modes_.insert(
                    ctx.swapchain_config_.fallback_present_modes_.end(), present_modes.begin(), present_modes.end());
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Queries surface support capabilities
        inline auto query_surface_support()
        {
            return [](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                // Query surface capabilities
                ctx.surface_capabilities_ = ctx.vk_physical_device_.getSurfaceCapabilitiesKHR(ctx.vk_surface_);
                // Query surface formats
                ctx.available_surface_formats_ = ctx.vk_physical_device_.getSurfaceFormatsKHR(ctx.vk_surface_);
                // Query present modes
                ctx.available_present_modes_ = ctx.vk_physical_device_.getSurfacePresentModesKHR(ctx.vk_surface_);

                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Selects optimal swapchain settings
        inline auto select_swapchain_settings()
        {
            return [](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                // Select surface format
                auto select_surface_format = [&ctx]() -> vk::SurfaceFormatKHR
                {
                    // Check preferred format
                    auto preferred_format =
                        std::ranges::find_if(ctx.available_surface_formats_,
                                             [&ctx](const vk::SurfaceFormatKHR& format)
                                             {
                                                 return format.format == ctx.swapchain_config_.preferred_surface_format_.format &&
                                                        format.colorSpace == ctx.swapchain_config_.preferred_surface_format_.colorSpace;
                                             });

                    if (preferred_format != ctx.available_surface_formats_.end())
                    {
                        return *preferred_format;
                    }

                    // Check fallback formats
                    for (const auto& fallback : ctx.swapchain_config_.fallback_surface_formats_)
                    {
                        auto fallback_format =
                            std::ranges::find_if(ctx.available_surface_formats_,
                                                 [&fallback](const vk::SurfaceFormatKHR& format)
                                                 { return format.format == fallback.format && format.colorSpace == fallback.colorSpace; });

                        if (fallback_format != ctx.available_surface_formats_.end())
                        {
                            return *fallback_format;
                        }
                    }

                    // Use very default format or first available format
                    return ctx.available_surface_formats_.empty() ? vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear}
                                                                  // default
                                                                  : ctx.available_surface_formats_[0];
                };

                // Select present mode
                auto select_present_mode = [&ctx]() -> vk::PresentModeKHR
                {
                    // Check preferred mode
                    auto preferred_mode = std::ranges::find(ctx.available_present_modes_, ctx.swapchain_config_.preferred_present_mode_);
                    if (preferred_mode != ctx.available_present_modes_.end())
                    {
                        return *preferred_mode;
                    }

                    // Check fallback modes
                    for (const auto& fallback : ctx.swapchain_config_.fallback_present_modes_)
                    {
                        auto fallback_mode = std::ranges::find(ctx.available_present_modes_, fallback);
                        if (fallback_mode != ctx.available_present_modes_.end())
                        {
                            return *fallback_mode;
                        }
                    }

                    // FIFO is guaranteed to be available
                    return vk::PresentModeKHR::eFifo;
                };

                // Select extent
                auto select_extent = [&ctx]() -> vk::Extent2D
                {
                    if (ctx.surface_capabilities_.currentExtent.width != UINT32_MAX)
                    {
                        return ctx.surface_capabilities_.currentExtent;
                    }

                    vk::Extent2D actual_extent =
                        ctx.swapchain_config_.use_current_extent_ ? ctx.surface_capabilities_.currentExtent : ctx.swapchain_config_.desired_extent_;

                    actual_extent.width = std::clamp(
                        actual_extent.width, ctx.surface_capabilities_.minImageExtent.width, ctx.surface_capabilities_.maxImageExtent.width);
                    actual_extent.height = std::clamp(
                        actual_extent.height, ctx.surface_capabilities_.minImageExtent.height, ctx.surface_capabilities_.maxImageExtent.height);
                    return actual_extent;
                };

                // Select image count
                auto select_image_count = [&ctx]() -> uint32_t
                {
                    uint32_t image_count = std::max(ctx.swapchain_config_.min_image_count_, ctx.swapchain_config_.desired_image_count_);

                    if (ctx.surface_capabilities_.maxImageCount > 0)
                    {
                        image_count = std::min(image_count, ctx.surface_capabilities_.maxImageCount);
                    }

                    return std::max(image_count, ctx.surface_capabilities_.minImageCount);
                };

                ctx.swapchain_info_.surface_format_ = select_surface_format();
                ctx.swapchain_info_.present_mode_   = select_present_mode();
                ctx.swapchain_info_.extent_         = select_extent();
                ctx.swapchain_info_.image_count_    = select_image_count();

                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Creates the swapchain
        inline auto create_swapchain()
        {
            return [](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                vk::SwapchainCreateInfoKHR create_info;
                create_info.setSurface(ctx.vk_surface_)
                    .setMinImageCount(ctx.swapchain_info_.image_count_)
                    .setImageFormat(ctx.swapchain_info_.surface_format_.format)
                    .setImageColorSpace(ctx.swapchain_info_.surface_format_.colorSpace)
                    .setImageExtent(ctx.swapchain_info_.extent_)
                    .setImageArrayLayers(1)
                    .setImageUsage(ctx.swapchain_config_.image_usage_);

                // Configure sharing mode
                if (ctx.swapchain_config_.sharing_mode_ == vk::SharingMode::eConcurrent && !ctx.swapchain_config_.queue_family_indices_.empty())
                {
                    create_info.setImageSharingMode(vk::SharingMode::eConcurrent);
                    create_info.setQueueFamilyIndexCount(static_cast<uint32_t>(ctx.swapchain_config_.queue_family_indices_.size()));
                    create_info.setPQueueFamilyIndices(ctx.swapchain_config_.queue_family_indices_.data());
                }
                else
                {
                    create_info.setImageSharingMode(vk::SharingMode::eExclusive);
                    create_info.setQueueFamilyIndexCount(0);
                    create_info.setPQueueFamilyIndices(nullptr);
                }

                create_info.setPreTransform((ctx.swapchain_config_.pre_transform_ == vk::SurfaceTransformFlagBitsKHR::eIdentity)
                                                ? ctx.surface_capabilities_.currentTransform
                                                : ctx.swapchain_config_.pre_transform_);
                create_info.setCompositeAlpha(ctx.swapchain_config_.composite_alpha_);
                create_info.setPresentMode(ctx.swapchain_info_.present_mode_);
                create_info.setClipped(ctx.swapchain_config_.clipped_);
                create_info.setOldSwapchain(ctx.swapchain_config_.old_swapchain_);

                // Create the swapchain
                ctx.vk_swapchain_ = ctx.vk_logical_device_.createSwapchainKHR(create_info);
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Retrieves swapchain images
        inline auto get_swapchain_images()
        {
            return [](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_images_ = ctx.vk_logical_device_.getSwapchainImagesKHR(ctx.vk_swapchain_);
                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Creates swapchain image views
        inline auto create_image_views()
        {
            return [](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                ctx.swapchain_image_views_.resize(ctx.swapchain_images_.size());

                for (size_t i = 0; i < ctx.swapchain_images_.size(); i++)
                {
                    vk::ImageViewCreateInfo view_info;
                    view_info.setImage(ctx.swapchain_images_[i])
                        .setViewType(vk::ImageViewType::e2D)
                        .setFormat(ctx.swapchain_info_.surface_format_.format)
                        .setComponents({vk::ComponentSwizzle::eIdentity,
                                        vk::ComponentSwizzle::eIdentity,
                                        vk::ComponentSwizzle::eIdentity,
                                        vk::ComponentSwizzle::eIdentity})
                        .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

                    ctx.swapchain_image_views_[i] = ctx.vk_logical_device_.createImageView(view_info);
                }

                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Validates swapchain configuration
        inline auto validate_swapchain()
        {
            return [](CommVkSwapchainContext ctx) -> callable::Chainable<CommVkSwapchainContext>
            {
                if (ctx.vk_swapchain_ == VK_NULL_HANDLE)
                {
                    return callable::Chainable<CommVkSwapchainContext>(callable::error<CommVkSwapchainContext>("Swapchain not created"));
                }

                if (ctx.swapchain_images_.empty())
                {
                    return callable::Chainable<CommVkSwapchainContext>(callable::error<CommVkSwapchainContext>("No swapchain images available"));
                }

                if (ctx.swapchain_image_views_.size() != ctx.swapchain_images_.size())
                {
                    return callable::Chainable<CommVkSwapchainContext>(callable::error<CommVkSwapchainContext>("Image view count mismatch"));
                }

                return callable::make_chain(std::move(ctx));
            };
        }

        /// @brief Helper function to get swapchain extent
        inline vk::Extent2D get_swapchain_extent(const CommVkSwapchainContext& ctx)
        {
            return ctx.swapchain_info_.extent_;
        }

        /// @brief Helper function to get swapchain format
        inline vk::Format get_swapchain_format(const CommVkSwapchainContext& ctx)
        {
            return ctx.swapchain_info_.surface_format_.format;
        }

        /// @brief Helper function to get image count
        inline uint32_t get_image_count(const CommVkSwapchainContext& ctx)
        {
            return static_cast<uint32_t>(ctx.swapchain_images_.size());
        }

        /// @brief Helper function to acquire next image
        inline vk::Result acquire_next_image(
            const CommVkSwapchainContext& ctx, uint64_t timeout, vk::Semaphore semaphore, vk::Fence fence, uint32_t* image_index)
        {
            return ctx.vk_logical_device_.acquireNextImageKHR(ctx.vk_swapchain_, timeout, semaphore, fence, image_index);
        }

    } // namespace swapchain

} // namespace templates::common

#endif // COMMON_H
