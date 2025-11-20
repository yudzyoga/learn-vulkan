#pragma once

#include "glm/glm.hpp"
#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>

#define VK_CHECK(x)                                                                                \
	do {                                                                                           \
		VkResult err = x;                                                                          \
		if (err) {                                                                                 \
			fmt::println("Detected Vulkan error: {}", string_VkResult(err));                       \
			abort();                                                                               \
		}                                                                                          \
	} while (0)

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo info;
};

struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VmaAllocation allocation;
	VkExtent3D imageExtent;
	VkFormat imageFormat;
};
// struct Texture {
// 	VkImage image;
// 	VkImageLayout imageLayout;
// 	VkImageView view;
// 	uint32_t width, height;
// 	uint32_t mipLevels;
// 	uint32_t layerCount;
// 	VkDescriptorImageInfo descriptor;
// 	VkSampler sampler;
// 	uint32_t index;
// };

/*
	glTF default vertex layout with easy Vulkan mapping functions
*/
enum class VertexComponent { Position, Normal, UV, Color, Tangent, Joint0, Weight0 };

struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 color;
	glm::vec4 joint0;
	glm::vec4 weight0;
	glm::vec4 tangent;
	static VkVertexInputBindingDescription vertexInputBindingDescription;
	static std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions;
	static VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo;
	static VkVertexInputBindingDescription inputBindingDescription(uint32_t binding);
	static VkVertexInputAttributeDescription
	inputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component);
	static std::vector<VkVertexInputAttributeDescription>
	inputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components);
	/** @brief Returns the default pipeline vertex input state create info
	 * structure for the requested vertex components
	 */
	static VkPipelineVertexInputStateCreateInfo *
	getPipelineVertexInputState(const std::vector<VertexComponent> components);
};