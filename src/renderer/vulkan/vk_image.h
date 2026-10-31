#ifndef ARCHTYPE_VK_IMAGE_H
#define ARCHTYPE_VK_IMAGE_H

/* GPU images allocated with VMA. Not part of the public API. */

#include "renderer/vulkan/vk_allocator.h"
#include "renderer/vulkan/vk_upload.h"

typedef struct at_vk_image {
    VkImage       handle;
    VmaAllocation allocation;
    VkImageView   view;
    VkFormat      format;
    VkExtent2D    extent;
} at_vk_image;

/* Creates a 2D image in fast GPU memory, plus a view of all of it. */
bool at_vk_image_create(at_vk_image         *image,
                        VmaAllocator         allocator,
                        const at_vk_context *context,
                        uint32_t             width,
                        uint32_t             height,
                        VkFormat             format,
                        VkImageUsageFlags    usage);

/* Destroys the view and the image (safe to call twice). */
void at_vk_image_destroy(at_vk_image *image, VmaAllocator allocator, const at_vk_context *context);

/* Copies tightly packed pixels (4 bytes each) into the whole image through */
/* a staging buffer, then leaves it ready to be sampled by shaders.         */
/* Waits until the copy is done.                                            */
bool at_vk_image_upload(at_vk_image         *image,
                        VmaAllocator         allocator,
                        at_vk_upload        *upload,
                        const at_vk_context *context,
                        const void          *pixels);

#endif /* ARCHTYPE_VK_IMAGE_H */