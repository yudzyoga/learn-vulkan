#include "device.h"
#include "initializers.h"
#include <vulkan/vulkan_core.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
// #include "tiny_gltf.h"

#define STB_IMAGE_IMPLEMENTATION
// #include "stb/stb_image.h"

#include "loader.h"

void Loader::load_gltf_texture(tinygltf::Model &gltfModel, VkQueue graphicsQueue) {
	for (tinygltf::Image &gltfimage : gltfModel.images) {
		// Texture tex;

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
		VkExtent3D extent{.width = static_cast<uint32_t>(gltfimage.width),
						  .height = static_cast<uint32_t>(gltfimage.height),
						  .depth = 1};
		uint32_t mipLevels =
			static_cast<uint32_t>(floor(log2(std::max(extent.width, extent.height))) + 1.0);

		fmt::println("{} miplevels", mipLevels);
		// ! end texture loading

		// * start create buffer
		AllocatedBuffer stagingBuffer = this->device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bufferSize, VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(stagingBuffer.info.pMappedData, buffer, bufferSize);

		// * start create image
		AllocatedImage stagingImagez = this->device->createImage(
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT,
			extent, format, true);

		// * initialize tex2 struct, storing all necessary informations
		Texture texture(stagingImagez, this->device);
		// texture.mipLevels = mipLevels;

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
				.image = texture.image,
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
			.imageExtent = extent};

		vkCmdCopyBufferToImage(copyCmd, stagingBuffer.buffer, texture.image,
							   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

		{
			VkImageMemoryBarrier imageMemoryBarrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.image = texture.image,
				.subresourceRange = subresourceRange,
			};
			vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
								 &imageMemoryBarrier);
		}

		device->flushCommandBuffer(copyCmd, graphicsQueue);
		device->destroyBuffer(stagingBuffer);

		// TODO: mipmaps blit
		// Generate the mip chain (glTF uses jpg and png, so we need to create this
		// manually)
		VkCommandBuffer blitCmd =
			device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		for (uint32_t i = 1; i < mipLevels; i++) {
			VkImageBlit imageBlit{};
			imageBlit.srcSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = i - 1,
				.layerCount = 1,
			};
			imageBlit.srcOffsets[1] = {.x = int32_t(extent.width >> (i - 1)),
									   .y = int32_t(extent.height >> (i - 1)),
									   .z = 1};
			imageBlit.dstSubresource = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = i,
				.layerCount = 1,
			};
			imageBlit.dstOffsets[1] = {
				.x = int32_t(extent.width >> i), .y = int32_t(extent.height >> i), .z = 1};

			VkImageSubresourceRange mipSubRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
												.baseMipLevel = i,
												.levelCount = 1,
												.layerCount = 1};
			{
				VkImageMemoryBarrier imageMemoryBarrier{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.srcAccessMask = 0,
					.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.image = texture.image,
					.subresourceRange = mipSubRange};
				vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
									 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
									 &imageMemoryBarrier);
			}
			vkCmdBlitImage(blitCmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						   texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit,
						   VK_FILTER_LINEAR);
			{
				VkImageMemoryBarrier imageMemoryBarrier{
					.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
					.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					.image = texture.image,
					.subresourceRange = mipSubRange};
				vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
									 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
									 &imageMemoryBarrier);
			}
		}

		subresourceRange.levelCount = mipLevels;
		texture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		{
			VkImageMemoryBarrier imageMemoryBarrier{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
				.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				.image = texture.image,
				.subresourceRange = subresourceRange};
			vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
								 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
								 1, &imageMemoryBarrier);
		}

		// remove buffer
		if (deleteBuffer) {
			delete[] buffer;
		}

		// * execute blit command buffer
		device->flushCommandBuffer(blitCmd, graphicsQueue, true);

		VkSamplerCreateInfo samplerInfo{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,
			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
			.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
			.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
			// .anisotropyEnable = VK_TRUE,
			// .maxAnisotropy = 8.0f,
			.compareOp = VK_COMPARE_OP_NEVER,
			.maxLod = (float)mipLevels,
			.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
		};
		VK_CHECK(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &texture.sampler));

		VkImageViewCreateInfo viewInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
									   .image = texture.image,
									   .viewType = VK_IMAGE_VIEW_TYPE_2D,
									   .format = format,
									   .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
															.levelCount = mipLevels,
															.layerCount = 1}};
		VK_CHECK(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &texture.imageView));

		texture.descriptor.sampler = texture.sampler;
		texture.descriptor.imageView = texture.imageView;
		texture.descriptor.imageLayout = texture.imageLayout;
		texture.mipLevels = mipLevels;
		texture.layerCount = 1;
		texture.index = static_cast<uint32_t>(loaded_textures.size());

		// TODO: remaining stuff for texture
		// TODO: make this as another loader for gltf (load_textures -> return
		// std::vector<Textures>)

		loaded_textures.push_back(texture);
	}
}

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

	VkQueue graphicsQueue = device->getQueue(vkb::QueueType::graphics);

	// * 1. load images
	load_gltf_texture(gltfModel, graphicsQueue);

	// * 2.
}
