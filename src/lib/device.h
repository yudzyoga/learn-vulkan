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

struct VulkanDevice {

#define VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME "VK_KHR_acceleration_structure"
#define VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME "VK_KHR_ray_tracing_pipeline"
#define VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME "VK_KHR_buffer_device_address"
#define VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME "VK_KHR_deferred_host_operations"
#define VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME "VK_EXT_descriptor_indexing"
#define VK_KHR_SPIRV_1_4_EXTENSION_NAME "VK_KHR_spirv_1_4"
#define VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME "VK_KHR_shader_float_controls"

	vkb::Device vkbDevice;
	VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
	VkDevice logicalDevice{VK_NULL_HANDLE};

	VkPhysicalDeviceProperties properties{};
	VkPhysicalDeviceFeatures features{};

	VkPhysicalDeviceFeatures enabledFeatures{};
	std::vector<const char *> enabledDeviceExtensions;
	std::vector<const char *> enabledInstanceExtensions;

	VkQueue graphicsQueue;
	uint32_t graphicsQueueFamily;

	std::vector<VkQueueFamilyProperties> queueFamilyProperties;
	std::vector<std::string> supportedExtensions;
	VkCommandPool commandPool{VK_NULL_HANDLE};

	VkPhysicalDeviceMemoryProperties memoryProperties{};

	VmaAllocator vmaAllocator;

	operator VkDevice() const { return logicalDevice; };
	explicit VulkanDevice(vkb::Instance &vkbInstance, VkSurfaceKHR surface);
	~VulkanDevice();

	VkQueue getQueue(vkb::QueueType type) const;
	uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound = nullptr) const;
	uint32_t getQueueFamilyIndex(vkb::QueueType type) const;

	AllocatedBuffer createBuffer(VkBufferUsageFlags usageFlags, VmaMemoryUsage memoryUsage, VkDeviceSize allocSize,
								 VmaAllocationCreateFlags vmaFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT);
	void destroyBuffer(const AllocatedBuffer &buffer);
	void copyBuffer();

	AllocatedImage createImage(VkImageUsageFlags usageFlags, VmaMemoryUsage memoryUsage, VkExtent3D extentSize,
							   VkFormat format, bool mipmapped);
	VkCommandPool createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags);
	VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, bool begin);
	void flushCommandBuffer(VkCommandBuffer cmdBuffer, VkQueue queue, bool free = true);

	uint64_t get_buffer_device_address(VkBuffer buffer);
};