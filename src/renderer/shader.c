#include "archtype/renderer/shader.h"
#include "renderer/renderer_internal.h"

#include <stdlib.h>
#include <string.h>

#include "core/file.h"

/* Every SPIR-V binary starts with this number */
#define AT_SPIRV_MAGIC 0x07230203u

at_shader *at_shader_create(at_renderer *renderer, const uint32_t *code, size_t size) {
    if (!renderer || !code || size == 0 || size % 4 != 0) {
        fprintf(stderr, "[archtype] Invalid SPIR-V code (size must be a multiple of 4)\n");
        return NULL;
    }
    if (code[0] != AT_SPIRV_MAGIC) {
        fprintf(stderr, "[archtype] Not a SPIR-V binary (was the shader compiled?)\n");
        return NULL;
    }

    at_shader *shader = (at_shader *)calloc(1, sizeof(*shader));
    if (!shader) {
        fprintf(stderr, "[archtype] Failed to allocate shader\n");
        return NULL;
    }

    VkShaderModuleCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode    = code;

    VkResult result = vkCreateShaderModule(renderer->context.device, &info, NULL,
                                           &shader->module);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateShaderModule", result);
        free(shader);
        return NULL;
    }

    return shader;
}

at_shader *at_shader_load(at_renderer *renderer, const char *path) {
    if (!renderer || !path) {
        return NULL;
    }

    size_t size = 0;
    void  *code = at_file_read_all(path, &size);
    if (!code) {
        return NULL;
    }

    /* malloc'ed memory is aligned for uint32_t, as Vulkan requires */
    at_shader *shader = at_shader_create(renderer, (const uint32_t *)code, size);
    if (!shader) {
        fprintf(stderr, "[archtype] Failed to load shader: %s\n", path);
    }

    free(code);
    return shader;
}

void at_shader_destroy(at_renderer *renderer, at_shader *shader) {
    if (!renderer || !shader) {
        return;
    }

    vkDestroyShaderModule(renderer->context.device, shader->module, NULL);
    free(shader);
}