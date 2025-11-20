#include "initializers.h"
#include <vulkan/vulkan_core.h>
#define VMA_IMPLEMENTATION
#include "device.h"

VulkanDevice::VulkanDevice(vkb::Instance &vkbInstance, VkSurfaceKHR surface) {
	// vulkan general features
	VkPhysicalDeviceFeatures features{};
	features.shaderInt64 = true;
	features.shaderStorageImageReadWithoutFormat = true;
	features.shaderStorageImageWriteWithoutFormat = true;
	features.shaderStorageImageWriteWithoutFormat = true;

	// vulkan 1.3 features
	VkPhysicalDeviceVulkan13Features features13{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
	features13.dynamicRendering = true;
	features13.synchronization2 = true;

	// vulkan 1.2 features
	VkPhysicalDeviceVulkan12Features features12{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing = true;

	// use vkbootstrap to select a gpu.
	// We want a gpu that can write to the SDL surface and supports vulkan 1.3
	// with the correct features make sure we select NVIDIA's
	vkb::PhysicalDeviceSelector selector{vkbInstance};
	vkb::PhysicalDevice vkbPhysicalDevice;

	// add extensions
	selector.add_required_extensions(
		{"VK_KHR_maintenance3", "VK_EXT_descriptor_indexing", "VK_KHR_acceleration_structure",
		 "VK_KHR_ray_tracing_pipeline", "VK_KHR_buffer_device_address",
		 "VK_KHR_deferred_host_operations", "VK_EXT_descriptor_indexing", "VK_KHR_spirv_1_4",
		 "VK_KHR_shader_float_controls"});

	bool is_force_nvidia = true;
	if (is_force_nvidia) {

		auto vkbPhysicalDevices = selector.set_minimum_version(1, 3)
									  .set_required_features_13(features13)
									  .set_required_features_12(features12)
									  .set_required_features(features)
									  .set_surface(surface)
									  .select_devices()
									  .value();

		for (auto &vkbDevice : vkbPhysicalDevices) {
			if (vkbDevice.name.find("NVIDIA") != std::string::npos) {
				fmt::println("Found NVIDIA");
				vkbPhysicalDevice = vkbDevice;
				break;
			} else {
				fmt::println("NVIDIA not found.");
				continue;
			}
		}
	} else {
		// * Quick select one
		vkbPhysicalDevice = selector.set_minimum_version(1, 3)
								.set_required_features_13(features13)
								.set_required_features_12(features12)
								.set_required_features(features)
								.set_surface(surface)
								.select()
								.value();
	}
	// create the final vulkan device
	vkb::DeviceBuilder deviceBuilder{vkbPhysicalDevice};

	// build device driver
	vkbDevice = deviceBuilder.build().value();

	// Get the VkDevice handle used in the rest of a vulkan application
	logicalDevice = vkbDevice.device;
	physicalDevice = vkbPhysicalDevice.physical_device;

	// use vkbootstrap to get a Graphics queue
	// graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	// graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	// initialize the memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = physicalDevice;
	allocatorInfo.device = logicalDevice;
	allocatorInfo.instance = vkbInstance.instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &vmaAllocator);

	commandPool = createCommandPool(getQueueFamilyIndex(vkb::QueueType::graphics),
									VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	fmt::println("Initialized !!!.");
}

VkQueue VulkanDevice::getQueue(vkb::QueueType type) const {
	return vkbDevice.get_queue(type).value();
}

uint32_t VulkanDevice::getQueueFamilyIndex(vkb::QueueType type) const {
	return vkbDevice.get_queue_index(type).value();
}

VulkanDevice::~VulkanDevice() { vmaDestroyAllocator(vmaAllocator); }

AllocatedBuffer VulkanDevice::createBuffer(VkBufferUsageFlags usageFlags, VkDeviceSize allocSize,
										   VmaMemoryUsage memoryUsage) {

	// allocate buffer info
	VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = usageFlags;

	// memory allocation info using vma
	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	// allocate the buffer
	AllocatedBuffer newBuffer;
	VK_CHECK(vmaCreateBuffer(vmaAllocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer,
							 &newBuffer.allocation, &newBuffer.info));

	return newBuffer;
}

void VulkanDevice::destroyBuffer(const AllocatedBuffer &buffer) {
	vmaDestroyBuffer(vmaAllocator, buffer.buffer, buffer.allocation);
}

AllocatedImage VulkanDevice::createImage(VkImageUsageFlags usageFlags, VkExtent3D extentSize,
										 VkFormat format, bool mipmapped) {
	AllocatedImage newImage;
	newImage.imageFormat = format;
	newImage.imageExtent = extentSize;

	VkImageCreateInfo imgInfo = vkinit::image_create_info(format, usageFlags, extentSize);
	if (mipmapped) {
		imgInfo.mipLevels = 1 + static_cast<uint32_t>(std::floor(
									std::log2(std::max(extentSize.width, extentSize.height))));
	}

	// always allocate images on dedicated GPU memory
	VmaAllocationCreateInfo allocinfo = {};
	allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// allocate and create the image
	VK_CHECK(vmaCreateImage(vmaAllocator, &imgInfo, &allocinfo, &newImage.image,
							&newImage.allocation, nullptr));

	// if the format is a depth format, we will need to have it use the correct
	// aspect flag
	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT) {
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	// build a image-view for the image
	VkImageViewCreateInfo view_info =
		vkinit::imageview_create_info(format, newImage.image, aspectFlag);
	view_info.subresourceRange.levelCount = imgInfo.mipLevels;
	VK_CHECK(vkCreateImageView(logicalDevice, &view_info, nullptr, &newImage.imageView));

	return newImage;
}

void VulkanDevice::copyBuffer() {}

VkCommandPool VulkanDevice::createCommandPool(uint32_t queueFamilyIndex,
											  VkCommandPoolCreateFlags createFlags) {
	VkCommandPool cmdPool;
	VkCommandPoolCreateInfo commandPoolInfo =
		vkinit::command_pool_create_info(queueFamilyIndex, createFlags);

	VK_CHECK(vkCreateCommandPool(logicalDevice, &commandPoolInfo, nullptr, &cmdPool));
	return cmdPool;
}

VkCommandBuffer VulkanDevice::createCommandBuffer(VkCommandBufferLevel level, bool begin) {
	// initialize buffer allocation
	VkCommandBufferAllocateInfo cmdBufAllocateInfo =
		vkinit::command_buffer_allocate_info(commandPool, 1);

	// allocate command buffer
	VkCommandBuffer cmdBuffer;
	VK_CHECK(vkAllocateCommandBuffers(logicalDevice, &cmdBufAllocateInfo, &cmdBuffer));
	if (begin) {
		// start the beginning of command buffer
		VkCommandBufferBeginInfo cmdBufInfo{};
		cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
	}
	return cmdBuffer;
}

void VulkanDevice::flushCommandBuffer(VkCommandBuffer cmdBuffer, VkQueue queue, bool free) {
	if (cmdBuffer == VK_NULL_HANDLE) {
		return;
	}

	VK_CHECK(vkEndCommandBuffer(cmdBuffer));

	VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
							.commandBufferCount = 1,
							.pCommandBuffers = &cmdBuffer};

	// Create fence to ensure that the command buffer has finished executing
	VkFenceCreateInfo fenceInfo = vkinit::fence_create_info(0);
	VkFence fence;
	VK_CHECK(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence));

	// Submit to the queue
	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, fence));

	// Wait for the fence to signal that command buffer has finished executing
	VK_CHECK(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, 100000000000));
	vkDestroyFence(logicalDevice, fence, nullptr);
	if (true) {
		vkFreeCommandBuffers(logicalDevice, commandPool, 1, &cmdBuffer);
	}
}