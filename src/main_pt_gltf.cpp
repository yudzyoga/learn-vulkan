#include "main_pt_gltf.h"
#include "lib/loader.h"
#include <fmt/core.h>
#include <vk-bootstrap/src/VkBootstrap.h>

constexpr bool bUseValidationLayers = true;

VulkanSimplePT::VulkanSimplePT() {
	title = "Ray tracing GLTF";
	windowExtent = {640, 480};

	camera.type = Camera::CameraType::lookat;
	camera.setPerspective(60.0f, (float)windowExtent.width / (float)windowExtent.height, 0.1f,
						  512.0f);
	camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
	camera.setTranslation(glm::vec3(0.0f, -0.1f, -1.0f));
}

void VulkanSimplePT::init() {
	init_window();
	init_vulkan();
	init_commandPool();
	init_swapchain();
	init_commandBuffer();
	init_syncPrimitives();
	init_rayTracingSetup();

	// load model here
	load_model("../media/glTF/"
			   "FlightHelmet.gltf");

	// end
	m_isInitialized = true;
}

void VulkanSimplePT::init_window() {
	fmt::println("[INFO] Run init_window");
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	// glfwWindowHint(GLFW_RESIZABLE,
	// GLFW_TRUE);

	m_window = glfwCreateWindow((float)windowExtent.width, (float)windowExtent.height, "Vulkan",
								nullptr, nullptr);
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
	vkb::SwapchainBuilder swapchainBuilder{vkDevice->physicalDevice, vkDevice->logicalDevice,
										   m_surface};

	// fill the format for RGBA image
	m_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain =
		swapchainBuilder
			//.use_default_format_selection()
			.set_desired_format(VkSurfaceFormatKHR{.format = m_swapchainImageFormat,
												   .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
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

	VkImageCreateInfo rimg_info =
		vkinit::image_create_info(m_drawImage.imageFormat, drawImageUsages, drawImageExtent);

	// for the draw image, we want to
	// allocate it from gpu local memory
	VmaAllocationCreateInfo rimg_allocinfo = {};
	rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// allocate and create the image
	vmaCreateImage(vkDevice->vmaAllocator, &rimg_info, &rimg_allocinfo, &m_drawImage.image,
				   &m_drawImage.allocation, nullptr);

	// build a image-view for the draw
	// image to use for rendering
	VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(
		m_drawImage.imageFormat, m_drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(
		vkCreateImageView(vkDevice->logicalDevice, &rview_info, nullptr, &m_drawImage.imageView));

	// add to deletion queues
	m_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(vkDevice->logicalDevice, m_drawImage.imageView, nullptr);
		vmaDestroyImage(vkDevice->vmaAllocator, m_drawImage.image, m_drawImage.allocation);
	});
}

void VulkanSimplePT::init_commandPool() {
	VkCommandPoolCreateInfo commandPoolInfo =
		vkinit::command_pool_create_info(vkDevice->getQueueFamilyIndex(vkb::QueueType::graphics),
										 VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	VK_CHECK(vkCreateCommandPool(vkDevice->logicalDevice, &commandPoolInfo, nullptr,
								 &m_frame._commandPool));
	m_mainDeletionQueue.push_function(
		[=]() { vkDestroyCommandPool(vkDevice->logicalDevice, m_frame._commandPool, nullptr); });
}

void VulkanSimplePT::init_commandBuffer() {
	// allocate the default command
	// buffer that we will use for
	// rendering
	VkCommandBufferAllocateInfo cmdAllocInfo =
		vkinit::command_buffer_allocate_info(m_frame._commandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(vkDevice->logicalDevice, &cmdAllocInfo,
									  &m_frame._mainCommandBuffer));
	m_mainDeletionQueue.push_function(
		[=]() { vkDestroyCommandPool(vkDevice->logicalDevice, m_frame._commandPool, nullptr); });
}

void VulkanSimplePT::init_syncPrimitives() {
	fmt::println("[INFO] Run "
				 "init_syncPrimitives");

	// create syncronization structures
	// one fence to control when the gpu
	// has finished rendering the frame,
	// and 2 semaphores to syncronize
	// rendering with swapchain we want
	// the fence to start signalled so
	// we can wait on it on the first
	// frame
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info(0);

	VK_CHECK(
		vkCreateFence(vkDevice->logicalDevice, &fenceCreateInfo, nullptr, &m_frame._renderFence));
	VK_CHECK(vkCreateSemaphore(vkDevice->logicalDevice, &semaphoreCreateInfo, nullptr,
							   &m_frame._swapchainSemaphore));
	VK_CHECK(vkCreateSemaphore(vkDevice->logicalDevice, &semaphoreCreateInfo, nullptr,
							   &m_frame._renderSemaphore));

	m_mainDeletionQueue.push_function([=]() {
		vkDestroyFence(vkDevice->logicalDevice, m_frame._renderFence, nullptr);
		vkDestroySemaphore(vkDevice->logicalDevice, m_frame._renderSemaphore, nullptr);
		vkDestroySemaphore(vkDevice->logicalDevice, m_frame._swapchainSemaphore, nullptr);
	});
}

void VulkanSimplePT::init_rayTracingSetup() {
	fmt::println("[INFO] Run "
				 "init_rayTracingSetup");

	// Get properties and features
	rayTracingPipelineProperties.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
	VkPhysicalDeviceProperties2 deviceProperties2{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &rayTracingPipelineProperties};

	vkGetPhysicalDeviceProperties2(vkDevice->physicalDevice, &deviceProperties2);
	accelerationStructureFeatures.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	VkPhysicalDeviceFeatures2 deviceFeatures2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
											  .pNext = &accelerationStructureFeatures};
	vkGetPhysicalDeviceFeatures2(vkDevice->physicalDevice, &deviceFeatures2);

	// Get the function pointers
	// required for ray tracing
	vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkGetBufferDeviceAddre"
													 "ssKHR"));
	vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkCmdBuildAcceleration"
													 "StructuresKHR"));
	vkBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkBuildAccelerationStr"
													 "ucturesKHR"));
	vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkCreateAccelerationSt"
													 "ructureKHR"));
	vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkDestroyAccelerationS"
													 "tructureKHR"));
	vkGetAccelerationStructureBuildSizesKHR =
		reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
			vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkGetAccelerationStruc"
														 "tureBuildSizesKHR"));
	vkGetAccelerationStructureDeviceAddressKHR =
		reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
			vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkGetAccelerationStruc"
														 "ture"
														 "DeviceAddressKHR"));
	vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkCmdTraceRaysKHR"));
	vkGetRayTracingShaderGroupHandlesKHR =
		reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
			vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkGetRayTracingShaderG"
														 "roupHandlesKHR"));
	vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
		vkGetDeviceProcAddr(vkDevice->logicalDevice, "vkCreateRayTracingPipe"
													 "linesKHR"));
}

void VulkanSimplePT::run() {

	double lastTime = glfwGetTime();
	int frames = 0;

	while (!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();
		// drawFrame();

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
		m_frame._deletionQueue.flush();

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
						  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
						  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	Loader loader;
	loader.load_gltf(scene_filename, vkDevice);
}

VulkanSimplePT *vkpt;
int main() {
	vkpt = new VulkanSimplePT();
	vkpt->init();
	vkpt->run();
	return 0;
}
