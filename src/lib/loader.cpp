#include "device.h"
#include "initializers.h"
#include "types.h"
#include <cstdint>
#include <vulkan/vulkan_core.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_IMPLEMENTATION
// #include "tiny_gltf.h"

#define STB_IMAGE_IMPLEMENTATION
// #include "stb/stb_image.h"

#include "loader.h"

VkDescriptorSetLayout descriptorSetLayoutImage = VK_NULL_HANDLE;
VkDescriptorSetLayout descriptorSetLayoutUbo = VK_NULL_HANDLE;
VkMemoryPropertyFlags memoryPropertyFlags = 0;
uint32_t descriptorBindingFlags = DescriptorBindingFlags::ImageBaseColor;

void Primitive::setDimensions(glm::vec3 min, glm::vec3 max) {
	dimensions.min = min;
	dimensions.max = max;
	dimensions.size = max - min;
	dimensions.center = (min + max) / 2.0f;
	dimensions.radius = glm::distance(min, max) / 2.0f;
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
	load_gltf_textures(gltfModel, graphicsQueue);
	load_gltf_empty_texture(graphicsQueue);

	// * 2. load materials
	load_gltf_materials(gltfModel, graphicsQueue);

	// * 3. load nodes
	const tinygltf::Scene &scene =
		gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];

	auto scale = 1.0f;
	for (size_t i = 0; i < scene.nodes.size(); i++) {
		const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
		load_nodes(nullptr, node, scene.nodes[i], gltfModel, indexBuffer, vertexBuffer, scale);
	}

	// * additional preprocessing
	const bool is_PreTransformVertices = false;
	const bool is_PreMultiplyVertexColors = false;
	const bool is_FlipY = false;

	if (is_PreTransformVertices || is_PreMultiplyVertexColors || is_FlipY) {
		for (Node *node : loaded_linearNodes) {
			if (node->mesh) {
				const glm::mat4 localMatrix = node->getMatrix();
				for (Primitive *primitive : node->mesh->primitives) {
					for (uint32_t i = 0; i < primitive->vertexCount; i++) {
						Vertex &vertex = vertexBuffer[primitive->firstVertex + i];
						// Pre-transform vertex positions by node-hierarchy
						if (is_PreTransformVertices) {
							vertex.pos = glm::vec3(localMatrix * glm::vec4(vertex.pos, 1.0f));
							vertex.normal = glm::normalize(glm::mat3(localMatrix) * vertex.normal);
						}
						// Flip Y-Axis of vertex positions
						if (is_FlipY) {
							vertex.pos.y *= -1.0f;
							vertex.normal.y *= -1.0f;
						}
						// Pre-Multiply vertex colors with material base color
						if (is_PreMultiplyVertexColors) {
							vertex.color = primitive->material.baseColorFactor * vertex.color;
						}
					}
				}
			}
		}
	}

	// for (auto &extension : gltfModel.extensionsUsed) {
	// 	if (extension == "KHR_materials_pbrSpecularGlossiness") {
	// 		std::cout << "Required extension: " << extension;
	// 		metallicRoughnessWorkflow = false;
	// 	}
	// }

	fmt::println("The model has {} textures", gltfModel.textures.size());
	fmt::println("The model has {} materials", gltfModel.materials.size());
	fmt::println("The model has {} nodes", gltfModel.nodes.size());
	fmt::println("The model has {} vertices", vertexBuffer.size());
	fmt::println("The model has {} indices", indexBuffer.size());

	size_t vertexBufferSize = vertexBuffer.size() * sizeof(Vertex);
	size_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);
	indices.count = static_cast<uint32_t>(indexBuffer.size());
	vertices.count = static_cast<uint32_t>(vertexBuffer.size());

	assert((vertexBufferSize > 0) && (indexBufferSize > 0));

	// * start create vertex and indices buffer
	AllocatedBuffer vertexStaging = this->device->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, vertexBufferSize);

	AllocatedBuffer indexStaging = this->device->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, indexBufferSize);

	vertices.allocBuffer = this->device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
														  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
													  VMA_MEMORY_USAGE_GPU_ONLY, vertexBufferSize);

	indices.allocBuffer = this->device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
														 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
													 VMA_MEMORY_USAGE_GPU_ONLY, indexBufferSize);

	memcpy(vertexStaging.info.pMappedData, vertexBuffer.data(), vertexBufferSize);
	memcpy(indexStaging.info.pMappedData, indexBuffer.data(), indexBufferSize);

	// * start new command buffer
	VkCommandBuffer copyCmd =
		this->device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	VkBufferCopy copyRegion = {};

	copyRegion.size = vertexBufferSize;
	vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, vertices.allocBuffer.buffer, 1, &copyRegion);

	copyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(copyCmd, indexStaging.buffer, indices.allocBuffer.buffer, 1, &copyRegion);

	this->device->flushCommandBuffer(copyCmd, graphicsQueue, true);

	this->device->destroyBuffer(vertexStaging);
	this->device->destroyBuffer(indexStaging);

	get_scene_dimensions();

	// ! RETRY UNDERSTANDING
	// add setup descriptors
	// Setup descriptors
	uint32_t uboCount{0};
	uint32_t imageCount{0};
	for (auto &node : loaded_linearNodes) {
		if (node->mesh) {
			uboCount++;
		}
	}
	for (auto &material : loaded_materials) {
		if (material.baseColorTexture != nullptr) {
			imageCount++;
		}
	}
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uboCount},
	};
	if (imageCount > 0) {
		if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
			poolSizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount});
		}
		if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
			poolSizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageCount});
		}
	}

	// ! RETRY UNDERSTANDING
	VkDescriptorPoolCreateInfo descriptorPoolCI{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = uboCount + imageCount,
		.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
		.pPoolSizes = poolSizes.data()};
	VK_CHECK(
		vkCreateDescriptorPool(device->logicalDevice, &descriptorPoolCI, nullptr, &descriptorPool));

	// ! RETRY UNDERSTANDING
	// Descriptors for per-node uniform buffers
	{
		// Layout is global, so only create if it hasn't already been created before
		if (descriptorSetLayoutUbo == VK_NULL_HANDLE) {
			VkDescriptorSetLayoutBinding setLayoutBinding{.binding = 0,
														  .descriptorType =
															  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
														  .descriptorCount = 1,
														  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT};
			VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = 1,
				.pBindings = &setLayoutBinding};
			VK_CHECK(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI,
												 nullptr, &descriptorSetLayoutUbo));
		}
		for (auto node : loaded_nodes) {
			prepare_node_descriptor(node, descriptorSetLayoutUbo);
		}
	}

	// ! RETRY UNDERSTANDING
	// Descriptors for per-material images
	{
		// Layout is global, so only create if it hasn't already been created before
		if (descriptorSetLayoutImage == VK_NULL_HANDLE) {
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
			if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
				setLayoutBindings.push_back(
					{.binding = static_cast<uint32_t>(setLayoutBindings.size()),
					 .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					 .descriptorCount = 1,
					 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT});
			}
			if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
				setLayoutBindings.push_back(
					{.binding = static_cast<uint32_t>(setLayoutBindings.size()),
					 .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					 .descriptorCount = 1,
					 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT});
			}
			VkDescriptorSetLayoutCreateInfo descriptorLayoutCI{
				.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
				.bindingCount = static_cast<uint32_t>(setLayoutBindings.size()),
				.pBindings = setLayoutBindings.data(),
			};
			VK_CHECK(vkCreateDescriptorSetLayout(device->logicalDevice, &descriptorLayoutCI,
												 nullptr, &descriptorSetLayoutImage));
		}
		for (auto &material : loaded_materials) {
			if (material.baseColorTexture != nullptr) {
				material.createDescriptorSet(descriptorPool, descriptorSetLayoutImage,
											 descriptorBindingFlags);
			}
		}
	}

	fmt::println("uboCount: {}", uboCount);
	fmt::println("imageCount: {}", imageCount);
}

void Loader::load_gltf_textures(tinygltf::Model &gltfModel, VkQueue graphicsQueue) {
	// * load textures inside the model
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
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, bufferSize);
		memcpy(stagingBuffer.info.pMappedData, buffer, bufferSize);

		// * start create image
		AllocatedImage stagingImage = this->device->createImage(
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
				VK_IMAGE_USAGE_SAMPLED_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY, extent, format, true);

		// * initialize tex2 struct, storing all necessary informations
		Texture texture(std::move(stagingImage), this->device);
		// texture.mipLevels = mipLevels;

		// * start command buffer
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

void Loader::load_gltf_empty_texture(VkQueue graphicsQueue) {
	const uint32_t width = 1, height = 1, mipLevels = 1, layerCount = 1;
	const VkExtent3D extent{.width = width, .height = height, .depth = 1};

	size_t bufferSize = width * height * 4;
	unsigned char *buffer = new unsigned char[bufferSize];

	// * start create empty buffer
	AllocatedBuffer stagingEmptyBuffer = this->device->createBuffer(
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, bufferSize);
	memcpy(stagingEmptyBuffer.info.pMappedData, buffer, bufferSize);

	// * start create empty image
	AllocatedImage stagingEmptyImage = this->device->createImage(
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY, extent, VK_FORMAT_R8G8B8A8_UNORM, false);
	emptyTexture = Texture(std::move(stagingEmptyImage), device);
	emptyTexture->mipLevels = mipLevels;
	emptyTexture->layerCount = layerCount;
	emptyTexture->imageExtent = extent;

	VkImageSubresourceRange subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
											 .baseMipLevel = 0,
											 .levelCount = 1,
											 .layerCount = 1};

	VkBufferImageCopy bufferCopyRegion{
		.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
		.imageExtent = emptyTexture->imageExtent};

	VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	{ // set image layour
		VkImageMemoryBarrier imageMemoryBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.image = emptyTexture->image,
			.subresourceRange = subresourceRange,
		};
		vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
							 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
							 &imageMemoryBarrier);
	}

	// //
	// //
	// //
	// //
	// //

	vkCmdCopyBufferToImage(copyCmd, stagingEmptyBuffer.buffer, emptyTexture->image,
						   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

	{
		VkImageMemoryBarrier imageMemoryBarrier{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			.image = emptyTexture->image,
			.subresourceRange = subresourceRange,
		};
		vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
							 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
							 &imageMemoryBarrier);
	}

	emptyTexture->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	device->flushCommandBuffer(copyCmd, graphicsQueue);
	device->destroyBuffer(stagingEmptyBuffer);

	VkSamplerCreateInfo samplerCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.maxAnisotropy = 1.0f,
		.compareOp = VK_COMPARE_OP_NEVER,
	};
	VK_CHECK(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr,
							 &emptyTexture->sampler));

	VkImageViewCreateInfo viewCreateInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = emptyTexture->image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							 .baseMipLevel = 0,
							 .levelCount = 1,
							 .baseArrayLayer = 0,
							 .layerCount = 1},
	};
	VK_CHECK(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr,
							   &emptyTexture->imageView));

	emptyTexture->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	emptyTexture->descriptor.imageView = emptyTexture->imageView;
	emptyTexture->descriptor.sampler = emptyTexture->sampler;
}

void Loader::load_nodes(Node *parent, const tinygltf::Node &node, uint32_t nodeIndex,
						const tinygltf::Model &model, std::vector<uint32_t> &indexBuffer,
						std::vector<Vertex> &vertexBuffer, float globalscale) {
	Node *newNode = new Node{};
	newNode->index = nodeIndex;
	newNode->parent = parent;
	newNode->name = node.name;
	newNode->matrix = glm::mat4(1.0f);

	// Generate local node matrix
	glm::vec3 translation = glm::vec3(0.0f);
	if (node.translation.size() == 3) {
		translation = glm::make_vec3(node.translation.data());
		newNode->translation = translation;
	}
	glm::mat4 rotation = glm::mat4(1.0f);
	if (node.rotation.size() == 4) {
		glm::quat q = glm::make_quat(node.rotation.data());
		newNode->rotation = glm::mat4(q);
	}
	glm::vec3 scale = glm::vec3(1.0f);
	if (node.scale.size() == 3) {
		scale = glm::make_vec3(node.scale.data());
		newNode->scale = scale;
	}
	if (node.matrix.size() == 16) {
		newNode->matrix = glm::make_mat4x4(node.matrix.data());
		if (globalscale != 1.0f) {
			// newNode->matrix = glm::scale(newNode->matrix, glm::vec3(globalscale));
		}
	};

	// Node with children
	if (node.children.size() > 0) {
		for (auto i = 0; i < node.children.size(); i++) {
			load_nodes(newNode, model.nodes[node.children[i]], node.children[i], model, indexBuffer,
					   vertexBuffer, globalscale);
		}
	}

	// Node contains mesh data
	if (node.mesh > -1) {
		const tinygltf::Mesh mesh = model.meshes[node.mesh];
		Mesh *newMesh = new Mesh(device, newNode->matrix);
		newMesh->name = mesh.name;
		for (size_t j = 0; j < mesh.primitives.size(); j++) {
			const tinygltf::Primitive &primitive = mesh.primitives[j];
			if (primitive.indices < 0) {
				continue;
			}
			uint32_t indexStart = static_cast<uint32_t>(indexBuffer.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
			uint32_t indexCount = 0;
			uint32_t vertexCount = 0;
			glm::vec3 posMin{};
			glm::vec3 posMax{};
			bool hasSkin = false;
			// Vertices
			{
				const float *bufferPos = nullptr;
				const float *bufferNormals = nullptr;
				const float *bufferTexCoords = nullptr;
				const float *bufferColors = nullptr;
				const float *bufferTangents = nullptr;
				uint32_t numColorComponents;
				const uint16_t *bufferJoints = nullptr;
				const float *bufferWeights = nullptr;

				// Position attribute is required
				assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

				const tinygltf::Accessor &posAccessor =
					model.accessors[primitive.attributes.find("POSITION")->second];
				const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
				bufferPos = reinterpret_cast<const float *>(
					&(model.buffers[posView.buffer]
						  .data[posAccessor.byteOffset + posView.byteOffset]));
				posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1],
								   posAccessor.minValues[2]);
				posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1],
								   posAccessor.maxValues[2]);

				if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
					const tinygltf::Accessor &normAccessor =
						model.accessors[primitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView &normView =
						model.bufferViews[normAccessor.bufferView];
					bufferNormals = reinterpret_cast<const float *>(
						&(model.buffers[normView.buffer]
							  .data[normAccessor.byteOffset + normView.byteOffset]));
				}

				if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
					const tinygltf::Accessor &uvAccessor =
						model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
					bufferTexCoords = reinterpret_cast<const float *>(
						&(model.buffers[uvView.buffer]
							  .data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				if (primitive.attributes.find("COLOR_0") != primitive.attributes.end()) {
					const tinygltf::Accessor &colorAccessor =
						model.accessors[primitive.attributes.find("COLOR_0")->second];
					const tinygltf::BufferView &colorView =
						model.bufferViews[colorAccessor.bufferView];
					// Color buffer are either of type vec3 or vec4
					numColorComponents =
						colorAccessor.type == TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 ? 3 : 4;
					bufferColors = reinterpret_cast<const float *>(
						&(model.buffers[colorView.buffer]
							  .data[colorAccessor.byteOffset + colorView.byteOffset]));
				}

				if (primitive.attributes.find("TANGENT") != primitive.attributes.end()) {
					const tinygltf::Accessor &tangentAccessor =
						model.accessors[primitive.attributes.find("TANGENT")->second];
					const tinygltf::BufferView &tangentView =
						model.bufferViews[tangentAccessor.bufferView];
					bufferTangents = reinterpret_cast<const float *>(
						&(model.buffers[tangentView.buffer]
							  .data[tangentAccessor.byteOffset + tangentView.byteOffset]));
				}

				// Skinning
				// Joints
				if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor &jointAccessor =
						model.accessors[primitive.attributes.find("JOINTS_0")->second];
					const tinygltf::BufferView &jointView =
						model.bufferViews[jointAccessor.bufferView];
					bufferJoints = reinterpret_cast<const uint16_t *>(
						&(model.buffers[jointView.buffer]
							  .data[jointAccessor.byteOffset + jointView.byteOffset]));
				}

				if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor &uvAccessor =
						model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
					const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
					bufferWeights = reinterpret_cast<const float *>(
						&(model.buffers[uvView.buffer]
							  .data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				hasSkin = (bufferJoints && bufferWeights);

				vertexCount = static_cast<uint32_t>(posAccessor.count);

				for (size_t v = 0; v < posAccessor.count; v++) {
					Vertex vert{};
					vert.pos = glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
					vert.normal = glm::normalize(glm::vec3(
						bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
					vert.uv =
						bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
					if (bufferColors) {
						switch (numColorComponents) {
						case 3:
							vert.color = glm::vec4(glm::make_vec3(&bufferColors[v * 3]), 1.0f);
							break;
						case 4:
							vert.color = glm::make_vec4(&bufferColors[v * 4]);
							break;
						}
					} else {
						vert.color = glm::vec4(1.0f);
					}
					vert.tangent = bufferTangents
									   ? glm::vec4(glm::make_vec4(&bufferTangents[v * 4]))
									   : glm::vec4(0.0f);
					vert.joint0 =
						hasSkin ? glm::vec4(glm::make_vec4(&bufferJoints[v * 4])) : glm::vec4(0.0f);
					vert.weight0 =
						hasSkin ? glm::make_vec4(&bufferWeights[v * 4]) : glm::vec4(0.0f);
					vertexBuffer.push_back(vert);
				}
			}
			// Indices
			{
				const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
				const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

				indexCount = static_cast<uint32_t>(accessor.count);

				switch (accessor.componentType) {
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
					uint32_t *buf = new uint32_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
						   accessor.count * sizeof(uint32_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
					uint16_t *buf = new uint16_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
						   accessor.count * sizeof(uint16_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
					uint8_t *buf = new uint8_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
						   accessor.count * sizeof(uint8_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
					delete[] buf;
					break;
				}
				default:
					fmt::println("Index component type {} not supported!", accessor.componentType);
					return;
				}
			}
			Primitive *newPrimitive =
				new Primitive(indexStart, indexCount,
							  primitive.material > -1 ? loaded_materials[primitive.material]
													  : loaded_materials.back());
			newPrimitive->firstVertex = vertexStart;
			newPrimitive->vertexCount = vertexCount;
			newPrimitive->setDimensions(posMin, posMax);
			newMesh->primitives.push_back(newPrimitive);
		}
		newNode->mesh = newMesh;
	}
	if (parent) {
		parent->children.push_back(newNode);
	} else {
		loaded_nodes.push_back(newNode);
	}
	loaded_linearNodes.push_back(newNode);
}

Texture *Loader::getTexture(uint32_t index) {
	if (index < loaded_textures.size()) {
		return &loaded_textures[index];
	}
	return nullptr;
}

void Loader::load_gltf_materials(tinygltf::Model &gltfModel, VkQueue graphicsQueue) {
	// * loop for each material
	for (tinygltf::Material &mat : gltfModel.materials) {
		// * initialize struct
		Material material(device);

		// * albedo information
		if (mat.values.find("baseColorTexture") != mat.values.end()) {
			material.baseColorTexture = getTexture(
				gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source);
		}

		// Metallic roughness workflow
		if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
			material.metallicRoughnessTexture = getTexture(
				gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source);
		}
		if (mat.values.find("roughnessFactor") != mat.values.end()) {
			material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
		}
		if (mat.values.find("metallicFactor") != mat.values.end()) {
			material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
		}
		if (mat.values.find("baseColorFactor") != mat.values.end()) {
			material.baseColorFactor =
				glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
		}

		// * load normal texture, if not then load empty texture instead
		// TODO: normal can be taken from triangles
		if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
			material.normalTexture = getTexture(
				gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source);
		} else {
			material.normalTexture = &emptyTexture.value();
		}

		if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
			material.emissiveTexture = getTexture(
				gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
			material.occlusionTexture = getTexture(
				gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source);
		}

		if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
			tinygltf::Parameter param = mat.additionalValues["alphaMode"];
			if (param.string_value == "BLEND") {
				material.alphaMode = Material::ALPHAMODE_BLEND;
			}
			if (param.string_value == "MASK") {
				material.alphaMode = Material::ALPHAMODE_MASK;
			}
		}
		if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
			material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
		}

		loaded_materials.push_back(material);
	}
	// Push a default material at the end of the list for meshes with no material assigned
	loaded_materials.push_back(Material(device));
}

void Loader::load_gltf_vertex_indices(VkQueue graphicsQueue) {}

void Loader::get_scene_dimensions() {
	dimensions.min = glm::vec3(FLT_MAX);
	dimensions.max = glm::vec3(-FLT_MAX);
	for (auto node : loaded_nodes) {
		get_node_dimensions(node, dimensions.min, dimensions.max);
	}
	dimensions.size = dimensions.max - dimensions.min;
	dimensions.center = (dimensions.min + dimensions.max) / 2.0f;
	dimensions.radius = glm::distance(dimensions.min, dimensions.max) / 2.0f;
}

void Loader::get_node_dimensions(Node *node, glm::vec3 &min, glm::vec3 &max) {
	if (node->mesh) {
		for (Primitive *primitive : node->mesh->primitives) {
			glm::vec4 locMin = glm::vec4(primitive->dimensions.min, 1.0f) * node->getMatrix();
			glm::vec4 locMax = glm::vec4(primitive->dimensions.max, 1.0f) * node->getMatrix();
			if (locMin.x < min.x) {
				min.x = locMin.x;
			}
			if (locMin.y < min.y) {
				min.y = locMin.y;
			}
			if (locMin.z < min.z) {
				min.z = locMin.z;
			}
			if (locMax.x > max.x) {
				max.x = locMax.x;
			}
			if (locMax.y > max.y) {
				max.y = locMax.y;
			}
			if (locMax.z > max.z) {
				max.z = locMax.z;
			}
		}
	}
	for (auto child : node->children) {
		get_node_dimensions(child, min, max);
	}
}

void Loader::prepare_node_descriptor(Node *node, VkDescriptorSetLayout descriptorSetLayout) {
	if (node->mesh) {
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = descriptorPool,
			.descriptorSetCount = 1,
			.pSetLayouts = &descriptorSetLayout};
		VK_CHECK(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo,
										  &node->mesh->uniformBuffer.descriptorSet));
		VkWriteDescriptorSet writeDescriptorSet{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
												.dstSet = node->mesh->uniformBuffer.descriptorSet,
												.dstBinding = 0,
												.descriptorCount = 1,
												.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
												.pBufferInfo =
													&node->mesh->uniformBuffer.descriptor};
		vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}
	for (auto &child : node->children) {
		prepare_node_descriptor(child, descriptorSetLayout);
	}
}

Mesh::Mesh(VulkanDevice *device, glm::mat4 matrix) {
	this->device = device;
	this->uniformBlock.matrix = matrix;

	// create UBO
	AllocatedBuffer uboAlloc = device->createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(uniformBlock));

	uniformBuffer.buffer = uboAlloc.buffer;
	uniformBuffer.allocation = uboAlloc.allocation;
	uniformBuffer.mapped = uboAlloc.info.pMappedData;
	uniformBuffer.descriptor = {uniformBuffer.buffer, 0, sizeof(uniformBlock)};
}

Mesh::~Mesh() {
	vmaDestroyBuffer(this->device->vmaAllocator, uniformBuffer.buffer, uniformBuffer.allocation);
	for (auto primitive : primitives) {
		delete primitive;
	}
}

void Texture::updateDescriptor() {
	descriptor.sampler = sampler;
	descriptor.imageView = imageView;
	descriptor.imageLayout = imageLayout;
}

void Texture::destroy() {
	if (device) {
		vmaDestroyImage(device->vmaAllocator, image, allocation);
		vkDestroyImageView(device->logicalDevice, imageView, nullptr);
		vkDestroySampler(device->logicalDevice, sampler, nullptr);
	}
}

void Material::createDescriptorSet(VkDescriptorPool descriptorPool,
								   VkDescriptorSetLayout descriptorSetLayout,
								   uint32_t descriptorBindingFlags) {
	VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &descriptorSetLayout,
	};
	VK_CHECK(
		vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &descriptorSet));
	std::vector<VkDescriptorImageInfo> imageDescriptors{};
	std::vector<VkWriteDescriptorSet> writeDescriptorSets{};

	if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
		imageDescriptors.push_back(baseColorTexture->descriptor);
		VkWriteDescriptorSet writeDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptorSet,
			.dstBinding = static_cast<uint32_t>(writeDescriptorSets.size()),
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &baseColorTexture->descriptor};
		writeDescriptorSets.push_back(writeDescriptorSet);
	}
	if (normalTexture && descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
		imageDescriptors.push_back(normalTexture->descriptor);
		VkWriteDescriptorSet writeDescriptorSet{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descriptorSet,
			.dstBinding = static_cast<uint32_t>(writeDescriptorSets.size()),
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.pImageInfo = &normalTexture->descriptor};
		writeDescriptorSets.push_back(writeDescriptorSet);
	}
	vkUpdateDescriptorSets(device->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()),
						   writeDescriptorSets.data(), 0, nullptr);
}

glm::mat4 Node::localMatrix() {
	return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) *
		   glm::scale(glm::mat4(1.0f), scale) * matrix;
}

glm::mat4 Node::getMatrix() {
	glm::mat4 m = localMatrix();
	Node *p = parent;
	while (p) {
		m = p->localMatrix() * m;
		p = p->parent;
	}
	return m;
}

void Node::update() {
	if (mesh) {
		glm::mat4 m = getMatrix();
		memcpy(mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
	}

	for (auto &child : children) {
		child->update();
	}
}

Node::~Node() {
	if (mesh) {
		delete mesh;
	}
	for (auto &child : children) {
		delete child;
	}
}
