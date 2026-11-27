#include "archtype/renderer/texture.h"
#include "renderer/renderer_internal.h"

#include <stdlib.h>
#include <string.h>

#include "renderer/image_loader.h"

static VkFilter to_vk_filter(at_texture_filter filter) {
    return filter == AT_TEXTURE_FILTER_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

static VkSamplerAddressMode to_vk_address_mode(at_texture_wrap wrap) {
    switch (wrap) {
        case AT_TEXTURE_WRAP_CLAMP:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AT_TEXTURE_WRAP_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AT_TEXTURE_WRAP_REPEAT:
        default:                     return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

/* sRGB textures are converted to linear when sampled, so shader math  */
/* (lighting, blending) happens in linear space, as it should.         */
static VkFormat to_vk_format(at_texture_color_space color_space) {
    return color_space == AT_TEXTURE_COLOR_SPACE_LINEAR ? VK_FORMAT_R8G8B8A8_UNORM
                                                        : VK_FORMAT_R8G8B8A8_SRGB;
}

/* How many mip levels the texture gets: the whole chain when asked for */
/* and the GPU can build it, otherwise just the image itself.           */
static uint32_t choose_mip_levels(at_renderer *renderer, uint32_t width, uint32_t height,
                                  VkFormat format, const at_texture_options *options) {
    if (!options->mipmaps) {
        return 1;
    }
    if (!at_vk_format_supports_mipmaps(&renderer->context, format)) {
        fprintf(stderr, "[archtype] This GPU can't generate mipmaps for the texture's "
                        "format: it gets no mipmaps\n");
        return 1;
    }
    return at_vk_mip_level_count(width, height);
}

static bool create_sampler(at_renderer *renderer, at_texture *texture,
                           const at_texture_options *options) {
    VkFilter             filter  = to_vk_filter(options->filter);
    VkSamplerAddressMode address = to_vk_address_mode(options->wrap);

    VkSamplerCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter    = filter;
    info.minFilter    = filter;
    info.addressModeU = address;
    info.addressModeV = address;
    info.addressModeW = address;
    info.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    /* Between mip levels: blend the two nearest (smooth), or take the */
    /* nearest one (sharp pixels).                                     */
    info.mipmapMode = options->filter == AT_TEXTURE_FILTER_NEAREST
                    ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                    : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.minLod     = 0.0f;
    info.maxLod     = (float)texture->image.mip_levels;

    VkResult result = vkCreateSampler(renderer->context.device, &info, NULL, &texture->sampler);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateSampler", result);
        return false;
    }
    return true;
}

at_texture *at_texture_create(at_renderer *renderer, uint32_t width, uint32_t height,
                              const void *pixels, const at_texture_options *options) {
    if (!renderer || !pixels || width == 0 || height == 0) {
        fprintf(stderr, "[archtype] Invalid texture (pixels and a size are required)\n");
        return NULL;
    }

    VkPhysicalDeviceLimits *limits = &renderer->context.physical_device_properties.limits;
    if (width > limits->maxImageDimension2D || height > limits->maxImageDimension2D) {
        fprintf(stderr, "[archtype] Texture %ux%u is larger than the GPU limit (%u)\n",
                width, height, limits->maxImageDimension2D);
        return NULL;
    }

    at_texture_options defaults;
    memset(&defaults, 0, sizeof(defaults));
    if (!options) {
        options = &defaults;
    }

    at_texture *texture = (at_texture *)calloc(1, sizeof(*texture));
    if (!texture) {
        fprintf(stderr, "[archtype] Failed to allocate texture\n");
        return NULL;
    }

    VkFormat format     = to_vk_format(options->color_space);
    uint32_t mip_levels = choose_mip_levels(renderer, width, height, format, options);

    /* Mipmaps are made by copying each level into the next, so the image */
    /* is also read by transfers                                          */
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (mip_levels > 1) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    bool ok = at_vk_image_create_ex(&texture->image, &renderer->allocator, &renderer->context,
                                    width, height, format, usage, mip_levels)
           && at_vk_image_upload(&texture->image, &renderer->allocator, &renderer->upload,
                                 &renderer->context, pixels)
           && create_sampler(renderer, texture, options);

    if (!ok) {
        at_texture_destroy(renderer, texture);
        return NULL;
    }

    return texture;
}

at_texture *at_texture_load(at_renderer *renderer, const char *path,
                            const at_texture_options *options) {
    if (!renderer || !path) {
        return NULL;
    }

    at_image_data image;
    if (!at_image_load(path, &image)) {
        return NULL;
    }

    at_texture *texture = at_texture_create(renderer, image.width, image.height,
                                            image.pixels, options);
    if (texture) {
        fprintf(stderr, "[archtype] Texture loaded: %s (%ux%u, %u mip levels)\n",
                path, image.width, image.height, texture->image.mip_levels);
    }

    at_image_free(&image);
    return texture;
}

void at_texture_get_size(const at_texture *texture, uint32_t *width, uint32_t *height) {
    if (width) {
        *width = texture->image.extent.width;
    }
    if (height) {
        *height = texture->image.extent.height;
    }
}

void at_texture_destroy(at_renderer *renderer, at_texture *texture) {
    if (!renderer || !texture) {
        return;
    }

    /* The GPU may still be sampling this texture */
    vkDeviceWaitIdle(renderer->context.device);

    if (texture->sampler != VK_NULL_HANDLE) {
        vkDestroySampler(renderer->context.device, texture->sampler, NULL);
    }
    at_vk_image_destroy(&texture->image, &renderer->allocator, &renderer->context);
    free(texture);
}