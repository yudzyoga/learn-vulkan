#pragma once

#include "device.h"
#include "types.h"
#include "vulkan/vulkan.h"
#include <filesystem>

struct Loader {
	std::vector<Texture> loaded_materials;

	VulkanDevice *device;
	void load_gltf(const std::filesystem::path filepath, VulkanDevice *device);
};