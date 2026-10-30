#include "renderer/vulkan/vk_frame.h"

#include <string.h>

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

/* Changes an image's layout and orders the work before and after it. */
static void transition_image(VkCommandBuffer       command_buffer,
                             VkImage               image,
                             VkImageLayout         old_layout,
                             VkImageLayout         new_layout,
                             VkPipelineStageFlags2 src_stage,
                             VkAccessFlags2        src_access,
                             VkPipelineStageFlags2 dst_stage,
                             VkAccessFlags2        dst_access) {
    VkImageMemoryBarrier2 barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask                    = src_stage;
    barrier.srcAccessMask                   = src_access;
    barrier.dstStageMask                    = dst_stage;
    barrier.dstAccessMask                   = dst_access;
    barrier.oldLayout                       = old_layout;
    barrier.newLayout                       = new_layout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkDependencyInfo dependency;
    memset(&dependency, 0, sizeof(dependency));
    dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(command_buffer, &dependency);
}

/* ------------------------------------------------------------------------ */
/* Creation and destruction                                                  */
/* ------------------------------------------------------------------------ */

bool at_vk_frames_create(at_vk_frames *frames, const at_vk_context *context) {
    memset(frames, 0, sizeof(*frames));

    /* One pool for every frame; each command buffer is reset individually */
    VkCommandPoolCreateInfo pool_info;
    memset(&pool_info, 0, sizeof(pool_info));
    pool_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context->graphics_queue_family;

    VkResult result = vkCreateCommandPool(context->device, &pool_info, NULL,
                                          &frames->command_pool);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateCommandPool", result);
        return false;
    }

    VkCommandBuffer command_buffers[AT_VK_FRAMES_IN_FLIGHT];

    VkCommandBufferAllocateInfo alloc_info;
    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool        = frames->command_pool;
    alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = AT_VK_FRAMES_IN_FLIGHT;

    result = vkAllocateCommandBuffers(context->device, &alloc_info, command_buffers);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkAllocateCommandBuffers", result);
        at_vk_frames_destroy(frames, context);
        return false;
    }

    VkSemaphoreCreateInfo semaphore_info;
    memset(&semaphore_info, 0, sizeof(semaphore_info));
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    /* Fences start signaled so the first wait on each frame returns at once */
    VkFenceCreateInfo fence_info;
    memset(&fence_info, 0, sizeof(fence_info));
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        at_vk_frame *frame    = &frames->frames[i];
        frame->command_buffer = command_buffers[i];

        result = vkCreateSemaphore(context->device, &semaphore_info, NULL,
                                   &frame->image_available);
        if (result != VK_SUCCESS) {
            AT_VK_LOG_ERROR("vkCreateSemaphore", result);
            at_vk_frames_destroy(frames, context);
            return false;
        }

        result = vkCreateFence(context->device, &fence_info, NULL, &frame->in_flight);
        if (result != VK_SUCCESS) {
            AT_VK_LOG_ERROR("vkCreateFence", result);
            at_vk_frames_destroy(frames, context);
            return false;
        }
    }

    fprintf(stderr, "[archtype] Frame resources created (%d frames in flight)\n",
            AT_VK_FRAMES_IN_FLIGHT);
    return true;
}

void at_vk_frames_destroy(at_vk_frames *frames, const at_vk_context *context) {
    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        at_vk_frame *frame = &frames->frames[i];

        if (frame->in_flight != VK_NULL_HANDLE) {
            vkDestroyFence(context->device, frame->in_flight, NULL);
        }
        if (frame->image_available != VK_NULL_HANDLE) {
            vkDestroySemaphore(context->device, frame->image_available, NULL);
        }
    }

    /* Destroying the pool also frees its command buffers */
    if (frames->command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(context->device, frames->command_pool, NULL);
    }

    memset(frames, 0, sizeof(*frames));
}

/* ------------------------------------------------------------------------ */
/* Frame flow                                                                */
/* ------------------------------------------------------------------------ */

bool at_vk_frame_begin(at_vk_frames           *frames,
                       const at_vk_context    *context,
                       const at_vk_swapchain  *swapchain,
                       const float             clear_color[4]) {
    at_vk_frame *frame = &frames->frames[frames->current];

    /* 1. Wait until the GPU is done with the last use of this frame slot */
    vkWaitForFences(context->device, 1, &frame->in_flight, VK_TRUE, UINT64_MAX);

    /* 2. Get the next swapchain image to draw into */
    VkResult result = vkAcquireNextImageKHR(context->device, swapchain->handle, UINT64_MAX,
                                            frame->image_available, VK_NULL_HANDLE,
                                            &frames->image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        frames->out_of_date = true;
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        frames->out_of_date = true; /* Still usable: draw now, recreate later */
    } else if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkAcquireNextImageKHR", result);
        return false;
    }

    /* Only reset the fence once we know we will submit work this frame, */
    /* otherwise the next wait on it would block forever.                */
    vkResetFences(context->device, 1, &frame->in_flight);

    /* 3. Start recording commands */
    VkCommandBuffer command_buffer = frame->command_buffer;
    vkResetCommandBuffer(command_buffer, 0);

    VkCommandBufferBeginInfo begin_info;
    memset(&begin_info, 0, sizeof(begin_info));
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkBeginCommandBuffer", result);
        return false;
    }

    VkImage image = swapchain->images[frames->image_index];

    /* 4. Make the image ready to be drawn into. The old contents don't   */
    /*    matter (UNDEFINED) because the whole image is cleared next.     */
    transition_image(command_buffer, image,
                     VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    /* 5. Begin dynamic rendering: clears the image with clear_color */
    VkRenderingAttachmentInfo color_attachment;
    memset(&color_attachment, 0, sizeof(color_attachment));
    color_attachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment.imageView   = swapchain->image_views[frames->image_index];
    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clearValue.color.float32[0] = clear_color[0];
    color_attachment.clearValue.color.float32[1] = clear_color[1];
    color_attachment.clearValue.color.float32[2] = clear_color[2];
    color_attachment.clearValue.color.float32[3] = clear_color[3];

    VkRenderingInfo rendering_info;
    memset(&rendering_info, 0, sizeof(rendering_info));
    rendering_info.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering_info.renderArea.offset.x  = 0;
    rendering_info.renderArea.offset.y  = 0;
    rendering_info.renderArea.extent    = swapchain->extent;
    rendering_info.layerCount           = 1;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachments    = &color_attachment;

    vkCmdBeginRendering(command_buffer, &rendering_info);

    /* Draw commands will be recorded between begin and end frame */
    return true;
}

void at_vk_frame_end(at_vk_frames          *frames,
                     const at_vk_context   *context,
                     const at_vk_swapchain *swapchain) {
    at_vk_frame    *frame          = &frames->frames[frames->current];
    VkCommandBuffer command_buffer = frame->command_buffer;
    VkImage         image          = swapchain->images[frames->image_index];
    VkSemaphore     render_finished = swapchain->render_finished[frames->image_index];

    /* 1. Finish rendering and make the image ready to be shown */
    vkCmdEndRendering(command_buffer);

    transition_image(command_buffer, image,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_NONE, 0);

    VkResult result = vkEndCommandBuffer(command_buffer);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkEndCommandBuffer", result);
        return;
    }

    /* 2. Submit: wait for the image to be acquired before writing colors, */
    /*    signal render_finished when done, and the fence for the CPU.     */
    VkSemaphoreSubmitInfo wait_info;
    memset(&wait_info, 0, sizeof(wait_info));
    wait_info.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = frame->image_available;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signal_info;
    memset(&signal_info, 0, sizeof(signal_info));
    signal_info.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = render_finished;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo command_info;
    memset(&command_info, 0, sizeof(command_info));
    command_info.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command_info.commandBuffer = command_buffer;

    VkSubmitInfo2 submit_info;
    memset(&submit_info, 0, sizeof(submit_info));
    submit_info.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.waitSemaphoreInfoCount   = 1;
    submit_info.pWaitSemaphoreInfos      = &wait_info;
    submit_info.commandBufferInfoCount   = 1;
    submit_info.pCommandBufferInfos      = &command_info;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos    = &signal_info;

    result = vkQueueSubmit2(context->graphics_queue, 1, &submit_info, frame->in_flight);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkQueueSubmit2", result);
        return;
    }

    /* 3. Present: show the image once rendering finished */
    VkPresentInfoKHR present_info;
    memset(&present_info, 0, sizeof(present_info));
    present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores    = &render_finished;
    present_info.swapchainCount     = 1;
    present_info.pSwapchains        = &swapchain->handle;
    present_info.pImageIndices      = &frames->image_index;

    result = vkQueuePresentKHR(context->present_queue, &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        frames->out_of_date = true;
    } else if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkQueuePresentKHR", result);
    }

    /* 4. Move on to the next frame slot */
    frames->current = (frames->current + 1) % AT_VK_FRAMES_IN_FLIGHT;
}