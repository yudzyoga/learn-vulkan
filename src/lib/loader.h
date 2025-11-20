#pragma once

#include "device.h"
#include "types.h"
#include "vulkan/vulkan.h"
#include <filesystem>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

struct Texture : AllocatedImage {
	VulkanDevice *device = nullptr;
	VkImageLayout imageLayout;
	uint32_t mipLevels;
	uint32_t layerCount;
	VkDescriptorImageInfo descriptor;
	VkSampler sampler;
	uint32_t index;
	void updateDescriptor();
	void destroy();

	Texture(const AllocatedImage &base, VulkanDevice *dev) : AllocatedImage(base), device(dev){};
};

struct Loader {
	std::vector<Texture> loaded_textures;

	VulkanDevice *device;
	void load_gltf(const std::filesystem::path filepath, VulkanDevice *device);
	void load_gltf_texture(tinygltf::Model &gltfModel, VkQueue graphicsQueue);
};