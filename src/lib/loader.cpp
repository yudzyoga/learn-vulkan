#include "initializers.h"
#include <vulkan/vulkan_core.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include "loader.h"

void Loader::load_gltf(const std::filesystem::path filepath, VulkanDevice *device) {

	this->device = device;

	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF loader;

	std::string err, warn;
	bool fileLoaded = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filepath);

	if (!warn.empty()) {
		fmt::println("[WARNING] {}", warn.c_str());
	}

	if (!err.empty()) {
		fmt::println("[ERROR] {}", err.c_str());
		exit(-1);
	}

	if (!fileLoaded) {
		fmt::println("[ERROR] Failed "
					 "to parse glTF",
					 err.c_str());
		exit(-1);
	}

	std::vector<uint32_t> indexBuffer;
	std::vector<Vertex> vertexBuffer;

	// * 1. load images
	VkQueue graphicsQueue = device->getQueue(vkb::QueueType::graphics);

	std::vector<Texture> textures;
	for (tinygltf::Image &gltfimage : gltfModel.images) {
		Texture tex;

		// ! start texture loading
		unsigned char *buffer = nullptr;
		VkDeviceSize bufferSize = 0;
		bool deleteBuffer = false;
		if (gltfimage.component == 3) {
			// Most devices don't
			// support RGB only on
			// Vulkan so convert if
			// necessary
			// TODO: Check actual format
			// support and transform
			// only if required
			bufferSize = gltfimage.width * gltfimage.height * 4;
			buffer = new unsigned char[bufferSize];
			unsigned char *rgba = buffer;
			unsigned char *rgb = &gltfimage.image[0];
			for (size_t i = 0; i < gltfimage.width * gltfimage.height; ++i) {
				for (int32_t j = 0; j < 3; ++j) {
					rgba[j] = rgb[j];
				}
				rgba += 4;
				rgb += 3;
			}
			deleteBuffer = true;
		} else {
			buffer = &gltfimage.image[0];
			bufferSize = gltfimage.image.size();
		}
		assert(buffer);

		VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
		uint32_t width = gltfimage.width;
		uint32_t height = gltfimage.height;
		uint32_t mipLevels = static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

		fmt::println("{} miplevels", mipLevels);
		// ! end texture loading

		// * start create buffer
		AllocatedBuffer stagingBuffer = this->device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bufferSize, VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(stagingBuffer.info.pMappedData, buffer, bufferSize);

		// * start create image
		AllocatedImage stagingImage = this->device->createImage(
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT,
			{.width = width, .height = height, .depth = 1}, format, true);

		//
		VkCommandBuffer copyCmd =
			this->device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		VkImageSubresourceRange subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1};

		{
			VkImageMemoryBarrier imageMemoryBarrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.image = stagingImage.image,
				.subresourceRange = subresourceRange,
			};
			vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &imageMemoryBarrier);
		}

		VkBufferImageCopy bufferCopyRegion{
			.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
								 .mipLevel = 0,
								 .baseArrayLayer = 0,
								 .layerCount = 1},
			.imageExtent = {.width = width, .height = height, .depth = 1}};

		vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, stagingImage.image,
							   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

		{
			VkImageMemoryBarrier imageMemoryBarrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.image = stagingImage.image,
				.subresourceRange = subresourceRange,
			};
			vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &imageMemoryBarrier);
		}

		device->flushCommandBuffer(copyCmd, graphicsQueue);
		device->destroyBuffer(stagingBuffer);

		// TODO: mipmaps blit
		// TODO: remaining stuff for texture
		// TODO: make this as another loader for gltf (load_textures -> return
		// std::vector<Textures>)

		tex.index = static_cast<uint32_t>(textures.size());
		textures.push_back(tex);
	}
}
