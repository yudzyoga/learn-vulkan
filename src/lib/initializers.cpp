#include "initializers.h"

namespace vkinit {

//> init_cmd
VkCommandPoolCreateInfo command_pool_create_info(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags /*= 0*/) {
	VkCommandPoolCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	info.pNext = nullptr;
	info.queueFamilyIndex = queueFamilyIndex;
	info.flags = flags;
	return info;
}

VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool pool, uint32_t count /*= 1*/) {
	VkCommandBufferAllocateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	info.pNext = nullptr;

	info.commandPool = pool;
	info.commandBufferCount = count;
	info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	return info;
}
//< init_cmd

//> init_cmd_draw
VkCommandBufferBeginInfo command_buffer_begin_info(VkCommandBufferUsageFlags flags /*= 0*/) {
	VkCommandBufferBeginInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	info.pNext = nullptr;

	info.pInheritanceInfo = nullptr;
	info.flags = flags;
	return info;
}
//< init_cmd_draw

//> image_set
VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent) {
	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.pNext = nullptr;

	info.imageType = VK_IMAGE_TYPE_2D;

	info.format = format;
	info.extent = extent;

	info.mipLevels = 1;
	info.arrayLayers = 1;

	// for MSAA. we will not be using it by default, so default it to 1 sample
	// per pixel.
	info.samples = VK_SAMPLE_COUNT_1_BIT;

	// optimal tiling, which means the image is stored on the best gpu format
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usageFlags;

	// * additional from Vulkan tutorila
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	return info;
}

VkImageViewCreateInfo imageview_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags) {
	// build a image-view for the depth image to use for rendering
	VkImageViewCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.pNext = nullptr;

	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.image = image;
	info.format = format;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.baseArrayLayer = 0;
	info.subresourceRange.layerCount = 1;
	info.subresourceRange.aspectMask = aspectFlags;

	return info;
}
//< image_set

//> init_sync
VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags /*= 0*/) {
	VkFenceCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	info.pNext = nullptr;

	info.flags = flags;

	return info;
}

VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags /*= 0*/) {
	VkSemaphoreCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	info.pNext = nullptr;
	info.flags = flags;
	return info;
}
//< init_sync

VkDescriptorSetLayoutBinding descriptorSetLayoutBinding(VkDescriptorType type, VkShaderStageFlags stageFlags,
														uint32_t binding, uint32_t descriptorCount) {
	VkDescriptorSetLayoutBinding setLayoutBinding{};
	setLayoutBinding.descriptorType = type;
	setLayoutBinding.stageFlags = stageFlags;
	setLayoutBinding.binding = binding;
	setLayoutBinding.descriptorCount = descriptorCount;
	return setLayoutBinding;
}

VkDescriptorSetLayoutCreateInfo
descriptorSetLayoutCreateInfo(const std::vector<VkDescriptorSetLayoutBinding> &bindings) {
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};
	descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutCreateInfo.pBindings = bindings.data();
	descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	return descriptorSetLayoutCreateInfo;
}

VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo(const VkDescriptorSetLayout *pSetLayouts, uint32_t setLayoutCount) {
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.setLayoutCount = setLayoutCount;
	pipelineLayoutCreateInfo.pSetLayouts = pSetLayouts;
	return pipelineLayoutCreateInfo;
}

VkDescriptorPoolCreateInfo descriptorPoolCreateInfo(const std::vector<VkDescriptorPoolSize> &poolSizes,
													uint32_t maxSets) {
	VkDescriptorPoolCreateInfo descriptorPoolInfo{};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	descriptorPoolInfo.pPoolSizes = poolSizes.data();
	descriptorPoolInfo.maxSets = maxSets;
	return descriptorPoolInfo;
}

VkDescriptorSetAllocateInfo descriptorSetAllocateInfo(VkDescriptorPool descriptorPool,
													  const VkDescriptorSetLayout *pSetLayouts,
													  uint32_t descriptorSetCount) {
	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{};
	descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocateInfo.descriptorPool = descriptorPool;
	descriptorSetAllocateInfo.pSetLayouts = pSetLayouts;
	descriptorSetAllocateInfo.descriptorSetCount = descriptorSetCount;
	return descriptorSetAllocateInfo;
}

VkWriteDescriptorSetAccelerationStructureKHR writeDescriptorSetAccelerationStructureKHR() {
	VkWriteDescriptorSetAccelerationStructureKHR writeDescriptorSetAccelerationStructureKHR{};
	writeDescriptorSetAccelerationStructureKHR.sType =
		VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	return writeDescriptorSetAccelerationStructureKHR;
}

VkWriteDescriptorSet writeDescriptorSet(VkDescriptorSet dstSet, VkDescriptorType type, uint32_t binding,
										VkDescriptorImageInfo *imageInfo, uint32_t descriptorCount) {
	VkWriteDescriptorSet writeDescriptorSet{};
	writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSet.dstSet = dstSet;
	writeDescriptorSet.descriptorType = type;
	writeDescriptorSet.dstBinding = binding;
	writeDescriptorSet.pImageInfo = imageInfo;
	writeDescriptorSet.descriptorCount = descriptorCount;
	return writeDescriptorSet;
}

VkWriteDescriptorSet writeDescriptorSet(VkDescriptorSet dstSet, VkDescriptorType type, uint32_t binding,
										VkDescriptorBufferInfo *bufferInfo, uint32_t descriptorCount) {
	VkWriteDescriptorSet writeDescriptorSet{};
	writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSet.dstSet = dstSet;
	writeDescriptorSet.descriptorType = type;
	writeDescriptorSet.dstBinding = binding;
	writeDescriptorSet.pBufferInfo = bufferInfo;
	writeDescriptorSet.descriptorCount = descriptorCount;
	return writeDescriptorSet;
}

VkCommandBufferBeginInfo commandBufferBeginInfo() {
	VkCommandBufferBeginInfo cmdBufferBeginInfo{};
	cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	return cmdBufferBeginInfo;
}

VkImageMemoryBarrier imageMemoryBarrier() {
	VkImageMemoryBarrier imageMemoryBarrier{};
	imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	return imageMemoryBarrier;
}

//> init_submit
VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore) {
	VkSemaphoreSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;
	submitInfo.semaphore = semaphore;
	submitInfo.stageMask = stageMask;
	submitInfo.deviceIndex = 0;
	submitInfo.value = 1;

	return submitInfo;
}

VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd) {
	VkCommandBufferSubmitInfo info{};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	info.pNext = nullptr;
	info.commandBuffer = cmd;
	info.deviceMask = 0;

	return info;
}

VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo *cmd, VkSemaphoreSubmitInfo *signalSemaphoreInfo,
						  VkSemaphoreSubmitInfo *waitSemaphoreInfo) {
	VkSubmitInfo2 info = {};
	info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	info.pNext = nullptr;

	info.waitSemaphoreInfoCount = waitSemaphoreInfo == nullptr ? 0 : 1;
	info.pWaitSemaphoreInfos = waitSemaphoreInfo;

	info.signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0 : 1;
	info.pSignalSemaphoreInfos = signalSemaphoreInfo;

	info.commandBufferInfoCount = 1;
	info.pCommandBufferInfos = cmd;

	return info;
}
//< init_submit

} // namespace vkinit