#include "renderer/vulkan/vk_context.h"
#include "renderer/vulkan/vk_device.h"

#include <stdlib.h>
#include <string.h>

#include "archtype/core/version.h"
#include "platform/platform_internal.h"

#define AT_VK_VALIDATION_LAYER "VK_LAYER_KHRONOS_validation"
#define AT_VK_MAX_EXTENSIONS   16

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static bool is_layer_available(const char *name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, NULL);
    if (count == 0) {
        return false;
    }

    VkLayerProperties *layers = (VkLayerProperties *)malloc(count * sizeof(*layers));
    if (!layers) {
        return false;
    }
    vkEnumerateInstanceLayerProperties(&count, layers);

    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(layers[i].layerName, name) == 0) {
            found = true;
            break;
        }
    }

    free(layers);
    return found;
}

static bool is_extension_available(const char *name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    if (count == 0) {
        return false;
    }

    VkExtensionProperties *extensions =
        (VkExtensionProperties *)malloc(count * sizeof(*extensions));
    if (!extensions) {
        return false;
    }
    vkEnumerateInstanceExtensionProperties(NULL, &count, extensions);

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

/* ------------------------------------------------------------------------ */
/* Debug messenger                                                           */
/* ------------------------------------------------------------------------ */

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             type,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void                                       *user_data) {
    (void)type;
    (void)user_data;

    const char *label = "INFO";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        label = "ERROR";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        label = "WARNING";
    }

    fprintf(stderr, "[archtype] Validation %s: %s\n", label, data->pMessage);

    /* Returning VK_FALSE lets the Vulkan call continue normally */
    return VK_FALSE;
}

static void fill_debug_messenger_info(VkDebugUtilsMessengerCreateInfoEXT *info) {
    memset(info, 0, sizeof(*info));
    info->sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info->messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info->messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                          | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info->pfnUserCallback = debug_callback;
}

/* vkCreateDebugUtilsMessengerEXT is an extension function, so it must be */
/* loaded at runtime instead of being linked directly.                    */
static VkResult create_debug_messenger(at_vk_context *context) {
    PFN_vkCreateDebugUtilsMessengerEXT create_fn =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            context->instance, "vkCreateDebugUtilsMessengerEXT");
    if (!create_fn) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkDebugUtilsMessengerCreateInfoEXT info;
    fill_debug_messenger_info(&info);
    return create_fn(context->instance, &info, NULL, &context->debug_messenger);
}

static void destroy_debug_messenger(at_vk_context *context) {
    if (context->debug_messenger == VK_NULL_HANDLE) {
        return;
    }

    PFN_vkDestroyDebugUtilsMessengerEXT destroy_fn =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            context->instance, "vkDestroyDebugUtilsMessengerEXT");
    if (destroy_fn) {
        destroy_fn(context->instance, context->debug_messenger, NULL);
    }
    context->debug_messenger = VK_NULL_HANDLE;
}

/* ------------------------------------------------------------------------ */
/* Instance                                                                  */
/* ------------------------------------------------------------------------ */

static void print_instance_version(void) {
    uint32_t version = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&version);

    fprintf(stderr, "[archtype] Vulkan instance version: %u.%u.%u\n",
            VK_API_VERSION_MAJOR(version),
            VK_API_VERSION_MINOR(version),
            VK_API_VERSION_PATCH(version));
}

static bool create_instance(at_vk_context *context, const at_vk_context_desc *desc) {
    /* The platform layer loads the Vulkan loader (vulkan-1.dll / libvulkan.so) */
    /* and volk takes every function from it, so both use the same library.     */
    PFN_vkGetInstanceProcAddr get_instance_proc_addr = at_platform_load_vulkan();
    if (!get_instance_proc_addr) {
        fprintf(stderr, "[archtype] Could not load Vulkan. Is a Vulkan driver installed?\n");
        return false;
    }
    context->vulkan_loaded = true;

    volkInitializeCustom(get_instance_proc_addr);

    print_instance_version();

    /* Validation layers: only if requested and installed */
    bool validation = desc->enable_validation;
    if (validation && !is_layer_available(AT_VK_VALIDATION_LAYER)) {
        fprintf(stderr, "[archtype] Validation layers requested but not available. "
                        "Is the Vulkan SDK installed?\n");
        validation = false;
    }
    if (validation && !is_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        fprintf(stderr, "[archtype] %s not available, validation disabled\n",
                VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        validation = false;
    }

    /* Instance extensions: what the window system needs, plus debug utils */
    uint32_t           platform_count      = 0;
    const char *const *platform_extensions = at_platform_get_vulkan_extensions(&platform_count);
    if (!platform_extensions) {
        fprintf(stderr, "[archtype] Could not get the Vulkan extensions for windows\n");
        return false;
    }

    const char *extensions[AT_VK_MAX_EXTENSIONS];
    uint32_t    extension_count = 0;

    for (uint32_t i = 0; i < platform_count && extension_count < AT_VK_MAX_EXTENSIONS; ++i) {
        extensions[extension_count++] = platform_extensions[i];
    }
    if (validation && extension_count < AT_VK_MAX_EXTENSIONS) {
        extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

    VkInstanceCreateFlags flags = 0;
#if defined(AT_PLATFORM_MACOS)
    /* MoltenVK is a "portability" implementation and must be opted into */
    if (extension_count < AT_VK_MAX_EXTENSIONS) {
        extensions[extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    at_version engine_version = at_get_version();

    VkApplicationInfo app_info;
    memset(&app_info, 0, sizeof(app_info));
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = desc->app_name ? desc->app_name : "Archtype Application";
    app_info.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    app_info.pEngineName        = "Archtype";
    app_info.engineVersion      = VK_MAKE_API_VERSION(0, engine_version.major,
                                                      engine_version.minor,
                                                      engine_version.patch);
    app_info.apiVersion         = VK_API_VERSION_1_3;

    const char *layers[] = { AT_VK_VALIDATION_LAYER };

    VkInstanceCreateInfo create_info;
    memset(&create_info, 0, sizeof(create_info));
    create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.flags                   = flags;
    create_info.pApplicationInfo        = &app_info;
    create_info.enabledExtensionCount   = extension_count;
    create_info.ppEnabledExtensionNames = extensions;

    /* Chaining a messenger here also reports problems inside */
    /* vkCreateInstance and vkDestroyInstance themselves.      */
    VkDebugUtilsMessengerCreateInfoEXT debug_info;
    if (validation) {
        fill_debug_messenger_info(&debug_info);
        create_info.enabledLayerCount   = 1;
        create_info.ppEnabledLayerNames = layers;
        create_info.pNext               = &debug_info;
    }

    VkResult result = vkCreateInstance(&create_info, NULL, &context->instance);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateInstance", result);
        return false;
    }

    /* Load every instance-level function (and extensions like debug utils) */
    volkLoadInstance(context->instance);

    if (validation) {
        result = create_debug_messenger(context);
        if (result != VK_SUCCESS) {
            AT_VK_LOG_ERROR("vkCreateDebugUtilsMessengerEXT", result);
            validation = false;
        }
    }

    context->validation_enabled = validation;

    fprintf(stderr, "[archtype] Vulkan instance created (%u extensions)\n", extension_count);
    for (uint32_t i = 0; i < extension_count; ++i) {
        fprintf(stderr, "[archtype]   - %s\n", extensions[i]);
    }
    fprintf(stderr, "[archtype] Validation layers: %s\n", validation ? "enabled" : "disabled");

    return true;
}

/* ------------------------------------------------------------------------ */
/* Surface                                                                   */
/* ------------------------------------------------------------------------ */

static bool create_surface(at_vk_context *context, const at_window *window) {
    /* The platform layer logs the reason on failure */
    return at_window_create_vulkan_surface(window, context->instance, &context->surface);
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

bool at_vk_context_init(at_vk_context *context, const at_vk_context_desc *desc) {
    memset(context, 0, sizeof(*context));

    if (!desc->window) {
        fprintf(stderr, "[archtype] A window is required to create the Vulkan context\n");
        return false;
    }

    bool ok = create_instance(context, desc)
           && create_surface(context, desc->window)
           && at_vk_device_select(context)
           && at_vk_device_create(context);

    if (!ok) {
        at_vk_context_shutdown(context);
        return false;
    }

    return true;
}

void at_vk_context_shutdown(at_vk_context *context) {
    bool vulkan_loaded = context->vulkan_loaded;

    /* Destroy in reverse order of creation */
    at_vk_device_destroy(context);

    if (context->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(context->instance, context->surface, NULL);
    }

    if (context->instance != VK_NULL_HANDLE) {
        destroy_debug_messenger(context);
        vkDestroyInstance(context->instance, NULL);
    }

    memset(context, 0, sizeof(*context));

    /* Last: nothing may call Vulkan after the library is released */
    if (vulkan_loaded) {
        at_platform_unload_vulkan();
    }
}