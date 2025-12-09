#pragma once
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace vkinit {

VkCommandPoolCreateInfo command_pool_create_info(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags /*= 0*/);

VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool pool, uint32_t count /*= 1*/);

VkCommandBufferBeginInfo command_buffer_begin_info(VkCommandBufferUsageFlags flags = 0);

VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);

VkImageViewCreateInfo imageview_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);

VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags /*= 0*/);

VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags /*= 0*/);

VkDescriptorSetLayoutBinding descriptorSetLayoutBinding(VkDescriptorType type, VkShaderStageFlags stageFlags,
														uint32_t binding, uint32_t descriptorCount = 1);

VkDescriptorSetLayoutCreateInfo
descriptorSetLayoutCreateInfo(const std::vector<VkDescriptorSetLayoutBinding> &bindings);

VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo(const VkDescriptorSetLayout *pSetLayouts,
													uint32_t setLayoutCount = 1);

VkDescriptorPoolCreateInfo descriptorPoolCreateInfo(const std::vector<VkDescriptorPoolSize> &poolSizes,
													uint32_t maxSets);

VkDescriptorSetAllocateInfo descriptorSetAllocateInfo(VkDescriptorPool descriptorPool,
													  const VkDescriptorSetLayout *pSetLayouts,
													  uint32_t descriptorSetCount);

VkWriteDescriptorSetAccelerationStructureKHR writeDescriptorSetAccelerationStructureKHR();

VkWriteDescriptorSet writeDescriptorSet(VkDescriptorSet dstSet, VkDescriptorType type, uint32_t binding,
										VkDescriptorImageInfo *imageInfo, uint32_t descriptorCount = 1);
VkWriteDescriptorSet writeDescriptorSet(VkDescriptorSet dstSet, VkDescriptorType type, uint32_t binding,
										VkDescriptorBufferInfo *bufferInfo, uint32_t descriptorCount = 1);

VkCommandBufferBeginInfo commandBufferBeginInfo();

VkImageMemoryBarrier imageMemoryBarrier();

//> init_submit
VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);

VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd);

VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo *cmd, VkSemaphoreSubmitInfo *signalSemaphoreInfo,
						  VkSemaphoreSubmitInfo *waitSemaphoreInfo);
//< init_submit

}; // namespace vkinit