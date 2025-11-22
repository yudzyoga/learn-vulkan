#pragma once

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/quaternion_geometric.hpp"
#include "lib/loader.h"
#include <cstdint>
#include <deque>
#include <filesystem>
#include <iostream>
#include <span>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// #include "glfw/src/internal.h"
#include <string>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vulkan/vulkan.h>

// #define GLM_FORCE_RADIANS
// #define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES

// #define STB_IMAGE_IMPLEMENTATION

// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #define TINYGLTF_IMPLEMENTATION
// #include "tiny_gltf.h"

// #define STB_IMAGE_IMPLEMENTATION
// #include "stb/stb_image.h"

#include "lib/device.h"
#include "lib/initializers.h"
// #include "lib/texture.h"
#include "lib/types.h"

#define VK_CHECK(x)                                                                                                    \
	do {                                                                                                               \
		VkResult err = x;                                                                                              \
		if (err) {                                                                                                     \
			fmt::println("Detected Vulkan error: {}", string_VkResult(err));                                           \
			abort();                                                                                                   \
		}                                                                                                              \
	} while (0)

struct DeletionQueue {
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()> &&function) { deletors.push_back(function); }

	void flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)(); // call functors
		}

		deletors.clear();
	}
};

class Camera {
  private:
	float fov;
	float znear, zfar;

	void updateViewMatrix() {
		glm::mat4 currentMatrix = matrices.view;

		glm::mat4 rotM = glm::mat4(1.0f);
		glm::mat4 transM;

		rotM = glm::rotate(rotM, glm::radians(rotation.x * (flipY ? -1.0f : 1.0f)), glm::vec3(1.0f, 0.0f, 0.0f));
		rotM = glm::rotate(rotM, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		rotM = glm::rotate(rotM, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		glm::vec3 translation = position;
		if (flipY) {
			translation.y *= -1.0f;
		}
		transM = glm::translate(glm::mat4(1.0f), translation);

		if (type == CameraType::firstperson) {
			matrices.view = rotM * transM;
		} else {
			matrices.view = transM * rotM;
		}

		viewPos = glm::vec4(position, 0.0f) * glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);

		if (matrices.view != currentMatrix) {
			updated = true;
		}
	};

  public:
	enum CameraType { lookat, firstperson };
	CameraType type = CameraType::lookat;

	glm::vec3 rotation = glm::vec3();
	glm::vec3 position = glm::vec3();
	glm::vec4 viewPos = glm::vec4();

	float rotationSpeed = 1.0f;
	float movementSpeed = 1.0f;

	bool updated = true;
	bool flipY = false;

	struct {
		glm::mat4 perspective;
		glm::mat4 view;
	} matrices;

	struct {
		bool left = false;
		bool right = false;
		bool up = false;
		bool down = false;
	} keys;

	bool moving() const { return keys.left || keys.right || keys.up || keys.down; }

	float getNearClip() const { return znear; }

	float getFarClip() const { return zfar; }

	void setPerspective(float fov, float aspect, float znear, float zfar) {
		glm::mat4 currentMatrix = matrices.perspective;
		this->fov = fov;
		this->znear = znear;
		this->zfar = zfar;
		matrices.perspective = glm::perspective(glm::radians(fov), aspect, znear, zfar);
		if (flipY) {
			matrices.perspective[1][1] *= -1.0f;
		}
		if (matrices.view != currentMatrix) {
			updated = true;
		}
	};

	void updateAspectRatio(float aspect) {
		glm::mat4 currentMatrix = matrices.perspective;
		matrices.perspective = glm::perspective(glm::radians(fov), aspect, znear, zfar);
		if (flipY) {
			matrices.perspective[1][1] *= -1.0f;
		}
		if (matrices.view != currentMatrix) {
			updated = true;
		}
	}

	void setPosition(glm::vec3 position) {
		this->position = position;
		updateViewMatrix();
	}

	void setRotation(glm::vec3 rotation) {
		this->rotation = rotation;
		updateViewMatrix();
	}

	void rotate(glm::vec3 delta) {
		this->rotation += delta;
		updateViewMatrix();
	}

	void setTranslation(glm::vec3 translation) {
		this->position = translation;
		updateViewMatrix();
	};

	void translate(glm::vec3 delta) {
		this->position += delta;
		updateViewMatrix();
	}

	void setRotationSpeed(float rotationSpeed) { this->rotationSpeed = rotationSpeed; }

	void setMovementSpeed(float movementSpeed) { this->movementSpeed = movementSpeed; }

	void update(float deltaTime) {
		updated = false;
		if (type == CameraType::firstperson) {
			if (moving()) {
				glm::vec3 camFront;
				camFront.x = -cos(glm::radians(rotation.x)) * sin(glm::radians(rotation.y));
				camFront.y = sin(glm::radians(rotation.x));
				camFront.z = cos(glm::radians(rotation.x)) * cos(glm::radians(rotation.y));
				camFront = glm::normalize(camFront);

				float moveSpeed = deltaTime * movementSpeed;

				if (keys.up)
					position += camFront * moveSpeed;
				if (keys.down)
					position -= camFront * moveSpeed;
				if (keys.left)
					position -= glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f))) * moveSpeed;
				if (keys.right)
					position += glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f))) * moveSpeed;
			}
		}
		updateViewMatrix();
	};

	// Update camera passing separate axis data (gamepad)
	// Returns true if view or position has been changed
	bool updatePad(glm::vec2 axisLeft, glm::vec2 axisRight, float deltaTime) {
		bool retVal = false;

		if (type == CameraType::firstperson) {
			// Use the common console thumbstick layout
			// Left = view, right = move

			const float deadZone = 0.0015f;
			const float range = 1.0f - deadZone;

			glm::vec3 camFront;
			camFront.x = -cos(glm::radians(rotation.x)) * sin(glm::radians(rotation.y));
			camFront.y = sin(glm::radians(rotation.x));
			camFront.z = cos(glm::radians(rotation.x)) * cos(glm::radians(rotation.y));
			camFront = glm::normalize(camFront);

			float moveSpeed = deltaTime * movementSpeed * 2.0f;
			float rotSpeed = deltaTime * rotationSpeed * 50.0f;

			// Move
			if (fabsf(axisLeft.y) > deadZone) {
				float pos = (fabsf(axisLeft.y) - deadZone) / range;
				position -= camFront * pos * ((axisLeft.y < 0.0f) ? -1.0f : 1.0f) * moveSpeed;
				retVal = true;
			}
			if (fabsf(axisLeft.x) > deadZone) {
				float pos = (fabsf(axisLeft.x) - deadZone) / range;
				position += glm::normalize(glm::cross(camFront, glm::vec3(0.0f, 1.0f, 0.0f))) * pos *
							((axisLeft.x < 0.0f) ? -1.0f : 1.0f) * moveSpeed;
				retVal = true;
			}

			// Rotate
			if (fabsf(axisRight.x) > deadZone) {
				float pos = (fabsf(axisRight.x) - deadZone) / range;
				rotation.y += pos * ((axisRight.x < 0.0f) ? -1.0f : 1.0f) * rotSpeed;
				retVal = true;
			}
			if (fabsf(axisRight.y) > deadZone) {
				float pos = (fabsf(axisRight.y) - deadZone) / range;
				rotation.x -= pos * ((axisRight.y < 0.0f) ? -1.0f : 1.0f) * rotSpeed;
				retVal = true;
			}
		} else {
			// todo: move code from example base class for look-at
		}

		if (retVal) {
			updateViewMatrix();
		}

		return retVal;
	}
};

struct DescriptorAllocator {
	struct PoolSizeRatio {
		VkDescriptorType type;
		float ratio;
	};

	void init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
	void clear_descriptors(VkDevice device);
	void destroy_pool(VkDevice device);
	VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);

	VkDescriptorPool pool;
};

// Holds information for a ray tracing acceleration structure
struct AccelerationStructure {
	VkAccelerationStructureKHR handle{VK_NULL_HANDLE};
	uint64_t deviceAddress{0};
	VkBuffer buffer{VK_NULL_HANDLE};
	VkDeviceMemory memory{VK_NULL_HANDLE};
};

struct ScratchBuffer {
	uint64_t deviceAddress{0};
	VkBuffer buffer{VK_NULL_HANDLE};
	VkDeviceMemory memory{VK_NULL_HANDLE};
};

struct FrameData {
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkSemaphore _swapchainSemaphore, _renderSemaphore;
	VkFence _renderFence;

	DeletionQueue _deletionQueue;
};

struct GeometryNode {
	uint64_t vertexBufferDeviceAddress;
	uint64_t indexBufferDeviceAddress;
	int32_t textureIndexBaseColor;
	int32_t textureIndexOcclusion;
};

class VulkanSimplePT {
  public:
	// default
	std::string title = "Vulkan Example";
	Camera camera;

	// initialization
	VulkanSimplePT();
	~VulkanSimplePT() {}

	// BLAS and TLAS
	AccelerationStructure bottomLevelAS{};
	AccelerationStructure topLevelAS{};

	// internal pipelines
	void init();
	void cleanup();
	void draw();
	void run();
	// glm::vec2 window_size;
	VkExtent2D windowExtent;

	GLFWwindow *m_window;

	// Function pointers for ray tracing related stuff
	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR{nullptr};
	PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR{nullptr};
	PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR{nullptr};
	PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR{nullptr};
	PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR{nullptr};
	PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR{nullptr};
	PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR{nullptr};
	PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR{nullptr};
	PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR{nullptr};
	PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR{nullptr};

	// Available features and properties
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{};

	// Enabled features and properties
	VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddresFeatures{};
	VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures{};
	VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerationStructureFeatures{};

	//   private:
	void init_window();
	void init_vulkan();
	void init_swapchain();
	void init_commandPool();
	void init_commandBuffer();
	void init_syncPrimitives();
	void init_rayTracingSetup();
	void load_model(std::filesystem::path scene_filename);

	// Create the acceleration structures used to render the ray traced scene
	void createBottomLevelAccelerationStructure();
	void createTopLevelAccelerationStructure();
	void createAccelerationStructureBuffer(AccelerationStructure &accelerationStructure,
										   VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo);
	ScratchBuffer createScratchBuffer(VkDeviceSize size);
	void deleteScratchBuffer(ScratchBuffer &scratchBuffer);

	bool m_isInitialized = false;
	bool m_isPrepared = false;

	VkInstance m_instance;						// Vulkan library handle
	VkDebugUtilsMessengerEXT m_debug_messenger; // Vulkan debug output handle
	VkSurfaceKHR m_surface;						// Vulkan window surface

	// VkPhysicalDevice m_physicalDevice;			// GPU chosen as the default
	// device VkDevice m_device;							// Vulkan device for
	// commands VkQueue m_graphicsQueue; uint32_t m_graphicsQueueFamily;

	VkSwapchainKHR m_swapchain;
	VkFormat m_swapchainImageFormat;
	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;
	VkExtent2D m_swapchainExtent;

	FrameData m_frame;
	DescriptorAllocator globalDescriptorAllocator;
	DeletionQueue m_mainDeletionQueue;
	AllocatedImage m_drawImage;

	VkMemoryPropertyFlags memoryPropertyFlags;
	// tinygltf::Model gltfModel;

	VulkanDevice *vkDevice;
	Loader loader;
};
