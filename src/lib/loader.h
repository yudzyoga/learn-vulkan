#pragma once

#include "device.h"
#include "types.h"
#include "vulkan/vulkan.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vulkan/vulkan_core.h>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm/gtc/type_ptr.hpp"
#include <glm/glm.hpp>

struct Texture : AllocatedImage {
	VulkanDevice *device = nullptr;
	VkImageLayout imageLayout;
	uint32_t mipLevels;
	uint32_t layerCount;
	VkDescriptorImageInfo descriptor;
	VkSampler sampler;
	uint32_t index;

	void updateDescriptor(); // TODO
	void destroy();			 // TODO
	Texture(const AllocatedImage &base, VulkanDevice *dev) : AllocatedImage(base), device(dev){};
};

struct Material {
	enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };

	VulkanDevice *device = nullptr;
	AlphaMode alphaMode = ALPHAMODE_OPAQUE;
	float alphaCutoff = 1.0f;
	float metallicFactor = 1.0f;
	float roughnessFactor = 1.0f;
	glm::vec4 baseColorFactor = glm::vec4(1.0f);

	Texture *baseColorTexture = nullptr;
	Texture *metallicRoughnessTexture = nullptr;
	Texture *normalTexture = nullptr;
	Texture *occlusionTexture = nullptr;
	Texture *emissiveTexture = nullptr;
	Texture *specularGlossinessTexture;
	Texture *diffuseTexture;

	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

	Material(VulkanDevice *device) : device(device){};
	void createDescriptorSet(VkDescriptorPool descriptorPool,
							 VkDescriptorSetLayout descriptorSetLayout,
							 uint32_t descriptorBindingFlags); // TODO
};

struct Dimensions {
	glm::vec3 min = glm::vec3(FLT_MAX);
	glm::vec3 max = glm::vec3(-FLT_MAX);
	glm::vec3 size;
	glm::vec3 center;
	float radius;
};

struct Primitive {
	uint32_t firstIndex;
	uint32_t indexCount;
	uint32_t firstVertex;
	uint32_t vertexCount;
	Material &material;

	Dimensions dimensions;

	void setDimensions(glm::vec3 min, glm::vec3 max);
	Primitive(uint32_t firstIndex, uint32_t indexCount, Material &material)
		: firstIndex(firstIndex), indexCount(indexCount), material(material){};
};

struct Mesh {
	VulkanDevice *device;
	std::vector<Primitive *> primitives;
	std::string name;

	struct UniformBuffer {
		VkBuffer buffer;
		VmaAllocation allocation;
		VkDescriptorBufferInfo descriptor;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		void *mapped;
	} uniformBuffer;

	struct UniformBlock {
		glm::mat4 matrix;
		glm::mat4 jointMatrix[64]{};
		float jointcount{0};
	} uniformBlock;

	Mesh(VulkanDevice *device, glm::mat4 matrix);
	~Mesh();
};

struct Node {
	std::string name;
	Node *parent;
	std::vector<Node *> children;

	uint32_t index;
	glm::mat4 matrix;
	Mesh *mesh;
	glm::vec3 translation{};
	glm::vec3 scale{1.0f};
	glm::quat rotation{};
	glm::mat4 localMatrix();
	glm::mat4 getMatrix();
	void update();
	~Node();
};

struct Loader {
	Loader(){};
	~Loader(){};

	VulkanDevice *device;
	VkDescriptorPool descriptorPool;

	struct Vertices {
		int count;
		AllocatedBuffer allocBuffer;
	} vertices;
	struct Indices {
		int count;
		AllocatedBuffer allocBuffer;
	} indices;

	std::vector<Node *> loaded_nodes;
	std::vector<Node *> loaded_linearNodes;

	std::vector<Texture> loaded_textures;
	std::vector<Material> loaded_materials;
	std::optional<Texture> emptyTexture;

	Dimensions dimensions;

	void load_gltf(const std::filesystem::path filepath, VulkanDevice *device);
	void load_gltf_textures(tinygltf::Model &gltfModel, VkQueue graphicsQueue);
	void load_gltf_empty_texture(VkQueue graphicsQueue);
	void load_gltf_materials(tinygltf::Model &gltfModel, VkQueue graphicsQueue);
	void load_gltf_vertex_indices(VkQueue graphicsQueue);

	void load_nodes(Node *parent, const tinygltf::Node &node, uint32_t nodeIndex,
					const tinygltf::Model &model, std::vector<uint32_t> &indexBuffer,
					std::vector<Vertex> &vertexBuffer, float globalscale);

	void get_scene_dimensions();
	void get_node_dimensions(Node *node, glm::vec3 &min, glm::vec3 &max);
	void prepare_node_descriptor(Node *node, VkDescriptorSetLayout descriptorSetLayout);

	Texture *getTexture(uint32_t index);
};