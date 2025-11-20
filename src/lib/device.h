#pragma once

#include "vulkan/vulkan.h"

#include "fmt/base.h"
#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

#include "VkBootstrap.h"
#include "types.h"

struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VmaAllocation allocation;
	VkExtent3D imageExtent;
	VkFormat imageFormat;
};

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo info;
};

struct VulkanDevice {
	vkb::Device vkbDevice;
	VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
	VkDevice logicalDevice{VK_NULL_HANDLE};

	VkPhysicalDeviceProperties properties{};
	VkPhysicalDeviceFeatures features{};
	VkPhysicalDeviceFeatures enabledFeatures{};

	std::vector<VkQueueFamilyProperties> queueFamilyProperties;
	std::vector<std::string> supportedExtensions;
	VkCommandPool commandPool{VK_NULL_HANDLE};

	// struct {
	// 	uint32_t graphics;
	// 	uint32_t compute;
	// 	uint32_t transfer;
	// } queueFamilyIndices;
	// VkQueue graphicsQueue;
	// uint32_t graphicsQueueFamily;

	VmaAllocator vmaAllocator;

	operator VkDevice() const { return logicalDevice; };
	explicit VulkanDevice(vkb::Instance &vkbInstance, VkSurfaceKHR surface);
	~VulkanDevice();

	VkQueue getQueue(vkb::QueueType type) const;
	uint32_t getQueueFamilyIndex(vkb::QueueType type) const;

	AllocatedBuffer createBuffer(VkBufferUsageFlags usageFlags, VkDeviceSize allocSize,
								 VmaMemoryUsage memoryUsage);
	void destroyBuffer(const AllocatedBuffer &buffer);
	void copyBuffer();

	AllocatedImage createImage(VkImageUsageFlags usageFlags, VkExtent3D extentSize, VkFormat format,
							   bool mipmapped);
	VkCommandPool createCommandPool(uint32_t queueFamilyIndex,
									VkCommandPoolCreateFlags createFlags);
	VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, bool begin);
	void flushCommandBuffer(VkCommandBuffer cmdBuffer, VkQueue queue, bool free = true);
};