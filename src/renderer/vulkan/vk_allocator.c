#include "renderer/vulkan/vk_allocator.h"

#include <string.h>

bool at_vk_allocator_create(VmaAllocator *allocator, const at_vk_context *context) {
    VmaAllocatorCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    info.instance         = context->instance;
    info.physicalDevice   = context->physical_device;
    info.device           = context->device;

    /* Hand VMA the function pointers volk already loaded */
    VmaVulkanFunctions functions;
    VkResult result = vmaImportVulkanFunctionsFromVolk(&info, &functions);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vmaImportVulkanFunctionsFromVolk", result);
        return false;
    }
    info.pVulkanFunctions = &functions;

    result = vmaCreateAllocator(&info, allocator);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vmaCreateAllocator", result);
        return false;
    }

    fprintf(stderr, "[archtype] GPU memory allocator created (VMA)\n");
    return true;
}

void at_vk_allocator_destroy(VmaAllocator *allocator) {
    if (*allocator == VK_NULL_HANDLE) {
        return;
    }

    vmaDestroyAllocator(*allocator);
    *allocator = VK_NULL_HANDLE;
}