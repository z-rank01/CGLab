#include "vulkan_pipeline.h"
#include "utility/logger.h"

VulkanPipelineHelper::~VulkanPipelineHelper()
{
    if (pipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }
}

bool VulkanPipelineHelper::CreatePipeline(vk::Device device)
{
    device_ = device;
    
    // input assembly
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly
    {
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False
    };

    // vertex input
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo
    {
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &config_.vertex_input_binding_description,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(config_.vertex_input_attribute_descriptions.size()),
        .pVertexAttributeDescriptions = config_.vertex_input_attribute_descriptions.data()
    };

    // viewport and scissor
    vk::Viewport viewport
    {
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(config_.swap_chain_extent.width),
        .height = static_cast<float>(config_.swap_chain_extent.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F
    };

    vk::Rect2D scissor
    {
        .offset = { .x=0, .y=0 },
        .extent = config_.swap_chain_extent
    };

    vk::PipelineViewportStateCreateInfo viewportState
    {
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor
    };

    // rasterizer
    vk::PipelineRasterizationStateCreateInfo rasterizer
    {
        .depthClampEnable = vk::False,                  // Disable depth clamping
        .rasterizerDiscardEnable = vk::False,           // Disable if you want to draw
        .polygonMode = vk::PolygonMode::eFill,          // Fill the polygon
        .cullMode = vk::CullModeFlagBits::eBack,        // Cull back faces
        .frontFace = vk::FrontFace::eCounterClockwise,  // 或者尝试 VK_FRONT_FACE_CLOCKWISE
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0F
    };

    // multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling
    {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,    // 1 sample per pixel
        .sampleShadingEnable = vk::False, 
        .minSampleShading = 1.0F,                               // Optional
        .pSampleMask = nullptr,                                 // Optional
        .alphaToCoverageEnable = vk::False,                     // Optional
        .alphaToOneEnable = vk::False                           // Optional
    };

    // depth and stencil testing
    vk::PipelineDepthStencilStateCreateInfo depthStencil
    {
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0F,
        .maxDepthBounds = 1.0F,
    };
    
    
    // color blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment
    {
        .blendEnable = vk::False,
        .srcColorBlendFactor = vk::BlendFactor::eOne,
        .dstColorBlendFactor = vk::BlendFactor::eZero,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd, 
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | 
                         vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };

    vk::PipelineColorBlendStateCreateInfo colorBlending
    {
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = std::array<float, 4>{ 0.0F, 0.0F, 0.0F, 0.0F }
    };

    // pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo
    {
        .setLayoutCount = static_cast<uint32_t>(config_.descriptor_set_layouts.size()),
        .pSetLayouts = config_.descriptor_set_layouts.data(),
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr
    };

    pipeline_layout_ = device_.createPipelineLayout(pipelineLayoutInfo);
    if (!pipeline_layout_)
    {
        return false;
    }

    // shader stages
    std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages;
    
    // 获取顶点着色器模块
    auto it_vert = config_.shader_module_map.find(EShaderType::kVertexShader);
    if (it_vert == config_.shader_module_map.end()) {
        Logger::LogError("Vertex shader module not found in pipeline config map.");
        return false;
    }
    shader_stages[0] = vk::PipelineShaderStageCreateInfo
    {
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = it_vert->second,
        .pName = "main",
        .pSpecializationInfo = nullptr
    };

    // 获取片段着色器模块
    auto it_frag = config_.shader_module_map.find(EShaderType::kFragmentShader);
    if (it_frag == config_.shader_module_map.end()) {
        Logger::LogError("Fragment shader module not found in pipeline config map.");
        return false;
    }
    shader_stages[1] = vk::PipelineShaderStageCreateInfo
    {
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = it_frag->second,
        .pName = "main",
        .pSpecializationInfo = nullptr
    };

    // dynamic state
    std::array<vk::DynamicState, 2> dynamicStates =
    {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState
    {
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };

    // graphics pipeline create info
    vk::GraphicsPipelineCreateInfo pipelineInfo
    {
        .stageCount = static_cast<uint32_t>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = pipeline_layout_,
        .renderPass = config_.renderpass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };

    auto pipelineResult = device_.createGraphicsPipeline(VK_NULL_HANDLE, pipelineInfo);
    if (!(pipelineResult.result == vk::Result::eSuccess)) 
    {
        Logger::LogError("Failed to create graphics pipeline");
        return false;
    }
    pipeline_ = pipelineResult.value;
    
    return true;
}