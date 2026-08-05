#include "vulkan_shader.h"

VulkanShaderHelper::~VulkanShaderHelper()
{
    // Destroy all shader modules
    for (auto& pair : shader_module_pairs_)
    {
        if (pair.second != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device_, pair.second, nullptr);
            pair.second = VK_NULL_HANDLE;
        }
    }
    shader_module_pairs_.clear();
}

bool VulkanShaderHelper::CreateShaderModule(VkDevice device, const std::vector<uint32_t>& shader_code, EShaderType shader_type)
{
    device_ = device; // Store the device reference

    // Create a shader module
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = shader_code.size() * sizeof(uint32_t);
    create_info.pCode = shader_code.data();

    // Check if the shader module already exists for the specified shader type
    if (shader_module_pairs_.find(shader_type) != shader_module_pairs_.end())
    {
        Logger::LogError("Shader module already exists for the specified shader type.");
        vkDestroyShaderModule(device, shader_module_pairs_[shader_type], nullptr);
        shader_module_pairs_.erase(shader_type);
    }

    // Create the shader module
    VkShaderModule returned_shader_module = VK_NULL_HANDLE;
    if (Logger::LogWithVkResult(vkCreateShaderModule(device, &create_info, nullptr, &returned_shader_module), 
                "Failed to create shader module", 
                "Succeeded in creating shader module"))
    {
        shader_module_pairs_[shader_type] = returned_shader_module; // Store the shader module in the map
        return true;
    }
    else
    { 
        return false;
    }
}

bool VulkanShaderHelper::ReadShaderCode(const char* filename, std::vector<uint32_t>& shader_code)
{
    // Open the file at the end to easily get the size
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        // Consider logging an error here if Logger is available globally or passed in
        Logger::LogError("Failed to open shader file: " + std::string(filename));
        return false;
    }

    // resize and read
    size_t file_size = (size_t)file.tellg();
    if (file_size % sizeof(uint32_t) != 0) {
        Logger::LogError("Shader file size is not a multiple of sizeof(uint32_t): " + std::string(filename));
        file.close();
        return false;
    }
    shader_code.resize(file_size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(shader_code.data()), file_size);

    // Check for read errors
    if (file.fail())
    {
        // Logger::LogError("Failed to read shader file: " + std::string(filename) + ", error state: " + std::to_string(file.rdstate()));
        file.close();
        shader_code.clear(); // Clear potentially partially read data
        return false;
    }

    file.close();
    return true;
}
