#ifndef ARCHTYPE_VK_IMAGE_H
#define ARCHTYPE_VK_IMAGE_H

/* GPU images, with memory from the engine's allocator. */
/* Not part of the public API.                          */

#include "renderer/vulkan/vk_allocator.h"
#include "renderer/vulkan/vk_upload.h"

typedef struct at_vk_image {
    VkImage          handle;
    at_vk_allocation allocation;
    VkImageView      view;
    VkFormat         format;
    VkExtent2D       extent;
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

/* Destroys the view and the image (safe to call twice). */
void at_vk_image_destroy(at_vk_image         *image,
                         at_vk_allocator     *allocator,
                         const at_vk_context *context);

/* Copies tightly packed pixels (4 bytes each) into the whole image through */
/* a staging buffer, then leaves it ready to be sampled by shaders.         */
/* Waits until the copy is done.                                            */
bool at_vk_image_upload(at_vk_image         *image,
                        at_vk_allocator     *allocator,
                        at_vk_upload        *upload,
                        const at_vk_context *context,
                        const void          *pixels);

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