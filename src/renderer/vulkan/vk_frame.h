#ifndef ARCHTYPE_VK_FRAME_H
#define ARCHTYPE_VK_FRAME_H

/* Per-frame resources and the begin/end frame flow. */

#include "renderer/vulkan/vk_context.h"
#include "renderer/vulkan/vk_swapchain.h"

/* How many frames the CPU can prepare while the GPU is still drawing. */
/* 2 is the usual choice: more adds input latency, fewer stalls the CPU. */
#define AT_VK_FRAMES_IN_FLIGHT 2

typedef struct at_vk_frame {
    VkCommandBuffer command_buffer;
    VkSemaphore     image_available; /* Signaled when the swapchain image is ready  */
    VkFence         in_flight;       /* Signaled when the GPU finished this frame   */
} at_vk_frame;

typedef struct at_vk_frames {
    VkCommandPool command_pool;
    at_vk_frame   frames[AT_VK_FRAMES_IN_FLIGHT];
    uint32_t      current;      /* Which frame in flight is being recorded    */
    uint32_t      image_index;  /* Swapchain image used by the current frame  */
    bool          out_of_date;  /* The swapchain must be recreated            */
} at_vk_frames;

/* Creates the command pool, command buffers, semaphores and fences. */
bool at_vk_frames_create(at_vk_frames *frames, const at_vk_context *context);

/* Destroys everything created by at_vk_frames_create (safe to call twice). */
void at_vk_frames_destroy(at_vk_frames *frames, const at_vk_context *context);

/* Waits for the frame slot, acquires a swapchain image and starts         */
/* rendering into it, clearing it with clear_color (linear RGBA).          */
/* Returns false if this frame must be skipped (e.g. swapchain out of date). */
bool at_vk_frame_begin(at_vk_frames           *frames,
                       const at_vk_context    *context,
                       const at_vk_swapchain  *swapchain,
                       const float             clear_color[4]);

/* Finishes rendering, submits the commands and presents the image. */
void at_vk_frame_end(at_vk_frames          *frames,
                     const at_vk_context   *context,
                     const at_vk_swapchain *swapchain);

#endif /* ARCHTYPE_VK_FRAME_H */