#include "main_pt_gltf.h"
#include "lib/initializers.h"
#include "lib/types.h"
#include <fmt/core.h>
#include <vk-bootstrap/src/VkBootstrap.h>
#include <vulkan/vulkan_core.h>

constexpr bool bUseValidationLayers = true;

VulkanSimplePT::VulkanSimplePT() {
	title = "Ray tracing GLTF";
	windowExtent = {width, height};

	camera.type = Camera::CameraType::lookat;
	camera.setPerspective(60.0f, (float)windowExtent.width / (float)windowExtent.height, 0.1f, 512.0f);
	camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
	camera.setTranslation(glm::vec3(0.0f, -0.1f, 1.0f));
	// camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
	// camera.setTranslation(glm::vec3(0.0f, 0.0f, -10.0f));
}

void VulkanSimplePT::init() {
	init_window();
	init_vulkan();
	init_commandPool();
	init_swapchain();
	init_commandBuffer();
	init_syncPrimitives();
	init_rayTracingSetup();
	init_others();

	// load model here
	load_model("../media/glTF/"
			   "FlightHelmet.gltf");
	createBottomLevelAccelerationStructure();
	createTopLevelAccelerationStructure();

	createStorageImage();
	createUniformBuffer();
	createRayTracingPipeline();
	createShaderBindingTables();
	createDescriptorSets();

	// end
	m_isInitialized = true;
}

void VulkanSimplePT::init_window() {
	fmt::println("[INFO] Run init_window");
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	// glfwWindowHint(GLFW_RESIZABLE,
	// GLFW_TRUE);

	m_window = glfwCreateWindow((float)windowExtent.width, (float)windowExtent.height, "Vulkan", nullptr, nullptr);
	glfwSetWindowUserPointer(m_window, this);
}

void VulkanSimplePT::init_vulkan() {
	fmt::println("[INFO] Run init_vulkan");
	// bootstrap stuff
	vkb::InstanceBuilder builder;

	// make the vulkan instance, with
	// basic debug features
	auto inst_ret = builder.set_app_name("My Vulkan Application")
						.request_validation_layers(bUseValidationLayers)
						.use_default_debug_messenger()
						.require_api_version(1, 3, 0)
						.build();

	vkb::Instance vkb_inst = inst_ret.value();

	// grab the instance
	m_instance = vkb_inst.instance;
	m_debug_messenger = vkb_inst.debug_messenger;

	/*
			glfw give native OS handle,
	   which initialized as window. but
	   vulkan has no idea about this
	   therefore there should be an
	   internal representation from
	   vulkan to determine the place to
	   really represent the image
	   through "surface". therefore we
	   need it!
		*/
	if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS) {
		throw std::runtime_error("failed to create window "
								 "surface!");
	}

	vkDevice = new VulkanDevice(vkb_inst, m_surface);
}

void VulkanSimplePT::init_swapchain() {
	fmt::println("[INFO] Run init_swapchain");

	// ! CREATE SWAPCHAIN

	// generate swapchain
	vkb::SwapchainBuilder swapchainBuilder{vkDevice->physicalDevice, vkDevice->logicalDevice, m_surface};

	// fill the format for RGBA image
	m_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain =
		swapchainBuilder
			//.use_default_format_selection()
			.set_desired_format(
				VkSurfaceFormatKHR{.format = m_swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
			// use vsync present mode
			.set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
			.set_desired_extent(windowExtent.width, windowExtent.height)
			.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
			.build()
			.value();

	// store the size
	m_swapchainExtent = vkbSwapchain.extent;
	// store swapchain and its related
	// images
	m_swapchain = vkbSwapchain.swapchain;
	m_swapchainImages = vkbSwapchain.get_images().value();
	m_swapchainImageViews = vkbSwapchain.get_image_views().value();

	fmt::println("{} number of image views", m_swapchainImageViews.size());

	// ! CONTINUE INIT
	// draw image size will match the
	// window
	VkExtent3D drawImageExtent = {windowExtent.width, windowExtent.height, 1};

	// hardcoding the draw format to 32
	// bit float
	m_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	m_drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageCreateInfo rimg_info = vkinit::image_create_info(m_drawImage.imageFormat, drawImageUsages, drawImageExtent);

	// for the draw image, we want to
	// allocate it from gpu local memory
	VmaAllocationCreateInfo rimg_allocinfo = {};
	rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// allocate and create the image
	vmaCreateImage(vkDevice->vmaAllocator, &rimg_info, &rimg_allocinfo, &m_drawImage.image, &m_drawImage.allocation,
				   nullptr);

	// build a image-view for the draw
	// image to use for rendering
	VkImageViewCreateInfo rview_info =
		vkinit::imageview_create_info(m_drawImage.imageFormat, m_drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(vkDevice->logicalDevice, &rview_info, nullptr, &m_drawImage.imageView));

	// add to deletion queues
	m_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(vkDevice->logicalDevice, m_drawImage.imageView, nullptr);
		vmaDestroyImage(vkDevice->vmaAllocator, m_drawImage.image, m_drawImage.allocation);
	});
}

void VulkanSimplePT::init_commandPool() {
	// create a command pool for commands submitted to the graphics queue.
	// we also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(
		vkDevice->graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	// for (int i = 0; i < maxConcurrentFrames; i++) {
	// 	VK_CHECK(vkCreateCommandPool(vkDevice->logicalDevice, &commandPoolInfo, nullptr, &m_frame[i]._commandPool));

	// 	// allocate the default command buffer that we will use for rendering
	// 	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(m_frame[i]._commandPool, 1);

	// 	VK_CHECK(vkAllocateCommandBuffers(vkDevice->logicalDevice, &cmdAllocInfo, &m_frame[i]._mainCommandBuffer));

	// 	m_mainDeletionQueue.push_function(
	// 		[=]() { vkDestroyCommandPool(vkDevice->logicalDevice, m_frame[i]._commandPool, nullptr); });
	// }
}

void VulkanSimplePT::init_commandBuffer() {
	// allocate the default command
	// buffer that we will use for
	// rendering
	// VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(m_frame._commandPool, 1);

	// VK_CHECK(vkAllocateCommandBuffers(vkDevice->logicalDevice, &cmdAllocInfo, &m_frame._mainCommandBuffer));
	// m_mainDeletionQueue.push_function(
	// 	[=]() { vkDestroyCommandPool(vkDevice->logicalDevice, m_frame._commandPool, nullptr); });

	VkCommandBufferAllocateInfo cmdBufAllocateInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vkDevice->commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = static_cast<uint32_t>(drawCmdBuffers.size()),
	};
	VK_CHECK(vkAllocateCommandBuffers(vkDevice->logicalDevice, &cmdBufAllocateInfo, drawCmdBuffers.data()));
}

void VulkanSimplePT::init_syncPrimitives() {
	fmt::println("[INFO] Run "
				 "init_syncPrimitives");

	// create syncronization structures one fence to control when the gpu
	// has finished rendering the frame, and 2 semaphores to syncronize
	// rendering with swapchain we want the fence to start signalled so
	// we can wait on it on the first frame

	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info(0);

	// for (int i = 0; i < maxConcurrentFrames; i++) {
	// 	VK_CHECK(vkCreateFence(vkDevice->logicalDevice, &fenceCreateInfo, nullptr, &m_frame[i]._renderFence));
	// 	VK_CHECK(
	// 		vkCreateSemaphore(vkDevice->logicalDevice, &semaphoreCreateInfo, nullptr, &m_frame[i]._swapchainSemaphore));
	// 	VK_CHECK(
	// 		vkCreateSemaphore(vkDevice->logicalDevice, &semaphoreCreateInfo, nullptr, &m_frame[i]._renderSemaphore));

	// 	m_mainDeletionQueue.push_function([=]() {
	// 		vkDestroyFence(vkDevice->logicalDevice, m_frame[i]._renderFence, nullptr);
	// 		vkDestroySemaphore(vkDevice->logicalDevice, m_frame[i]._renderSemaphore, nullptr);
	// 		vkDestroySemaphore(vkDevice->logicalDevice, m_frame[i]._swapchainSemaphore, nullptr);
	// 	});
	// }

	// Wait fences to sync command buffer access
	for (auto &fence : waitFences) {
		VK_CHECK(vkCreateFence(vkDevice->logicalDevice, &fenceCreateInfo, nullptr, &fence));
	}
	// Used to ensure that image presentation is complete before starting to submit again
	for (auto &semaphore : presentCompleteSemaphores) {
		VK_CHECK(vkCreateSemaphore(vkDevice->logicalDevice, &semaphoreCreateInfo, nullptr, &semaphore));
	}
	// Semaphore used to ensure that all commands submitted have been finished before submitting the image to the queue
	renderCompleteSemaphores.resize(m_swapchainImages.size());
	for (auto &semaphore : renderCompleteSemaphores) {
		VK_CHECK(vkCreateSemaphore(vkDevice->logicalDevice, &semaphoreCreateInfo, nullptr, &semaphore));
	}
}

void VulkanSimplePT::init_rayTracingSetup() {
	fmt::println("[INFO] Run "
				 "init_rayTracingSetup");

	// Get properties and features
	rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
	VkPhysicalDeviceProperties2 deviceProperties2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
												  .pNext = &rayTracingPipelineProperties};

	vkGetPhysicalDeviceProperties2(vkDevice->physicalDevice, &deviceProperties2);

	accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	// accelerationStructureFeatures.accelerationStructure = VK_TRUE;

	VkPhysicalDeviceFeatures2 deviceFeatures2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
											  .pNext = &accelerationStructureFeatures};
	vkGetPhysicalDeviceFeatures2(vkDevice->physicalDevice, &deviceFeatures2);

	// Get the function pointers
	// required for ray tracing
	vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkGetBufferDeviceAddressKHR"));
	vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkCmdBuildAccelerationStructuresKHR"));
	vkBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkBuildAccelerationStructuresKHR"));
	vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkCreateAccelerationStructureKHR"));
	vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkDestroyAccelerationStructureKHR"));
	vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkGetAccelerationStructureBuildSizesKHR"));
	vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkGetAccelerationStructureDeviceAddressKHR"));
	vkCmdTraceRaysKHR =
		reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkCmdTraceRaysKHR"));
	vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkGetRayTracingShaderGroupHandlesKHR"));
	vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkCreateRayTracingPipelinesKHR"));

	VK_CHECK_FUNC(vkGetBufferDeviceAddressKHR);
	VK_CHECK_FUNC(vkCmdBuildAccelerationStructuresKHR);
	VK_CHECK_FUNC(vkBuildAccelerationStructuresKHR);
	VK_CHECK_FUNC(vkCreateAccelerationStructureKHR);
	VK_CHECK_FUNC(vkDestroyAccelerationStructureKHR);
	VK_CHECK_FUNC(vkGetAccelerationStructureBuildSizesKHR);
	VK_CHECK_FUNC(vkGetAccelerationStructureDeviceAddressKHR);
	VK_CHECK_FUNC(vkCmdTraceRaysKHR);
	VK_CHECK_FUNC(vkGetRayTracingShaderGroupHandlesKHR);
	VK_CHECK_FUNC(vkCreateRayTracingPipelinesKHR);
}

void VulkanSimplePT::init_others() {
	VkDescriptorSetLayoutBinding postBinding{};
	postBinding.binding = 0;
	postBinding.descriptorCount = 1;
	postBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	postBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutCI{...};
	layoutCI.bindingCount = 1;
	layoutCI.pBindings = &postBinding;

	vkCreateDescriptorSetLayout(vkDevice->logicalDevice, &layoutCI, nullptr, &postDescriptorSetLayout);
}

void VulkanSimplePT::run() {

	double lastTime = glfwGetTime();
	int frames = 0;

	while (!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();
		render();

		double currentTime = glfwGetTime();
		frames++;
		if (currentTime - lastTime >= 1.0) { // one second passed
			double fps = frames / (currentTime - lastTime);

			// Option A: print to
			// terminal
			std::cout << "FPS: " << fps << std::endl;

			// Option B: show in window
			// title
			std::string title = "Vulkan App - FPS: " + std::to_string((int)fps);
			glfwSetWindowTitle(m_window, title.c_str());

			frames = 0;
			lastTime = currentTime;
		}
	}

	vkDeviceWaitIdle(vkDevice->logicalDevice);
}

void VulkanSimplePT::cleanup() {
	if (m_isInitialized) {
		// m_frame._deletionQueue.flush();

		m_mainDeletionQueue.flush();

		vkDeviceWaitIdle(vkDevice->logicalDevice);

		vkDestroySwapchainKHR(vkDevice->logicalDevice, m_swapchain, nullptr);

		// destroy swapchain resources
		for (int i = 0; i < m_swapchainImageViews.size(); i++) {
			vkDestroyImageView(vkDevice->logicalDevice, m_swapchainImageViews[i], nullptr);
		}

		vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	}
}

void VulkanSimplePT::load_model(std::filesystem::path scene_filename) {
	fmt::println("[INFO] Run load_model");

	memoryPropertyFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
						  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	loader.load_gltf(scene_filename, vkDevice);
}

void VulkanSimplePT::createBottomLevelAccelerationStructure() {
	// Use transform matrices from the glTF nodes
	std::vector<VkTransformMatrixKHR> transformMatrices{};
	for (auto node : loader.loaded_linearNodes) {
		if (node->mesh) {
			for (auto primitive : node->mesh->primitives) {
				if (primitive->indexCount > 0) {
					VkTransformMatrixKHR transformMatrix{};
					auto m = glm::mat3x4(glm::transpose(node->getMatrix()));
					memcpy(&transformMatrix, (void *)&m, sizeof(glm::mat3x4));
					transformMatrices.push_back(transformMatrix);
				}
			}
		}
	}

	// Transform buffer
	uint32_t bufferSize = static_cast<uint32_t>(transformMatrices.size()) * sizeof(VkTransformMatrixKHR);
	AllocatedBuffer transformBuffer =
		vkDevice->createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
								   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
							   VMA_MEMORY_USAGE_CPU_TO_GPU, bufferSize);
	memcpy(transformBuffer.info.pMappedData, transformMatrices.data(), bufferSize);

	// Build
	// One geometry per glTF node, so we can index materials using gl_GeometryIndexEXT
	std::vector<uint32_t> maxPrimitiveCounts{};
	std::vector<VkAccelerationStructureGeometryKHR> geometries{};
	std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfos{};
	std::vector<VkAccelerationStructureBuildRangeInfoKHR *> pBuildRangeInfos{};
	std::vector<GeometryNode> geometryNodes{};
	for (auto node : loader.loaded_linearNodes) {
		if (node->mesh) {
			for (auto primitive : node->mesh->primitives) {
				if (primitive->indexCount > 0) {
					VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
					VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
					VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

					vertexBufferDeviceAddress.deviceAddress = vkDevice->get_buffer_device_address(
						loader.vertices.allocBuffer.buffer); // +primitive->firstVertex * sizeof(vkglTF::Vertex);
					indexBufferDeviceAddress.deviceAddress =
						vkDevice->get_buffer_device_address(loader.indices.allocBuffer.buffer) +
						primitive->firstIndex * sizeof(uint32_t);
					transformBufferDeviceAddress.deviceAddress =
						vkDevice->get_buffer_device_address(transformBuffer.buffer) +
						static_cast<uint32_t>(geometryNodes.size()) * sizeof(VkTransformMatrixKHR);

					VkAccelerationStructureGeometryKHR geometry{};
					geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
					geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
					geometry.geometry.triangles.sType =
						VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
					geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
					geometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
					geometry.geometry.triangles.maxVertex = loader.vertices.count;
					// geometry.geometry.triangles.maxVertex = primitive->vertexCount;
					geometry.geometry.triangles.vertexStride = sizeof(Vertex);
					geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
					geometry.geometry.triangles.indexData = indexBufferDeviceAddress;
					geometry.geometry.triangles.transformData = transformBufferDeviceAddress;
					geometries.push_back(geometry);
					maxPrimitiveCounts.push_back(primitive->indexCount / 3);

					VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
					buildRangeInfo.firstVertex = 0;
					buildRangeInfo.primitiveOffset = 0; // primitive->firstIndex * sizeof(uint32_t);
					buildRangeInfo.primitiveCount = primitive->indexCount / 3;
					buildRangeInfo.transformOffset = 0;
					buildRangeInfos.push_back(buildRangeInfo);

					GeometryNode geometryNode{};
					geometryNode.vertexBufferDeviceAddress = vertexBufferDeviceAddress.deviceAddress;
					geometryNode.indexBufferDeviceAddress = indexBufferDeviceAddress.deviceAddress;
					geometryNode.textureIndexBaseColor = primitive->material.baseColorTexture->index;
					geometryNode.textureIndexOcclusion =
						primitive->material.occlusionTexture ? primitive->material.occlusionTexture->index : -1;
					geometryNodes.push_back(geometryNode);
				}
			}
		}
	}

	for (auto &rangeInfo : buildRangeInfos) {
		pBuildRangeInfos.push_back(&rangeInfo);
	}

	uint32_t bufferSizeGeometryNodes = static_cast<uint32_t>(geometryNodes.size()) * sizeof(GeometryNode);
	std::cout << "bufferSizeGeometryNodes size : " << bufferSizeGeometryNodes << std::endl;
	AllocatedBuffer stagingBuffer =
		vkDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, bufferSizeGeometryNodes);
	memcpy(stagingBuffer.info.pMappedData, geometryNodes.data(), bufferSizeGeometryNodes);

	geometryNodesBuffer =
		vkDevice->createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
								   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
							   VMA_MEMORY_USAGE_GPU_ONLY, bufferSizeGeometryNodes);

	VkQueue queue = vkDevice->graphicsQueue;
	VkCommandBuffer copyCmd = vkDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	VkBufferCopy bufferCopy = {.size = bufferSizeGeometryNodes};
	vkCmdCopyBuffer(copyCmd, stagingBuffer.buffer, geometryNodesBuffer.buffer, 1, &bufferCopy);
	vkDevice->flushCommandBuffer(copyCmd, queue);

	vkQueueWaitIdle(queue);

	vkDevice->destroyBuffer(stagingBuffer);

	// Get size info
	VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
	accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationStructureBuildGeometryInfo.geometryCount = static_cast<uint32_t>(geometries.size());
	accelerationStructureBuildGeometryInfo.pGeometries = geometries.data();

	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vkGetAccelerationStructureBuildSizesKHR(vkDevice->logicalDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
											&accelerationStructureBuildGeometryInfo, maxPrimitiveCounts.data(),
											&accelerationStructureBuildSizesInfo);

	createAccelerationStructureBuffer(bottomLevelAS, accelerationStructureBuildSizesInfo);

	VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	accelerationStructureCreateInfo.buffer = bottomLevelAS.buffer;
	accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
	accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	vkCreateAccelerationStructureKHR(vkDevice->logicalDevice, &accelerationStructureCreateInfo, nullptr,
									 &bottomLevelAS.handle);

	// Create a small scratch buffer used during build of the top level acceleration structure
	ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

	accelerationStructureBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	accelerationStructureBuildGeometryInfo.dstAccelerationStructure = bottomLevelAS.handle;
	accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

	const VkAccelerationStructureBuildRangeInfoKHR *buildOffsetInfo = buildRangeInfos.data();

	// Build the acceleration structure on the device via a one-time command buffer submission
	// Some implementations may support acceleration structure building on the host
	// (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device
	// builds
	VkCommandBuffer cmdBuf = vkDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &accelerationStructureBuildGeometryInfo, pBuildRangeInfos.data());
	// vkDevice->flushCommandBuffer(cmdBuf, vkDevice->getQueue(vkb::QueueType::graphics));

	// VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
	// accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	// accelerationDeviceAddressInfo.accelerationStructure = bottomLevelAS.handle;
	// bottomLevelAS.deviceAddress =
	// 	vkGetAccelerationStructureDeviceAddressKHR(vkDevice->logicalDevice, &accelerationDeviceAddressInfo);

	// deleteScratchBuffer(scratchBuffer);
}

void VulkanSimplePT::createTopLevelAccelerationStructure() {
	// We flip the matrix [1][1] = -1.0f to accomodate for the glTF up vector
	VkTransformMatrixKHR transformMatrix = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

	VkAccelerationStructureInstanceKHR instance{};
	instance.transform = transformMatrix;
	instance.instanceCustomIndex = 0;
	instance.mask = 0xFF;
	instance.instanceShaderBindingTableRecordOffset = 0;
	instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	instance.accelerationStructureReference = bottomLevelAS.deviceAddress;

	AllocatedBuffer instanceBuffer =
		vkDevice->createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
								   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
							   VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(VkAccelerationStructureInstanceKHR));
	memcpy(instanceBuffer.info.pMappedData, &instance, sizeof(VkAccelerationStructureInstanceKHR));

	VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
	instanceDataDeviceAddress.deviceAddress = vkDevice->get_buffer_device_address(instanceBuffer.buffer);

	VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
	accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	accelerationStructureGeometry.geometry.instances.sType =
		VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
	accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;

	// Get size info
	/*
	The pSrcAccelerationStructure, dstAccelerationStructure, and mode members of pBuildInfo are ignored. Any
	VkDeviceOrHostAddressKHR members of pBuildInfo are ignored by this command, except that the hostAddress member of
	VkAccelerationStructureGeometryTrianglesDataKHR::transformData will be examined to check if it is NULL.*
	*/
	VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
	accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationStructureBuildGeometryInfo.geometryCount = 1;
	accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

	uint32_t primitive_count = 1;

	VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
	accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vkGetAccelerationStructureBuildSizesKHR(vkDevice->logicalDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
											&accelerationStructureBuildGeometryInfo, &primitive_count,
											&accelerationStructureBuildSizesInfo);

	createAccelerationStructureBuffer(topLevelAS, accelerationStructureBuildSizesInfo);

	VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	accelerationStructureCreateInfo.buffer = topLevelAS.buffer;
	accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
	accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	vkCreateAccelerationStructureKHR(vkDevice->logicalDevice, &accelerationStructureCreateInfo, nullptr,
									 &topLevelAS.handle);

	// Create a small scratch buffer used during build of the top level acceleration structure
	ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

	VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
	accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	accelerationBuildGeometryInfo.dstAccelerationStructure = topLevelAS.handle;
	accelerationBuildGeometryInfo.geometryCount = 1;
	accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
	accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

	VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
	accelerationStructureBuildRangeInfo.primitiveCount = 1;
	accelerationStructureBuildRangeInfo.primitiveOffset = 0;
	accelerationStructureBuildRangeInfo.firstVertex = 0;
	accelerationStructureBuildRangeInfo.transformOffset = 0;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR *> accelerationBuildStructureRangeInfos = {
		&accelerationStructureBuildRangeInfo};

	// Build the acceleration structure on the device via a one-time command buffer submission
	// Some implementations may support acceleration structure building on the host
	// (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device
	// builds
	VkCommandBuffer commandBuffer = vkDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &accelerationBuildGeometryInfo,
										accelerationBuildStructureRangeInfos.data());
	vkDevice->flushCommandBuffer(commandBuffer, vkDevice->graphicsQueue);

	VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
	accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	accelerationDeviceAddressInfo.accelerationStructure = topLevelAS.handle;

	deleteScratchBuffer(scratchBuffer);
	vkDevice->destroyBuffer(instanceBuffer);
}

void VulkanSimplePT::createAccelerationStructureBuffer(AccelerationStructure &accelerationStructure,
													   VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo) {
	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = buildSizeInfo.accelerationStructureSize;
	bufferCreateInfo.usage =
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VK_CHECK(vkCreateBuffer(vkDevice->logicalDevice, &bufferCreateInfo, nullptr, &accelerationStructure.buffer));

	VkMemoryRequirements memoryRequirements{};
	vkGetBufferMemoryRequirements(vkDevice->logicalDevice, accelerationStructure.buffer, &memoryRequirements);
	VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
	memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

	VkMemoryAllocateInfo memoryAllocateInfo{};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
	memoryAllocateInfo.allocationSize = memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex =
		vkDevice->getMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK(vkAllocateMemory(vkDevice->logicalDevice, &memoryAllocateInfo, nullptr, &accelerationStructure.memory));
	VK_CHECK(
		vkBindBufferMemory(vkDevice->logicalDevice, accelerationStructure.buffer, accelerationStructure.memory, 0));
}

ScratchBuffer VulkanSimplePT::createScratchBuffer(VkDeviceSize size) {
	ScratchBuffer scratchBuffer{};

	// * create buffer object
	VkBufferCreateInfo bufferCreateInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
										.flags = 0,
										.size = size,
										.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
										.sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_CHECK(vkCreateBuffer(vkDevice->logicalDevice, &bufferCreateInfo, nullptr, &scratchBuffer.buffer));

	// * allocate memory
	VkMemoryRequirements memoryRequirements{};
	vkGetBufferMemoryRequirements(vkDevice->logicalDevice, scratchBuffer.buffer, &memoryRequirements);
	VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
													  .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR};
	VkMemoryAllocateInfo memoryAllocateInfo{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = &memoryAllocateFlagsInfo,
		.allocationSize = memoryRequirements.size,
		.memoryTypeIndex =
			vkDevice->getMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
	VK_CHECK(vkAllocateMemory(vkDevice->logicalDevice, &memoryAllocateInfo, nullptr, &scratchBuffer.memory));
	VK_CHECK(vkBindBufferMemory(vkDevice->logicalDevice, scratchBuffer.buffer, scratchBuffer.memory, 0));

	// * get deviceAddress information
	VkBufferDeviceAddressInfoKHR bufferDeviceAddresInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
														.buffer = scratchBuffer.buffer};
	scratchBuffer.deviceAddress = vkGetBufferDeviceAddressKHR(vkDevice->logicalDevice, &bufferDeviceAddresInfo);

	return scratchBuffer;
}

void VulkanSimplePT::deleteScratchBuffer(ScratchBuffer &scratchBuffer) {
	if (scratchBuffer.memory != VK_NULL_HANDLE) {
		vkFreeMemory(vkDevice->logicalDevice, scratchBuffer.memory, nullptr);
	}
	if (scratchBuffer.buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(vkDevice->logicalDevice, scratchBuffer.buffer, nullptr);
	}
}

void VulkanSimplePT::createStorageImage() {
	// load storage image
	storageImage = vkDevice->createImage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
										 VMA_MEMORY_USAGE_GPU_ONLY, {width, height, 1}, m_swapchainImageFormat, false);

	VkCommandBuffer cmdBuffer = vkDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	setImageLayout(cmdBuffer, storageImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
				   {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
	vkDevice->flushCommandBuffer(cmdBuffer, vkDevice->graphicsQueue);
}

void VulkanSimplePT::createUniformBuffer() {
	for (auto &buffer : uniformBuffers) {
		buffer = vkDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU,
										sizeof(UniformData));
	}
}

VkShaderModule VulkanSimplePT::readShader(const char *fileName, VkDevice device) {
	std::ifstream is(fileName, std::ios::binary | std::ios::in | std::ios::ate);

	if (is.is_open()) {
		size_t size = is.tellg();
		is.seekg(0, std::ios::beg);
		char *shaderCode = new char[size];
		is.read(shaderCode, size);
		is.close();

		assert(size > 0);

		VkShaderModule shaderModule;
		VkShaderModuleCreateInfo moduleCreateInfo{};
		moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleCreateInfo.codeSize = size;
		moduleCreateInfo.pCode = (uint32_t *)shaderCode;

		VK_CHECK(vkCreateShaderModule(device, &moduleCreateInfo, NULL, &shaderModule));

		delete[] shaderCode;

		return shaderModule;
	} else {
		std::cerr << "Error: Could not open shader file \"" << fileName << "\""
				  << "\n";
		return VK_NULL_HANDLE;
	}
}

VkPipelineShaderStageCreateInfo VulkanSimplePT::loadShader(std::string fileName, VkShaderStageFlagBits stage) {
	VkPipelineShaderStageCreateInfo shaderStage{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = stage, .pName = "main"};
	shaderStage.module = readShader(fileName.c_str(), vkDevice->logicalDevice);
	assert(shaderStage.module != VK_NULL_HANDLE);
	// shaderModules.push_back(shaderStage.module);
	return shaderStage;
}

void VulkanSimplePT::createRayTracingPipeline() {
	const uint32_t imageCount = static_cast<uint32_t>(loader.loaded_textures.size());

	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		// Binding 0: Top level acceleration structure
		vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
										   VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0),
		// Binding 1: Ray tracing result image
		vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 1),
		// Binding 2: Uniform buffer
		vkinit::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR, 2),
		// Binding 3: Texture image
		vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
										   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 3),
		// Binding 4: Geometry node information SSBO
		vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
										   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 4),
		// Binding 5: All images used by the glTF model
		vkinit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
										   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, 5,
										   imageCount)};

	// Unbound set
	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT setLayoutBindingFlags{};
	setLayoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
	setLayoutBindingFlags.bindingCount = 6;
	std::vector<VkDescriptorBindingFlagsEXT> descriptorBindingFlags = {
		0, 0, 0, 0, 0, VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT};
	setLayoutBindingFlags.pBindingFlags = descriptorBindingFlags.data();

	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vkinit::descriptorSetLayoutCreateInfo(setLayoutBindings);
	descriptorSetLayoutCI.pNext = &setLayoutBindingFlags;
	VK_CHECK(
		vkCreateDescriptorSetLayout(vkDevice->logicalDevice, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout));

	VkPipelineLayoutCreateInfo pipelineLayoutCI = vkinit::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
	VK_CHECK(vkCreatePipelineLayout(vkDevice->logicalDevice, &pipelineLayoutCI, nullptr, &pipelineLayout));

	/*
	Setup ray tracing shader groups
*/
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	// Ray generation group
	{
		shaderStages.push_back(loadShader("../shaders/raytracinggltf/raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR));
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		shaderGroups.push_back(shaderGroup);
	}

	// Miss group
	{
		shaderStages.push_back(loadShader("../shaders/raytracinggltf/miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		shaderGroups.push_back(shaderGroup);
		// Second shader for shadows
		shaderStages.push_back(loadShader("../shaders/raytracinggltf/shadow.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
		shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroups.push_back(shaderGroup);
	}

	// Closest hit group for doing texture lookups
	{
		shaderStages.push_back(
			loadShader("../shaders/raytracinggltf/closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
		VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
		shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
		shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
		shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
		shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
		// This group also uses an anyhit shader for doing transparency (see anyhit.rahit for details)
		shaderStages.push_back(
			loadShader("../shaders/raytracinggltf/anyhit.rahit.spv", VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
		shaderGroup.anyHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
		shaderGroups.push_back(shaderGroup);
	}

	/*
		Create the ray tracing pipeline
	*/
	VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
	rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	rayTracingPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
	rayTracingPipelineCI.pStages = shaderStages.data();
	rayTracingPipelineCI.groupCount = static_cast<uint32_t>(shaderGroups.size());
	rayTracingPipelineCI.pGroups = shaderGroups.data();
	rayTracingPipelineCI.maxPipelineRayRecursionDepth = 1;
	rayTracingPipelineCI.layout = pipelineLayout;
	VK_CHECK(vkCreateRayTracingPipelinesKHR(vkDevice->logicalDevice, VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
											&rayTracingPipelineCI, nullptr, &pipeline));
}

uint32_t alignedSize(uint32_t value, uint32_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

VkStridedDeviceAddressRegionKHR VulkanSimplePT::getSbtEntryStridedDeviceAddressRegion(VkBuffer buffer,
																					  uint32_t handleCount) {
	const uint32_t handleSizeAligned = alignedSize(rayTracingPipelineProperties.shaderGroupHandleSize,
												   rayTracingPipelineProperties.shaderGroupHandleAlignment);
	VkStridedDeviceAddressRegionKHR stridedDeviceAddressRegionKHR{
		.deviceAddress = vkDevice->get_buffer_device_address(buffer), .stride = handleSizeAligned};
	stridedDeviceAddressRegionKHR.size = handleCount * handleSizeAligned;
	return stridedDeviceAddressRegionKHR;
}

void VulkanSimplePT::createShaderBindingTable(ShaderBindingTable &shaderBindingTable, uint32_t handleCount) {
	shaderBindingTable.allocBuffer = vkDevice->createBuffer(
		VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU, rayTracingPipelineProperties.shaderGroupHandleSize * handleCount);

	// Get the strided address to be used when dispatching the rays
	shaderBindingTable.stridedDeviceAddressRegion =
		getSbtEntryStridedDeviceAddressRegion(shaderBindingTable.allocBuffer.buffer, handleCount);
}

void VulkanSimplePT::createShaderBindingTables() {
	const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
	const uint32_t handleSizeAligned = alignedSize(rayTracingPipelineProperties.shaderGroupHandleSize,
												   rayTracingPipelineProperties.shaderGroupHandleAlignment);
	const uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
	const uint32_t sbtSize = groupCount * handleSizeAligned;

	std::vector<uint8_t> shaderHandleStorage(sbtSize);
	VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(vkDevice->logicalDevice, pipeline, 0, groupCount, sbtSize,
												  shaderHandleStorage.data()));

	createShaderBindingTable(shaderBindingTables.raygen, 1);
	createShaderBindingTable(shaderBindingTables.miss, 2);
	createShaderBindingTable(shaderBindingTables.hit, 1);

	// Copy handles
	memcpy(shaderBindingTables.raygen.allocBuffer.info.pMappedData, shaderHandleStorage.data(), handleSize);
	// We are using two miss shaders, so we need to get two handles for the miss shader binding table
	memcpy(shaderBindingTables.miss.allocBuffer.info.pMappedData, shaderHandleStorage.data() + handleSizeAligned,
		   handleSize * 2);
	memcpy(shaderBindingTables.hit.allocBuffer.info.pMappedData, shaderHandleStorage.data() + handleSizeAligned * 3,
		   handleSize);
}

inline VkDescriptorBufferInfo makeBufferDescriptor(const AllocatedBuffer &buf, VkDeviceSize range = VK_WHOLE_SIZE,
												   VkDeviceSize offset = 0) {
	VkDescriptorBufferInfo info{};
	info.buffer = buf.buffer;
	info.offset = offset;
	info.range = (range == VK_WHOLE_SIZE) ? buf.info.size // VMA gives actual size
										  : range;
	return info;
}

void VulkanSimplePT::createDescriptorSets() {
	uint32_t imageCount = static_cast<uint32_t>(loader.loaded_textures.size());
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, maxConcurrentFrames},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxConcurrentFrames},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxConcurrentFrames},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		 static_cast<uint32_t>(loader.loaded_textures.size()) * maxConcurrentFrames}};
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo =
		vkinit::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames);
	VK_CHECK(vkCreateDescriptorPool(vkDevice->logicalDevice, &descriptorPoolCreateInfo, nullptr, &descriptorPool));

	VkDescriptorSetVariableDescriptorCountAllocateInfoEXT variableDescriptorCountAllocInfo{};
	uint32_t variableDescCounts[] = {imageCount};
	variableDescriptorCountAllocInfo.sType =
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
	variableDescriptorCountAllocInfo.descriptorSetCount = 1;
	variableDescriptorCountAllocInfo.pDescriptorCounts = variableDescCounts;

	// Sets per frame, just like the buffers themselves
	// Acceleration structure and images do not need to be duplicated per frame, we use the same for each descriptor to
	// keep things simple
	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo =
		vkinit::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
	// Required for the variable no. of images used by the glTF model
	descriptorSetAllocateInfo.pNext = &variableDescriptorCountAllocInfo;

	for (auto i = 0; i < maxConcurrentFrames; i++) {
		VK_CHECK(vkAllocateDescriptorSets(vkDevice->logicalDevice, &descriptorSetAllocateInfo, &descriptorSets[i]));

		VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo =
			vkinit::writeDescriptorSetAccelerationStructureKHR();
		descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
		descriptorAccelerationStructureInfo.pAccelerationStructures = &topLevelAS.handle;

		VkWriteDescriptorSet accelerationStructureWrite{};
		accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		// The specialized acceleration structure descriptor has to be chained
		accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
		accelerationStructureWrite.dstSet = descriptorSets[i];
		accelerationStructureWrite.dstBinding = 0;
		accelerationStructureWrite.descriptorCount = 1;
		accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

		VkDescriptorImageInfo storageImageDescriptor{VK_NULL_HANDLE, storageImage.imageView, VK_IMAGE_LAYOUT_GENERAL};

		// auto bufDesc = makeBufferDescriptor(uniformBuffers[i]);
		// auto geomDesc = makeBufferDescriptor(geometryNodesBuffer);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			// Binding 0: Top level acceleration structure
			accelerationStructureWrite,
			// Binding 1: Ray tracing result image
			vkinit::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor),
			// Binding 2: Uniform data
			vkinit::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2,
									   &uniformBuffers[i].descriptor),
			// Binding 4: Geometry node information SSBO
			vkinit::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4,
									   &geometryNodesBuffer.descriptor),
		};

		// Image descriptors for the variable no. of images of the glTF model
		std::vector<VkDescriptorImageInfo> textureDescriptors{};
		for (auto &texture : loader.loaded_textures) {
			VkDescriptorImageInfo descriptor{};
			descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			descriptor.sampler = texture.sampler;
			descriptor.imageView = texture.imageView;
			textureDescriptors.push_back(descriptor);
		}

		VkWriteDescriptorSet writeDescriptorImgArray{};
		writeDescriptorImgArray.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorImgArray.dstBinding = 5;
		writeDescriptorImgArray.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorImgArray.descriptorCount = imageCount;
		writeDescriptorImgArray.dstSet = descriptorSets[i];
		writeDescriptorImgArray.pImageInfo = textureDescriptors.data();
		writeDescriptorSets.push_back(writeDescriptorImgArray);

		vkUpdateDescriptorSets(vkDevice->logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()),
							   writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
	}
}

void VulkanSimplePT::render() {
	if (!m_isInitialized)
		return;
	render_prepareFrame();
	if (camera.updated) {
		// If the camera's view has been updated we need to  reset the frame accumulation (which is used for transparent
		// surfaces and anti-aliasing)
		uniformData.frame = -1;
	}
	render_updateUniformBuffers();
	render_buildCommandBuffer();
	render_submitFrame();
}

void VulkanSimplePT::render_prepareFrame(bool waitForFence) {
	// Ensure command buffer execution has finished
	if (waitForFence) {
		VK_CHECK(vkWaitForFences(vkDevice->logicalDevice, 1, &waitFences[currentBuffer], VK_TRUE, UINT64_MAX));
		VK_CHECK(vkResetFences(vkDevice->logicalDevice, 1, &waitFences[currentBuffer]));
	}

	// ! updateOverlay();
	// Acquire the next image from the swap chain
	VkResult result =
		vkAcquireNextImageKHR(vkDevice->logicalDevice, m_swapchain, UINT64_MAX,
							  presentCompleteSemaphores[currentBuffer], (VkFence) nullptr, &currentImageIndex);

	// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE)
	// If no longer optimal (VK_SUBOPTIMAL_KHR), wait until submitFrame() in case number of swapchain images will change
	// on resize
	if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			// windowResize();
		}
		return;
	} else {
		VK_CHECK(result);
	}
}

void VulkanSimplePT::render_updateUniformBuffers() {
	uniformData.projInverse = glm::inverse(camera.matrices.perspective);
	uniformData.viewInverse = glm::inverse(camera.matrices.view);
	// This value is used to accumulate multiple frames into the finale picture
	// It's required as ray tracing needs to do multiple passes for transparency
	// In this sample we use noise offset by this frame index to shoot rays for
	// transparency into different directions Once enough frames with random ray
	// directions have been accumulated, it looks like proper transparency
	uniformData.frame++;
	memcpy(uniformBuffers[currentBuffer].info.pMappedData, &uniformData, sizeof(UniformData));
}

void VulkanSimplePT::render_buildCommandBuffer() {
	// if (resized) {
	// 	handleResize();
	// }

	// get command buffer
	VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];

	// reset the command buffer to begin recording again.
	VK_CHECK(vkResetCommandBuffer(cmdBuffer, 0));

	VkCommandBufferBeginInfo cmdBufInfo = vkinit::commandBufferBeginInfo();
	cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkImageSubresourceRange subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	VK_CHECK(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

	/*
		Dispatch the ray tracing commands
	*/
	vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
	vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0, 1,
							&descriptorSets[currentBuffer], 0, 0);

	VkStridedDeviceAddressRegionKHR emptySbtEntry = {};
	vkCmdTraceRaysKHR(cmdBuffer, &shaderBindingTables.raygen.stridedDeviceAddressRegion,
					  &shaderBindingTables.miss.stridedDeviceAddressRegion,
					  &shaderBindingTables.hit.stridedDeviceAddressRegion, &emptySbtEntry, width, height, 1);

	/*
		Copy ray tracing output to swap chain image
	*/
	// Prepare ray tracing output image as transfer source
	setImageLayout(cmdBuffer, storageImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				   subresourceRange);
	// Prepare current swap chain image as transfer destination
	setImageLayout(cmdBuffer, m_swapchainImages[currentImageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
				   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

	VkImageCopy copyRegion{};
	copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	copyRegion.srcOffset = {0, 0, 0};
	copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	copyRegion.dstOffset = {0, 0, 0};
	copyRegion.extent = {width, height, 1};
	vkCmdCopyImage(cmdBuffer, storageImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				   m_swapchainImages[currentImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

	// Transition ray tracing output image back to general layout
	setImageLayout(cmdBuffer, storageImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
				   subresourceRange);
	// Transition swap chain image back for presentation
	setImageLayout(cmdBuffer, m_swapchainImages[currentImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, subresourceRange);

	// TODO: this was skipped. check!
	// drawUI(cmdBuffer, frameBuffers[currentImageIndex]);
	const VkClearColorValue defaultClearColor{0.0f, 1.0f, 0.0f};

	VkRenderingAttachmentInfo colorAttachment{};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = m_swapchainImageViews[currentImageIndex]; // ← the swapchain view
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.clearValue.color = defaultClearColor;

	VkRenderingInfo renderInfo{};
	renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderInfo.renderArea = {{0, 0}, {width, height}};
	renderInfo.layerCount = 1;
	renderInfo.colorAttachmentCount = 1;
	renderInfo.pColorAttachments = &colorAttachment;

	vkCmdBeginRendering(cmdBuffer, &renderInfo);

	// 1) Ensure swapchain image is in COLOR_ATTACHMENT_OPTIMAL before this block
	setImageLayout(cmdBuffer, m_swapchainImages[currentImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, subresourceRange);

	// 2) Begin dynamic rendering (you already do this)
	vkCmdBeginRendering(cmdBuffer, &renderInfo);

	// 3) Bind graphics post-processing pipeline (not RT pipeline)
	vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipeline);

	// 4) If pipeline expects dynamic viewport/scissor, set them:
	VkViewport vp{0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
	VkRect2D scissor{{0, 0}, {width, height}};
	vkCmdSetViewport(cmdBuffer, 0, 1, &vp);
	vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

	// 5) Bind descriptor set for post pipeline (must be graphics bind point)
	vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, postPipelineLayout, 0, 1, &postDescriptorSet, 0,
							nullptr);

	// 6) Draw full-screen triangle (no vertex buffer)
	vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

	vkCmdEndRendering(cmdBuffer);

	// 7) Transition to present AFTER rendering
	setImageLayout(cmdBuffer, m_swapchainImages[currentImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, subresourceRange);

	vkCmdEndRendering(cmdBuffer);

	VK_CHECK(vkEndCommandBuffer(cmdBuffer));
}

void VulkanSimplePT::render_submitFrame(bool skipQueueSubmit) {
	if (!skipQueueSubmit) {
		const VkPipelineStageFlags waitPipelineStage{VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
		VkSubmitInfo submitInfo{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
								.waitSemaphoreCount = 1,
								.pWaitSemaphores = &presentCompleteSemaphores[currentBuffer],
								.pWaitDstStageMask = &waitPipelineStage,
								.commandBufferCount = 1,
								.pCommandBuffers = &drawCmdBuffers[currentBuffer],
								.signalSemaphoreCount = 1,
								.pSignalSemaphores = &renderCompleteSemaphores[currentImageIndex]};
		// VkSubmitInfo2 submit = vkinit::submit_info(VkCommandBufferSubmitInfo *cmd, VkSemaphoreSubmitInfo
		// *signalSemaphoreInfo, VkSemaphoreSubmitInfo *waitSemaphoreInfo)
		VK_CHECK(vkQueueSubmit(vkDevice->graphicsQueue, 1, &submitInfo, waitFences[currentBuffer]));
	}

	VkPresentInfoKHR presentInfo{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
								 .waitSemaphoreCount = 1,
								 .pWaitSemaphores = &renderCompleteSemaphores[currentImageIndex],
								 .swapchainCount = 1,
								 .pSwapchains = &m_swapchain,
								 .pImageIndices = &currentImageIndex};
	VkResult result = vkQueuePresentKHR(vkDevice->graphicsQueue, &presentInfo);
	// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE) or no longer optimal for
	// presentation (SUBOPTIMAL)
	// if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
	// 	// windowResize();
	// 	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
	// 		return;
	// 	}
	// } else {
	// 	VK_CHECK(result);
	// }
	// Select the next frame to render to, based on the max. no. of concurrent frames
	currentBuffer = (currentBuffer + 1) % maxConcurrentFrames;
}

void VulkanSimplePT::setImageLayout(VkCommandBuffer cmdbuffer, VkImage image, VkImageLayout oldImageLayout,
									VkImageLayout newImageLayout, VkImageSubresourceRange subresourceRange,
									VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask) {
	// Create an image barrier object
	VkImageMemoryBarrier imageMemoryBarrier = vkinit::imageMemoryBarrier();
	imageMemoryBarrier.oldLayout = oldImageLayout;
	imageMemoryBarrier.newLayout = newImageLayout;
	imageMemoryBarrier.image = image;
	imageMemoryBarrier.subresourceRange = subresourceRange;

	// Source layouts (old)
	// Source access mask controls actions that have to be finished on the old layout
	// before it will be transitioned to the new layout
	switch (oldImageLayout) {
	case VK_IMAGE_LAYOUT_UNDEFINED:
		// Image layout is undefined (or does not matter)
		// Only valid as initial layout
		// No flags required, listed only for completeness
		imageMemoryBarrier.srcAccessMask = 0;
		break;

	case VK_IMAGE_LAYOUT_PREINITIALIZED:
		// Image is preinitialized
		// Only valid as initial layout for linear images, preserves memory contents
		// Make sure host writes have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		// Image is a color attachment
		// Make sure any writes to the color buffer have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Image is a depth/stencil attachment
		// Make sure any writes to the depth/stencil buffer have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		// Image is a transfer source
		// Make sure any reads from the image have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		// Image is a transfer destination
		// Make sure any writes to the image have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// Image is read by a shader
		// Make sure any shader reads from the image have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		// Other source layouts aren't handled (yet)
		break;
	}

	// Target layouts (new)
	// Destination access mask controls the dependency for the new image layout
	switch (newImageLayout) {
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		// Image will be used as a transfer destination
		// Make sure any writes to the image have been finished
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		// Image will be used as a transfer source
		// Make sure any reads from the image have been finished
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		// Image will be used as a color attachment
		// Make sure any writes to the color buffer have been finished
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Image layout will be used as a depth/stencil attachment
		// Make sure any writes to depth/stencil buffer have been finished
		imageMemoryBarrier.dstAccessMask =
			imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// Image will be read in a shader (sampler, input attachment)
		// Make sure any writes to the image have been finished
		if (imageMemoryBarrier.srcAccessMask == 0) {
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		}
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		// Other source layouts aren't handled (yet)
		break;
	}

	// Put barrier inside setup command buffer
	vkCmdPipelineBarrier(cmdbuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
}

void VulkanSimplePT::setImageLayout(VkCommandBuffer cmdbuffer, VkImage image, VkImageAspectFlags aspectMask,
									VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
									VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask) {
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = aspectMask;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.layerCount = 1;
	setImageLayout(cmdbuffer, image, oldImageLayout, newImageLayout, subresourceRange, srcStageMask, dstStageMask);
}

VulkanSimplePT *vkpt;
int main() {
	vkpt = new VulkanSimplePT();
	vkpt->init();
	vkpt->run();
	return 0;
}
