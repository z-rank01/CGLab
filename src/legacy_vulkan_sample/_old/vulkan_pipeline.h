#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include <vector>
#include <map>
#include <utility>
#include "vulkan_shader.h"

struct SVulkanPipelineConfig
{
    vk::Extent2D swap_chain_extent;
    std::map<EShaderType, vk::ShaderModule> shader_module_map;
    vk::RenderPass renderpass;
    vk::VertexInputBindingDescription vertex_input_binding_description;
    std::vector<vk::VertexInputAttributeDescription> vertex_input_attribute_descriptions;
    std::vector<vk::DescriptorSetLayout> descriptor_set_layouts;
};

class VulkanPipelineHelper
{
private:
    SVulkanPipelineConfig config_;
    vk::PipelineLayout pipeline_layout_;
    vk::Pipeline pipeline_;
    vk::Device device_;
public:
    VulkanPipelineHelper(SVulkanPipelineConfig config) : config_(std::move(config)) {}
    ~VulkanPipelineHelper();

    bool CreatePipeline(vk::Device device);
    vk::Pipeline GetPipeline() const { return pipeline_; }
    vk::PipelineLayout GetPipelineLayout() const { return pipeline_layout_; }
};