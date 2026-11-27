#include "renderer/vulkan/vk_image.h"

#include <string.h>

#include "renderer/vulkan/vk_buffer.h"

#define AT_VK_BYTES_PER_PIXEL 4

/* ------------------------------------------------------------------------ */
/* Formats                                                                   */
/* ------------------------------------------------------------------------ */

bool at_vk_format_has_depth(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

bool at_vk_format_has_stencil(VkFormat format) {
    switch (format) {
        case VK_FORMAT_S8_UINT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

VkImageAspectFlags at_vk_format_aspect(VkFormat format) {
    VkImageAspectFlags aspect = 0;
    if (at_vk_format_has_depth(format)) {
        aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (at_vk_format_has_stencil(format)) {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return aspect != 0 ? aspect : VK_IMAGE_ASPECT_COLOR_BIT;
}

VkFormat at_vk_choose_depth_format(const at_vk_context *context) {
    /* Every GPU supports at least one of these as a depth attachment */
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_X8_D24_UNORM_PACK32,
        VK_FORMAT_D16_UNORM,
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(context->physical_device, candidates[i],
                                            &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return candidates[i];
        }
    }

    fprintf(stderr, "[archtype] The GPU supports no depth buffer format\n");
    return VK_FORMAT_UNDEFINED;
}

uint32_t at_vk_mip_level_count(uint32_t width, uint32_t height) {
    uint32_t size   = width > height ? width : height;
    uint32_t levels = 1;
    while (size > 1) {
        size >>= 1;
        ++levels;
    }
    return levels;
}

bool at_vk_format_supports_mipmaps(const at_vk_context *context, VkFormat format) {
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(context->physical_device, format, &properties);

    const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT
                                      | VK_FORMAT_FEATURE_BLIT_DST_BIT
                                      | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures & needed) == needed;
}

/* ------------------------------------------------------------------------ */
/* Images                                                                    */
/* ------------------------------------------------------------------------ */

/* Changes the layout of level_count mip levels of a color image, from */
/* base_level, and orders the work before and after it.               */
static void transition_levels(VkCommandBuffer       command_buffer,
                              VkImage               image,
                              uint32_t              base_level,
                              uint32_t              level_count,
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
    barrier.subresourceRange.baseMipLevel   = base_level;
    barrier.subresourceRange.levelCount     = level_count;
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
                        at_vk_allocator     *allocator,
                        const at_vk_context *context,
                        uint32_t             width,
                        uint32_t             height,
                        VkFormat             format,
                        VkImageUsageFlags    usage) {
    return at_vk_image_create_ex(image, allocator, context, width, height, format, usage, 1);
}

bool at_vk_image_create_ex(at_vk_image         *image,
                           at_vk_allocator     *allocator,
                           const at_vk_context *context,
                           uint32_t             width,
                           uint32_t             height,
                           VkFormat             format,
                           VkImageUsageFlags    usage,
                           uint32_t             mip_levels) {
    memset(image, 0, sizeof(*image));
    if (mip_levels == 0) {
        mip_levels = 1;
    }

    VkImageCreateInfo image_info;
    memset(&image_info, 0, sizeof(image_info));
    image_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.format        = format;
    image_info.extent.width  = width;
    image_info.extent.height = height;
    image_info.extent.depth  = 1;
    image_info.mipLevels     = mip_levels;
    image_info.arrayLayers   = 1;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL; /* GPU-friendly layout */
    image_info.usage         = usage;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(context->device, &image_info, NULL, &image->handle);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateImage", result);
        memset(image, 0, sizeof(*image));
        return false;
    }

    if (!at_vk_allocator_alloc_image(allocator, image->handle, AT_VK_MEMORY_GPU,
                                     &image->allocation)) {
        vkDestroyImage(context->device, image->handle, NULL);
        memset(image, 0, sizeof(*image));
        return false;
    }

    image->format        = format;
    image->extent.width  = width;
    image->extent.height = height;
    image->mip_levels    = mip_levels;

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
    view_info.subresourceRange.aspectMask     = at_vk_format_aspect(format);
    view_info.subresourceRange.baseMipLevel   = 0;
    view_info.subresourceRange.levelCount     = mip_levels;
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

void at_vk_image_destroy(at_vk_image         *image,
                         at_vk_allocator     *allocator,
                         const at_vk_context *context) {
    if (image->view != VK_NULL_HANDLE) {
        vkDestroyImageView(context->device, image->view, NULL);
    }
    if (image->handle != VK_NULL_HANDLE) {
        vkDestroyImage(context->device, image->handle, NULL);
    }
    at_vk_allocator_free(allocator, &image->allocation);
    memset(image, 0, sizeof(*image));
}

/* Level 0 holds the pixels and every level is in TRANSFER_DST. Each level */
/* is made by shrinking the one before it to half its size; once a level  */
/* has been read for the next one, it is made readable by shaders.        */
static void generate_mipmaps(VkCommandBuffer command_buffer, const at_vk_image *image) {
    const VkPipelineStageFlags2 shader_stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                                              | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;

    int32_t width  = (int32_t)image->extent.width;
    int32_t height = (int32_t)image->extent.height;

    for (uint32_t level = 1; level < image->mip_levels; ++level) {
        int32_t next_width  = width > 1 ? width / 2 : 1;
        int32_t next_height = height > 1 ? height / 2 : 1;

        /* The level before is complete: read it */
        transition_levels(command_buffer, image->handle, level - 1, 1,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        VkImageBlit blit;
        memset(&blit, 0, sizeof(blit));
        blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel       = level - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount     = 1;
        blit.srcOffsets[1].x               = width;
        blit.srcOffsets[1].y               = height;
        blit.srcOffsets[1].z               = 1;
        blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel       = level;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount     = 1;
        blit.dstOffsets[1].x               = next_width;
        blit.dstOffsets[1].y               = next_height;
        blit.dstOffsets[1].z               = 1;

        vkCmdBlitImage(command_buffer,
                       image->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       image->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit, VK_FILTER_LINEAR);

        /* The level before is done: shaders can read it */
        transition_levels(command_buffer, image->handle, level - 1, 1,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                          shader_stages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        width  = next_width;
        height = next_height;
    }

    /* The last level was only written (level 0 when there are no mipmaps) */
    transition_levels(command_buffer, image->handle, image->mip_levels - 1, 1,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      shader_stages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

bool at_vk_image_upload(at_vk_image         *image,
                        at_vk_allocator     *allocator,
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
    at_vk_buffer_flush(&staging, allocator, 0, VK_WHOLE_SIZE);

    VkCommandBuffer command_buffer = at_vk_upload_begin(upload, context);
    if (command_buffer == VK_NULL_HANDLE) {
        at_vk_buffer_destroy(&staging, allocator);
        return false;
    }

    /* 2. Make every level ready to receive a copy */
    transition_levels(command_buffer, image->handle, 0, image->mip_levels,
                      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_PIPELINE_STAGE_2_NONE, 0,
                      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    /* 3. Copy the pixels into level 0 */
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

    /* 4. Build the smaller levels and make them all readable by shaders */
    generate_mipmaps(command_buffer, image);

    bool ok = at_vk_upload_submit(upload, context);

    at_vk_buffer_destroy(&staging, allocator);
    return ok;
}