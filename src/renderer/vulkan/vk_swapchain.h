
#ifndef ARCHTYPE_VK_SWAPCHAIN_H
#define ARCHTYPE_VK_SWAPCHAIN_H

/* Swapchain: the images the renderer draws into and shows on the window. */

#include "renderer/vulkan/vk_context.h"

typedef struct at_vk_swapchain {
    VkSwapchainKHR   handle;
    VkFormat         format;
    VkColorSpaceKHR  color_space;
    VkPresentModeKHR present_mode;
    VkExtent2D       extent;

    uint32_t         image_count;
    VkImage         *images;      /* Owned by the swapchain, not destroyed by us */
    VkImageView     *image_views; /* One per image, created and destroyed by us  */
} at_vk_swapchain;

/* Creates the swapchain and one image view per image.        */
/* width/height are the window framebuffer size in pixels.    */
bool at_vk_swapchain_create(at_vk_swapchain     *swapchain,
                            const at_vk_context *context,
                            uint32_t             width,
                            uint32_t             height,
                            bool                 vsync);

/* Destroys the image views and the swapchain (safe to call twice). */
void at_vk_swapchain_destroy(at_vk_swapchain *swapchain, const at_vk_context *context);

#endif /* ARCHTYPE_VK_SWAPCHAIN_H */