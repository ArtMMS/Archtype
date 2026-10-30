#include "renderer/vulkan/vk_upload.h"

#include <string.h>

bool at_vk_upload_create(at_vk_upload *upload, const at_vk_context *context) {
    memset(upload, 0, sizeof(*upload));

    VkCommandPoolCreateInfo pool_info;
    memset(&pool_info, 0, sizeof(pool_info));
    pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context->graphics_queue_family;

    VkResult result = vkCreateCommandPool(context->device, &pool_info, NULL,
                                          &upload->command_pool);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateCommandPool", result);
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info;
    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = upload->command_pool;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    result = vkAllocateCommandBuffers(context->device, &alloc_info, &upload->command_buffer);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkAllocateCommandBuffers", result);
        at_vk_upload_destroy(upload, context);
        return false;
    }

    VkFenceCreateInfo fence_info;
    memset(&fence_info, 0, sizeof(fence_info));
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    result = vkCreateFence(context->device, &fence_info, NULL, &upload->fence);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateFence", result);
        at_vk_upload_destroy(upload, context);
        return false;
    }

    return true;
}

void at_vk_upload_destroy(at_vk_upload *upload, const at_vk_context *context) {
    if (upload->fence != VK_NULL_HANDLE) {
        vkDestroyFence(context->device, upload->fence, NULL);
    }
    if (upload->command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context->device, upload->command_pool, NULL);
    }
    memset(upload, 0, sizeof(*upload));
}

VkCommandBuffer at_vk_upload_begin(at_vk_upload *upload, const at_vk_context *context) {
    (void)context;

    vkResetCommandBuffer(upload->command_buffer, 0);

    VkCommandBufferBeginInfo begin_info;
    memset(&begin_info, 0, sizeof(begin_info));
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(upload->command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkBeginCommandBuffer", result);
        return VK_NULL_HANDLE;
    }

    return upload->command_buffer;
}

bool at_vk_upload_submit(at_vk_upload *upload, const at_vk_context *context) {
    VkResult result = vkEndCommandBuffer(upload->command_buffer);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkEndCommandBuffer", result);
        return false;
    }

    VkCommandBufferSubmitInfo command_info;
    memset(&command_info, 0, sizeof(command_info));
    command_info.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_info.commandBuffer = upload->command_buffer;

    VkSubmitInfo2 submit_info;
    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.sType                  = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos    = &command_info;

    result = vkQueueSubmit2(context->graphics_queue, 1, &submit_info, upload->fence);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkQueueSubmit2", result);
        return false;
    }

    vkWaitForFences(context->device, 1, &upload->fence, VK_TRUE, UINT64_MAX);
    vkResetFences(context->device, 1, &upload->fence);
    return true;
}