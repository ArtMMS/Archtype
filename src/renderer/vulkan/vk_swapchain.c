#include "renderer/vulkan/vk_swapchain.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static const char *format_string(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_SRGB:  return "B8G8R8A8_SRGB";
        case VK_FORMAT_R8G8B8A8_SRGB:  return "R8G8B8A8_SRGB";
        case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        default:                       return "other";
    }
}

static const char *present_mode_string(VkPresentModeKHR mode) {
    switch (mode) {
        case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO (vsync)";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO relaxed";
        case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX (no vsync, no tearing)";
        case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE (no vsync)";
        default:                               return "other";
    }
}

static uint32_t clamp_u32(uint32_t value, uint32_t min, uint32_t max) {
    return value < min ? min : (value > max ? max : value);
}

/* Prefers 8-bit sRGB so colors are shown correctly without manual conversion. */
static VkSurfaceFormatKHR choose_surface_format(const at_vk_context *context) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device, context->surface,
                                         &count, NULL);

    VkSurfaceFormatKHR fallback;
    fallback.format     = VK_FORMAT_B8G8R8A8_SRGB;
    fallback.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    if (count == 0) {
        return fallback;
    }

    VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)malloc(count * sizeof(*formats));
    if (!formats) {
        return fallback;
    }
    vkGetPhysicalDeviceSurfaceFormatsKHR(context->physical_device, context->surface,
                                         &count, formats);

    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < count; ++i) {
        bool srgb_space = formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        if (srgb_space && (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB ||
                           formats[i].format == VK_FORMAT_R8G8B8A8_SRGB)) {
            chosen = formats[i];
            break;
        }
    }

    free(formats);
    return chosen;
}

/* vsync on:  FIFO (always available).                                  */
/* vsync off: MAILBOX if available, then IMMEDIATE, then FIFO.          */
static VkPresentModeKHR choose_present_mode(const at_vk_context *context, bool vsync) {
    if (vsync) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device, context->surface,
                                              &count, NULL);
    if (count == 0) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkPresentModeKHR *modes = (VkPresentModeKHR *)malloc(count * sizeof(*modes));
    if (!modes) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(context->physical_device, context->surface,
                                              &count, modes);

    bool has_mailbox   = false;
    bool has_immediate = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            has_mailbox = true;
        } else if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            has_immediate = true;
        }
    }

    free(modes);

    if (has_mailbox) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    if (has_immediate) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

/* Most platforms dictate the size (currentExtent). When they don't       */
/* (width = UINT32_MAX), we use the window size clamped to what's allowed. */
static VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR *caps,
                                uint32_t width, uint32_t height) {
    if (caps->currentExtent.width != UINT32_MAX) {
        return caps->currentExtent;
    }

    VkExtent2D extent;
    extent.width  = clamp_u32(width,  caps->minImageExtent.width,  caps->maxImageExtent.width);
    extent.height = clamp_u32(height, caps->minImageExtent.height, caps->maxImageExtent.height);
    return extent;
}

/* One more than the minimum, so we don't wait on the driver for a free image. */
static uint32_t choose_image_count(const VkSurfaceCapabilitiesKHR *caps) {
    uint32_t count = caps->minImageCount + 1;
    if (caps->maxImageCount > 0 && count > caps->maxImageCount) { /* 0 = no limit */
        count = caps->maxImageCount;
    }
    return count;
}

static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(const VkSurfaceCapabilitiesKHR *caps) {
    const VkCompositeAlphaFlagBitsKHR preferred[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    };

    for (size_t i = 0; i < sizeof(preferred) / sizeof(preferred[0]); ++i) {
        if (caps->supportedCompositeAlpha & (VkCompositeAlphaFlagsKHR)preferred[i]) {
            return preferred[i];
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static bool create_image_views(at_vk_swapchain *swapchain, const at_vk_context *context) {
    swapchain->image_views =
        (VkImageView *)calloc(swapchain->image_count, sizeof(*swapchain->image_views));
    if (!swapchain->image_views) {
        fprintf(stderr, "[archtype] Failed to allocate swapchain image views\n");
        return false;
    }

    for (uint32_t i = 0; i < swapchain->image_count; ++i) {
        VkImageViewCreateInfo info;
        memset(&info, 0, sizeof(info));
        info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image                           = swapchain->images[i];
        info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        info.format                          = swapchain->format;
        info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
        info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel   = 0;
        info.subresourceRange.levelCount     = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount     = 1;

        VkResult result = vkCreateImageView(context->device, &info, NULL,
                                            &swapchain->image_views[i]);
        if (result != VK_SUCCESS) {
            AT_VK_LOG_ERROR("vkCreateImageView", result);
            return false;
        }
    }

    return true;
}

static bool create_semaphores(at_vk_swapchain *swapchain, const at_vk_context *context) {
    swapchain->render_finished =
        (VkSemaphore *)calloc(swapchain->image_count, sizeof(*swapchain->render_finished));
    if (!swapchain->render_finished) {
        fprintf(stderr, "[archtype] Failed to allocate swapchain semaphores\n");
        return false;
    }

    VkSemaphoreCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < swapchain->image_count; ++i) {
        VkResult result = vkCreateSemaphore(context->device, &info, NULL,
                                            &swapchain->render_finished[i]);
        if (result != VK_SUCCESS) {
            AT_VK_LOG_ERROR("vkCreateSemaphore", result);
            return false;
        }
    }

    return true;
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

bool at_vk_swapchain_create(at_vk_swapchain     *swapchain,
                            const at_vk_context *context,
                            uint32_t             width,
                            uint32_t             height,
                            bool                 vsync) {
    memset(swapchain, 0, sizeof(*swapchain));

    VkSurfaceCapabilitiesKHR caps;
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device,
                                                                context->surface, &caps);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
        return false;
    }

    VkSurfaceFormatKHR surface_format = choose_surface_format(context);
    VkPresentModeKHR   present_mode   = choose_present_mode(context, vsync);
    VkExtent2D         extent         = choose_extent(&caps, width, height);
    uint32_t           min_images     = choose_image_count(&caps);

    if (extent.width == 0 || extent.height == 0) {
        fprintf(stderr, "[archtype] Cannot create a swapchain with size 0 (minimized window)\n");
        return false;
    }

    VkSwapchainCreateInfoKHR info;
    memset(&info, 0, sizeof(info));
    info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface          = context->surface;
    info.minImageCount    = min_images;
    info.imageFormat      = surface_format.format;
    info.imageColorSpace  = surface_format.colorSpace;
    info.imageExtent      = extent;
    info.imageArrayLayers = 1;
    info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.preTransform     = caps.currentTransform;
    info.compositeAlpha   = choose_composite_alpha(&caps);
    info.presentMode      = present_mode;
    info.clipped          = VK_TRUE;
    info.oldSwapchain     = VK_NULL_HANDLE;

    /* Allow copying into swapchain images later (screenshots, blits) */
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
        info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    /* If drawing and presenting use different queue families, both must */
    /* be allowed to access the images.                                  */
    uint32_t families[2] = { context->graphics_queue_family, context->present_queue_family };
    if (families[0] != families[1]) {
        info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices   = families;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    result = vkCreateSwapchainKHR(context->device, &info, NULL, &swapchain->handle);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateSwapchainKHR", result);
        return false;
    }

    swapchain->format       = surface_format.format;
    swapchain->color_space  = surface_format.colorSpace;
    swapchain->present_mode = present_mode;
    swapchain->extent       = extent;

    /* The driver may create more images than we asked for */
    vkGetSwapchainImagesKHR(context->device, swapchain->handle, &swapchain->image_count, NULL);
    swapchain->images = (VkImage *)malloc(swapchain->image_count * sizeof(*swapchain->images));
    if (!swapchain->images) {
        fprintf(stderr, "[archtype] Failed to allocate swapchain images\n");
        at_vk_swapchain_destroy(swapchain, context);
        return false;
    }
    vkGetSwapchainImagesKHR(context->device, swapchain->handle, &swapchain->image_count,
                            swapchain->images);

    if (!create_image_views(swapchain, context) || !create_semaphores(swapchain, context)) {
        at_vk_swapchain_destroy(swapchain, context);
        return false;
    }

    fprintf(stderr, "[archtype] Swapchain created: %ux%u, %u images, %s, %s\n",
            extent.width, extent.height, swapchain->image_count,
            format_string(swapchain->format), present_mode_string(present_mode));
    return true;
}

void at_vk_swapchain_destroy(at_vk_swapchain *swapchain, const at_vk_context *context) {
    if (swapchain->render_finished) {
        for (uint32_t i = 0; i < swapchain->image_count; ++i) {
            if (swapchain->render_finished[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(context->device, swapchain->render_finished[i], NULL);
            }
        }
        free(swapchain->render_finished);
    }

    if (swapchain->image_views) {
        for (uint32_t i = 0; i < swapchain->image_count; ++i) {
            if (swapchain->image_views[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(context->device, swapchain->image_views[i], NULL);
            }
        }
        free(swapchain->image_views);
    }

    /* The images belong to the swapchain: destroying it frees them */
    free(swapchain->images);

    if (swapchain->handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(context->device, swapchain->handle, NULL);
    }

    memset(swapchain, 0, sizeof(*swapchain));
}