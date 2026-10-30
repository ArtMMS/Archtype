#include "renderer/vulkan/vk_device.h"

#include <stdlib.h>
#include <string.h>

/* Needed by MoltenVK (macOS); must be enabled when the GPU reports it */
#define AT_VK_PORTABILITY_SUBSET_EXTENSION "VK_KHR_portability_subset"

typedef struct queue_families {
    uint32_t graphics;
    uint32_t present;
    bool     has_graphics;
    bool     has_present;
} queue_families;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static const char *device_type_string(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
        default:                                     return "other";
    }
}

static bool has_device_extension(VkPhysicalDevice gpu, const char *name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, NULL);
    if (count == 0) {
        return false;
    }

    VkExtensionProperties *extensions =
        (VkExtensionProperties *)malloc(count * sizeof(*extensions));
    if (!extensions) {
        return false;
    }
    vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, extensions);

    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(extensions[i].extensionName, name) == 0) {
            found = true;
            break;
        }
    }

    free(extensions);
    return found;
}

/* Finds a queue family for drawing (graphics) and one for showing images */
/* on the window (present). Prefers a single family that does both.       */
static queue_families find_queue_families(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    queue_families families;
    memset(&families, 0, sizeof(families));

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, NULL);
    if (count == 0) {
        return families;
    }

    VkQueueFamilyProperties *properties =
        (VkQueueFamilyProperties *)malloc(count * sizeof(*properties));
    if (!properties) {
        return families;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, properties);

    for (uint32_t i = 0; i < count; ++i) {
        bool graphics = (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;

        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);

        if (graphics && present) {
            families.graphics     = i;
            families.present      = i;
            families.has_graphics = true;
            families.has_present  = true;
            break;
        }
        if (graphics && !families.has_graphics) {
            families.graphics     = i;
            families.has_graphics = true;
        }
        if (present && !families.has_present) {
            families.present     = i;
            families.has_present = true;
        }
    }

    free(properties);
    return families;
}

static bool supports_required_features(VkPhysicalDevice gpu) {
    VkPhysicalDeviceVulkan13Features features13;
    memset(&features13, 0, sizeof(features13));
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

    VkPhysicalDeviceFeatures2 features;
    memset(&features, 0, sizeof(features));
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;

    vkGetPhysicalDeviceFeatures2(gpu, &features);

    return features13.dynamicRendering && features13.synchronization2;
}

static bool surface_is_usable(VkPhysicalDevice gpu, VkSurfaceKHR surface) {
    uint32_t format_count = 0;
    uint32_t mode_count   = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, NULL);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &mode_count, NULL);
    return format_count > 0 && mode_count > 0;
}

/* Returns a score for the GPU (higher is better), or -1 if it cannot be */
/* used. When it cannot be used, *reason says why.                       */
static int rate_device(VkPhysicalDevice gpu, VkSurfaceKHR surface,
                       queue_families *families, const char **reason) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(gpu, &properties);

    if (properties.apiVersion < VK_API_VERSION_1_3) {
        *reason = "Vulkan 1.3 not supported";
        return -1;
    }

    *families = find_queue_families(gpu, surface);
    if (!families->has_graphics || !families->has_present) {
        *reason = "no queue that can draw and present to the window";
        return -1;
    }

    if (!has_device_extension(gpu, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        *reason = "swapchain extension not supported";
        return -1;
    }

    if (!supports_required_features(gpu)) {
        *reason = "dynamic rendering or synchronization2 not supported";
        return -1;
    }

    if (!surface_is_usable(gpu, surface)) {
        *reason = "no surface formats or present modes";
        return -1;
    }

    switch (properties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 1000;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 100;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 10;
        default:                                     return 1;
    }
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

bool at_vk_device_select(at_vk_context *context) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(context->instance, &count, NULL);
    if (count == 0) {
        fprintf(stderr, "[archtype] No GPU with Vulkan support was found\n");
        return false;
    }

    VkPhysicalDevice *gpus = (VkPhysicalDevice *)malloc(count * sizeof(*gpus));
    if (!gpus) {
        return false;
    }
    vkEnumeratePhysicalDevices(context->instance, &count, gpus);

    fprintf(stderr, "[archtype] GPUs found: %u\n", count);

    int            best_score = -1;
    queue_families best_families;
    memset(&best_families, 0, sizeof(best_families));

    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(gpus[i], &properties);

        queue_families families;
        const char    *reason = "";
        int            score  = rate_device(gpus[i], context->surface, &families, &reason);

        fprintf(stderr, "[archtype]   - %s (%s, Vulkan %u.%u): %s%s\n",
                properties.deviceName,
                device_type_string(properties.deviceType),
                VK_API_VERSION_MAJOR(properties.apiVersion),
                VK_API_VERSION_MINOR(properties.apiVersion),
                score >= 0 ? "suitable" : "not suitable, ",
                score >= 0 ? "" : reason);

        if (score > best_score) {
            best_score                          = score;
            best_families                       = families;
            context->physical_device            = gpus[i];
            context->physical_device_properties = properties;
        }
    }

    free(gpus);

    if (best_score < 0) {
        fprintf(stderr, "[archtype] No suitable GPU found\n");
        context->physical_device = VK_NULL_HANDLE;
        return false;
    }

    context->graphics_queue_family = best_families.graphics;
    context->present_queue_family  = best_families.present;

    fprintf(stderr, "[archtype] Selected GPU: %s\n",
            context->physical_device_properties.deviceName);
    fprintf(stderr, "[archtype] Queue families: graphics = %u, present = %u\n",
            context->graphics_queue_family, context->present_queue_family);
    return true;
}

bool at_vk_device_create(at_vk_context *context) {
    /* One queue per unique family */
    float                   priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[2];
    uint32_t                queue_info_count = 0;

    memset(queue_infos, 0, sizeof(queue_infos));

    queue_infos[queue_info_count].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_infos[queue_info_count].queueFamilyIndex = context->graphics_queue_family;
    queue_infos[queue_info_count].queueCount       = 1;
    queue_infos[queue_info_count].pQueuePriorities = &priority;
    queue_info_count++;

    if (context->present_queue_family != context->graphics_queue_family) {
        queue_infos[queue_info_count].sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_infos[queue_info_count].queueFamilyIndex = context->present_queue_family;
        queue_infos[queue_info_count].queueCount       = 1;
        queue_infos[queue_info_count].pQueuePriorities = &priority;
        queue_info_count++;
    }

    /* Device extensions */
    const char *extensions[2];
    uint32_t    extension_count = 0;

    extensions[extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (has_device_extension(context->physical_device, AT_VK_PORTABILITY_SUBSET_EXTENSION)) {
        extensions[extension_count++] = AT_VK_PORTABILITY_SUBSET_EXTENSION;
    }

    /* Vulkan 1.3 features used by the renderer */
    VkPhysicalDeviceVulkan13Features features13;
    memset(&features13, 0, sizeof(features13));
    features13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE; /* Draw without render pass objects */
    features13.synchronization2 = VK_TRUE; /* Simpler barriers and submits     */

    VkPhysicalDeviceFeatures2 features;
    memset(&features, 0, sizeof(features));
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;

    VkDeviceCreateInfo create_info;
    memset(&create_info, 0, sizeof(create_info));
    create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pNext                   = &features; /* pEnabledFeatures must stay NULL */
    create_info.queueCreateInfoCount    = queue_info_count;
    create_info.pQueueCreateInfos       = queue_infos;
    create_info.enabledExtensionCount   = extension_count;
    create_info.ppEnabledExtensionNames = extensions;

    VkResult result = vkCreateDevice(context->physical_device, &create_info, NULL,
                                     &context->device);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateDevice", result);
        return false;
    }

    /* Load device-level functions straight from the driver (faster calls) */
    volkLoadDevice(context->device);

    vkGetDeviceQueue(context->device, context->graphics_queue_family, 0,
                     &context->graphics_queue);
    vkGetDeviceQueue(context->device, context->present_queue_family, 0,
                     &context->present_queue);

    fprintf(stderr, "[archtype] Logical device created\n");
    return true;
}

void at_vk_device_destroy(at_vk_context *context) {
    if (context->device == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(context->device);
    vkDestroyDevice(context->device, NULL);

    context->device         = VK_NULL_HANDLE;
    context->graphics_queue = VK_NULL_HANDLE;
    context->present_queue  = VK_NULL_HANDLE;
}