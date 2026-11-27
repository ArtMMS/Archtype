#ifndef ARCHTYPE_VK_IMAGE_H
#define ARCHTYPE_VK_IMAGE_H

/* GPU images, with memory from the engine's allocator. */
/* Not part of the public API.                          */

#include "renderer/vulkan/vk_allocator.h"
#include "renderer/vulkan/vk_upload.h"

typedef struct at_vk_image {
    VkImage          handle;
    at_vk_allocation allocation;
    VkImageView      view;       /* Every mip level */
    VkFormat         format;
    VkExtent2D       extent;     /* Size of level 0 */
    uint32_t         mip_levels; /* 1 = no mipmaps  */
} at_vk_image;

/* Creates a 2D image in fast GPU memory, plus a view of all of it.   */
/* Depth formats get a depth view, so the same function makes depth  */
/* buffers (usage VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT).       */
bool at_vk_image_create(at_vk_image         *image,
                        at_vk_allocator     *allocator,
                        const at_vk_context *context,
                        uint32_t             width,
                        uint32_t             height,
                        VkFormat             format,
                        VkImageUsageFlags    usage);

/* Same, with mip_levels levels (0 is taken as 1). Images that will    */
/* generate their mipmaps need VK_IMAGE_USAGE_TRANSFER_SRC_BIT too.    */
bool at_vk_image_create_ex(at_vk_image         *image,
                           at_vk_allocator     *allocator,
                           const at_vk_context *context,
                           uint32_t             width,
                           uint32_t             height,
                           VkFormat             format,
                           VkImageUsageFlags    usage,
                           uint32_t             mip_levels);

/* Destroys the view and the image (safe to call twice). */
void at_vk_image_destroy(at_vk_image         *image,
                         at_vk_allocator     *allocator,
                         const at_vk_context *context);

/* Copies tightly packed pixels (4 bytes each) into level 0 through a     */
/* staging buffer, builds the other mip levels from it (each one half    */
/* the size of the one before, with linear filtering), then leaves every */
/* level ready to be sampled by shaders. Waits until it is done.         */
bool at_vk_image_upload(at_vk_image         *image,
                        at_vk_allocator     *allocator,
                        at_vk_upload        *upload,
                        const at_vk_context *context,
                        const void          *pixels);

/* Number of levels of a full mip chain: 1 + log2 of the biggest side, */
/* e.g. 256x64 -> 9 (256, 128, ... 1).                                 */
uint32_t at_vk_mip_level_count(uint32_t width, uint32_t height);

/* True if the GPU can build mipmaps of this format by blitting with   */
/* linear filtering (optimal tiling).                                  */
bool at_vk_format_supports_mipmaps(const at_vk_context *context, VkFormat format);

/* True for depth formats, with or without stencil. */
bool at_vk_format_has_depth(VkFormat format);

/* True for formats with a stencil part (e.g. D24_UNORM_S8_UINT). */
bool at_vk_format_has_stencil(VkFormat format);

/* Parts of an image of this format: color, or depth (+ stencil). */
VkImageAspectFlags at_vk_format_aspect(VkFormat format);

/* Best depth format the GPU can draw into: D32_SFLOAT when supported,  */
/* then formats with stencil, then D16. VK_FORMAT_UNDEFINED if none.    */
VkFormat at_vk_choose_depth_format(const at_vk_context *context);

#endif /* ARCHTYPE_VK_IMAGE_H */