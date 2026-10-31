#include "archtype/renderer/pipeline.h"
#include "renderer/renderer_internal.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Enum conversion                                                           */
/* ------------------------------------------------------------------------ */

static VkPrimitiveTopology to_vk_topology(at_primitive_topology topology) {
    switch (topology) {
        case AT_PRIMITIVE_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case AT_PRIMITIVE_LINES:          return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case AT_PRIMITIVE_LINE_STRIP:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case AT_PRIMITIVE_POINTS:         return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case AT_PRIMITIVE_TRIANGLES:
        default:                          return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static VkCullModeFlags to_vk_cull_mode(at_cull_mode mode) {
    switch (mode) {
        case AT_CULL_BACK:  return VK_CULL_MODE_BACK_BIT;
        case AT_CULL_FRONT: return VK_CULL_MODE_FRONT_BIT;
        case AT_CULL_NONE:
        default:            return VK_CULL_MODE_NONE;
    }
}

static VkFrontFace to_vk_front_face(at_front_face face) {
    return face == AT_FRONT_FACE_CW ? VK_FRONT_FACE_CLOCKWISE
                                    : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

static VkFormat to_vk_vertex_format(at_vertex_format format) {
    switch (format) {
        case AT_VERTEX_FORMAT_FLOAT2:      return VK_FORMAT_R32G32_SFLOAT;
        case AT_VERTEX_FORMAT_FLOAT3:      return VK_FORMAT_R32G32B32_SFLOAT;
        case AT_VERTEX_FORMAT_FLOAT4:      return VK_FORMAT_R32G32B32A32_SFLOAT;
        case AT_VERTEX_FORMAT_UBYTE4_NORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case AT_VERTEX_FORMAT_FLOAT:
        default:                           return VK_FORMAT_R32_SFLOAT;
    }
}

static VkShaderStageFlags to_vk_stages(uint32_t stages) {
    if (stages == 0) {
        return VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkShaderStageFlags flags = 0;
    if (stages & AT_SHADER_STAGE_VERTEX) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (stages & AT_SHADER_STAGE_FRAGMENT) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    return flags;
}

static VkDescriptorType to_vk_descriptor_type(at_binding_type type) {
    switch (type) {
        case AT_BINDING_UNIFORM_BUFFER:
        default: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

static VkPipelineColorBlendAttachmentState make_blend_state(at_blend_mode mode) {
    VkPipelineColorBlendAttachmentState state;
    memset(&state, 0, sizeof(state));
    state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    switch (mode) {
        case AT_BLEND_ALPHA:
            /* color = src * src.a + dst * (1 - src.a) */
            state.blendEnable         = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            state.colorBlendOp        = VK_BLEND_OP_ADD;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            state.alphaBlendOp        = VK_BLEND_OP_ADD;
            break;

        case AT_BLEND_ADDITIVE:
            /* color = src * src.a + dst */
            state.blendEnable         = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            state.colorBlendOp        = VK_BLEND_OP_ADD;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.alphaBlendOp        = VK_BLEND_OP_ADD;
            break;

        case AT_BLEND_NONE:
        default:
            state.blendEnable = VK_FALSE;
            break;
    }

    return state;
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

at_pipeline *at_pipeline_create(at_renderer *renderer, const at_pipeline_desc *desc) {
    if (!renderer || !desc || !desc->vertex_shader || !desc->fragment_shader) {
        fprintf(stderr, "[archtype] Invalid pipeline description "
                        "(vertex and fragment shaders are required)\n");
        return NULL;
    }

    const at_vertex_layout *layout = &desc->vertex_layout;
    if (layout->attribute_count > AT_MAX_VERTEX_ATTRIBUTES ||
        (layout->attribute_count > 0 && layout->stride == 0)) {
        fprintf(stderr, "[archtype] Invalid vertex layout (stride is 0 or too many "
                        "attributes)\n");
        return NULL;
    }

    if (desc->binding_count > AT_MAX_BINDINGS) {
        fprintf(stderr, "[archtype] Too many pipeline bindings (max %d)\n", AT_MAX_BINDINGS);
        return NULL;
    }
    for (uint32_t i = 0; i < desc->binding_count; ++i) {
        if (desc->bindings[i].binding >= AT_MAX_BINDINGS) {
            fprintf(stderr, "[archtype] Binding %u is out of range (max %d)\n",
                    desc->bindings[i].binding, AT_MAX_BINDINGS - 1);
            return NULL;
        }
    }

    if (desc->push_constant_size > AT_MAX_PUSH_CONSTANTS || desc->push_constant_size % 4 != 0) {
        fprintf(stderr, "[archtype] push_constant_size must be a multiple of 4, up to %d\n",
                AT_MAX_PUSH_CONSTANTS);
        return NULL;
    }

    at_pipeline *pipeline = (at_pipeline *)calloc(1, sizeof(*pipeline));
    if (!pipeline) {
        fprintf(stderr, "[archtype] Failed to allocate pipeline\n");
        return NULL;
    }

    VkDevice device = renderer->context.device;
    VkResult result = VK_SUCCESS;

    pipeline->binding_count      = desc->binding_count;
    pipeline->push_constant_size = desc->push_constant_size;
    memcpy(pipeline->bindings, desc->bindings, sizeof(pipeline->bindings));

    /* Descriptor set layout: the resources the shaders read in set 0 */
    if (desc->binding_count > 0) {
        VkDescriptorSetLayoutBinding set_bindings[AT_MAX_BINDINGS];
        memset(set_bindings, 0, sizeof(set_bindings));

        for (uint32_t i = 0; i < desc->binding_count; ++i) {
            set_bindings[i].binding         = desc->bindings[i].binding;
            set_bindings[i].descriptorType  = to_vk_descriptor_type(desc->bindings[i].type);
            set_bindings[i].descriptorCount = 1;
            set_bindings[i].stageFlags      = to_vk_stages(desc->bindings[i].stages);
        }

        VkDescriptorSetLayoutCreateInfo set_info;
        memset(&set_info, 0, sizeof(set_info));
        set_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set_info.bindingCount = desc->binding_count;
        set_info.pBindings    = set_bindings;

        result = vkCreateDescriptorSetLayout(device, &set_info, NULL, &pipeline->set_layout);
        if (result != VK_SUCCESS) {
            AT_VK_LOG_ERROR("vkCreateDescriptorSetLayout", result);
            free(pipeline);
            return NULL;
        }
    }

    /* Pipeline layout: descriptor set 0 and the push constant block */
    VkPushConstantRange push_range;
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset     = 0;
    push_range.size       = desc->push_constant_size;

    VkPipelineLayoutCreateInfo layout_info;
    memset(&layout_info, 0, sizeof(layout_info));
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (pipeline->set_layout != VK_NULL_HANDLE) {
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts    = &pipeline->set_layout;
    }
    if (desc->push_constant_size > 0) {
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges    = &push_range;
    }

    result = vkCreatePipelineLayout(device, &layout_info, NULL, &pipeline->layout);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreatePipelineLayout", result);
        if (pipeline->set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, pipeline->set_layout, NULL);
        }
        free(pipeline);
        return NULL;
    }

    /* Shader stages */
    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = desc->vertex_shader->module;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = desc->fragment_shader->module;
    stages[1].pName  = "main";

    /* Vertex input: how to read the vertex buffer (binding 0).     */
    /* With no attributes, the shader builds its vertices itself.   */
    VkVertexInputBindingDescription   binding;
    VkVertexInputAttributeDescription attributes[AT_MAX_VERTEX_ATTRIBUTES];

    memset(&binding, 0, sizeof(binding));
    binding.binding   = 0;
    binding.stride    = layout->stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    for (uint32_t i = 0; i < layout->attribute_count; ++i) {
        attributes[i].location = layout->attributes[i].location;
        attributes[i].binding  = 0;
        attributes[i].format   = to_vk_vertex_format(layout->attributes[i].format);
        attributes[i].offset   = layout->attributes[i].offset;
    }

    VkPipelineVertexInputStateCreateInfo vertex_input;
    memset(&vertex_input, 0, sizeof(vertex_input));
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (layout->attribute_count > 0) {
        vertex_input.vertexBindingDescriptionCount   = 1;
        vertex_input.pVertexBindingDescriptions      = &binding;
        vertex_input.vertexAttributeDescriptionCount = layout->attribute_count;
        vertex_input.pVertexAttributeDescriptions    = attributes;
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly;
    memset(&input_assembly, 0, sizeof(input_assembly));
    input_assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = to_vk_topology(desc->topology);

    /* Viewport and scissor are set every frame (dynamic state) */
    VkPipelineViewportStateCreateInfo viewport_state;
    memset(&viewport_state, 0, sizeof(viewport_state));
    viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterization;
    memset(&rasterization, 0, sizeof(rasterization));
    rasterization.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode    = to_vk_cull_mode(desc->cull_mode);
    rasterization.frontFace   = to_vk_front_face(desc->front_face);
    rasterization.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample;
    memset(&multisample, 0, sizeof(multisample));
    multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    /* Ignored while the renderer has no depth buffer */
    VkPipelineDepthStencilStateCreateInfo depth_stencil;
    memset(&depth_stencil, 0, sizeof(depth_stencil));
    depth_stencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable  = desc->depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = desc->depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blend_attachment = make_blend_state(desc->blend_mode);

    VkPipelineColorBlendStateCreateInfo color_blend;
    memset(&color_blend, 0, sizeof(color_blend));
    color_blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments    = &blend_attachment;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamic_state;
    memset(&dynamic_state, 0, sizeof(dynamic_state));
    dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates    = dynamic_states;

    /* Dynamic rendering: instead of a render pass, the pipeline just */
    /* declares the formats of the images it draws into.             */
    VkFormat color_format = renderer->swapchain.format;

    VkPipelineRenderingCreateInfo rendering_info;
    memset(&rendering_info, 0, sizeof(rendering_info));
    rendering_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount    = 1;
    rendering_info.pColorAttachmentFormats = &color_format;
    rendering_info.depthAttachmentFormat   = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipeline_info;
    memset(&pipeline_info, 0, sizeof(pipeline_info));
    pipeline_info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.pNext               = &rendering_info;
    pipeline_info.stageCount          = 2;
    pipeline_info.pStages             = stages;
    pipeline_info.pVertexInputState   = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState      = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState   = &multisample;
    pipeline_info.pDepthStencilState  = &depth_stencil;
    pipeline_info.pColorBlendState    = &color_blend;
    pipeline_info.pDynamicState       = &dynamic_state;
    pipeline_info.layout              = pipeline->layout;
    pipeline_info.renderPass          = VK_NULL_HANDLE; /* Dynamic rendering */

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL,
                                       &pipeline->handle);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateGraphicsPipelines", result);
        vkDestroyPipelineLayout(device, pipeline->layout, NULL);
        if (pipeline->set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, pipeline->set_layout, NULL);
        }
        free(pipeline);
        return NULL;
    }

    return pipeline;
}

void at_pipeline_destroy(at_renderer *renderer, at_pipeline *pipeline) {
    if (!renderer || !pipeline) {
        return;
    }

    VkDevice device = renderer->context.device;

    /* The GPU may still be drawing with this pipeline */
    vkDeviceWaitIdle(device);

    vkDestroyPipeline(device, pipeline->handle, NULL);
    vkDestroyPipelineLayout(device, pipeline->layout, NULL);
    if (pipeline->set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, pipeline->set_layout, NULL);
    }
    free(pipeline);
}