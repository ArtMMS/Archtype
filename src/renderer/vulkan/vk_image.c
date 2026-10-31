#include "renderer/vulkan/vk_image.h"

#include <string.h>

#include "renderer/vulkan/vk_buffer.h"

#define AT_VK_BYTES_PER_PIXEL 4

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

bool at_vk_image_create(at_vk_image         *image,
                        VmaAllocator         allocator,
                        const at_vk_context *context,
                        uint32_t             width,
                        uint32_t             height,
                        VkFormat             format,
                        VkImageUsageFlags    usage) {
    memset(image, 0, sizeof(*image));

    VkImageCreateInfo image_info;
    memset(&image_info, 0, sizeof(image_info));
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.format        = format;
    image_info.extent.width  = width;
    image_info.extent.height = height;
    image_info.extent.depth  = 1;
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL; /* GPU-friendly layout */
    image_info.usage         = usage;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info;
    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

    VkResult result = vmaCreateImage(allocator, &image_info, &alloc_info,
                                     &image->handle, &image->allocation, NULL);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vmaCreateImage", result);
        memset(image, 0, sizeof(*image));
        return false;
    }

    image->format        = format;
    image->extent.width  = width;
    image->extent.height = height;

    VkImageViewCreateInfo view_info;
    memset(&view_info, 0, sizeof(view_info));
    view_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image                           = image->handle;
    view_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format                          = format;
    view_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount     = 1;

    result = vkCreateImageView(context->device, &view_info, NULL, &image->view);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateImageView", result);
        at_vk_image_destroy(image, allocator, context);
        return false;
    }

    return true;
}

void at_vk_image_destroy(at_vk_image *image, VmaAllocator allocator, const at_vk_context *context) {
    if (image->view != VK_NULL_HANDLE) {
        vkDestroyImageView(context->device, image->view, NULL);
    }
    if (image->handle != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, image->handle, image->allocation);
    }
    memset(image, 0, sizeof(*image));
}

bool at_vk_image_upload(at_vk_image         *image,
                        VmaAllocator         allocator,
                        at_vk_upload        *upload,
                        const at_vk_context *context,
                        const void          *pixels) {
    VkDeviceSize size = (VkDeviceSize)image->extent.width * image->extent.height
                      * AT_VK_BYTES_PER_PIXEL;

    /* 1. Put the pixels in memory the CPU can write to */
    at_vk_buffer staging;
    if (!at_vk_buffer_create_host(&staging, allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
        return false;
    }
    memcpy(staging.mapped, pixels, (size_t)size);
    vmaFlushAllocation(allocator, staging.allocation, 0, VK_WHOLE_SIZE);

    VkCommandBuffer command_buffer = at_vk_upload_begin(upload, context);
    if (command_buffer == VK_NULL_HANDLE) {
        at_vk_buffer_destroy(&staging, allocator);
        return false;
    }

    /* 2. Make the image ready to receive a copy */
    transition_image(command_buffer, image->handle,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_NONE, 0,
                     VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    /* 3. Copy the pixels into the image */
    VkBufferImageCopy region;
    memset(&region, 0, sizeof(region));
    region.bufferOffset                    = 0;
    region.bufferRowLength                 = 0; /* 0 = rows are tightly packed */
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageExtent.width               = image->extent.width;
    region.imageExtent.height              = image->extent.height;
    region.imageExtent.depth               = 1;

    vkCmdCopyBufferToImage(command_buffer, staging.handle, image->handle,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    /* 4. Make the image readable by shaders */
    transition_image(command_buffer, image->handle,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    bool ok = at_vk_upload_submit(upload, context);

    at_vk_buffer_destroy(&staging, allocator);
    return ok;
}