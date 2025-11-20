#pragma once
#include <vulkan/vulkan.h>

namespace vkinit {

VkCommandPoolCreateInfo command_pool_create_info(uint32_t queueFamilyIndex,
												 VkCommandPoolCreateFlags flags /*= 0*/);

VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool pool,
														 uint32_t count /*= 1*/);

VkCommandBufferBeginInfo command_buffer_begin_info(VkCommandBufferUsageFlags flags = 0);

VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags,
									VkExtent3D extent);

VkImageViewCreateInfo imageview_create_info(VkFormat format, VkImage image,
											VkImageAspectFlags aspectFlags);

VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags /*= 0*/);

VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags /*= 0*/);

}; // namespace vkinit