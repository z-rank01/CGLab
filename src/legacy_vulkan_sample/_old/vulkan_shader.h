#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <fstream>
#include <utility/logger.h>
#include <map>

enum class EShaderType
{
    kVertexShader,
    kFragmentShader,
    kComputeShader,
    kGeometryShader,
    kTessellationShader,
    kRayTracingShader
};

struct SVulkanShaderConfig
{
    EShaderType shader_type;
    const char* shader_path;
};

class VulkanShaderHelper
{
public:
    VulkanShaderHelper() = delete;
    VulkanShaderHelper(VkDevice device) : device_(device) {};
    ~VulkanShaderHelper();

    bool ReadShaderCode(const char* filename, std::vector<uint32_t>& shader_code);
    bool CreateShaderModule(VkDevice device, const std::vector<uint32_t>& shader_code, EShaderType shader_type);
    VkShaderModule GetShaderModule(EShaderType shader_type) const 
    {
        if (shader_module_pairs_.find(shader_type) == shader_module_pairs_.end())
        {
            Logger::LogError("Shader module not found for the specified shader type.");
            throw std::runtime_error("Shader module not found for the specified shader type.");
        } 
        return shader_module_pairs_.at(shader_type); 
    }

private:
    VkDevice device_;
    std::map<EShaderType, VkShaderModule> shader_module_pairs_;
};