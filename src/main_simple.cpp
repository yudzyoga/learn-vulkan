#include <cstdint>
#include <cstring>
#include <vector>
#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

const uint32_t HEIGHT = 600;
const uint32_t WIDTH = 800;
const std::vector<const char *> validationLayers = {
	"VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
// * if Release (-DCMAKE_BUILD_TYPE=Release)
const bool enableValidationLayers = false;
#else
// * if Debug (-DCMAKE_BUILD_TYPE=Debug)
const bool enableValidationLayers = true;
#endif

class HelloTriangleApplication {
  public:
	void run() {
		initWindow();
		initVulkan();
		mainLoop();
		cleanup();
	}

  private:
	void initWindow() {
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
	}

	void initVulkan() {
		std::cout << "[INFO] initVulkan - createInstance()" << std::endl;
		createInstance();

		std::cout << "[INFO] initVulkan - setupDebugMessenger()" << std::endl;
		setupDebugMessenger();
	}

	bool checkValidationLayerSupport() {
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const auto layer : availableLayers) {
			std::cout << "    " << layer.layerName << std::endl;
		}

		for (const char *layerName : validationLayers) {
			bool layerFound = false;

			for (const auto &layerProperties : availableLayers) {
				if (strcmp(layerName, layerProperties.layerName) == 0) {
					layerFound = true;
					break;
				}
			}
			if (!layerFound) {
				return false;
			}
		}

		return true;
	}

	std::vector<const char *> getRequiredExtensions() {
		uint32_t glfwExtensionCount = 0;
		const char **glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
		std::vector<const char *> extensions(
			glfwExtensions, glfwExtensions + glfwExtensionCount);

		if (enableValidationLayers) {
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		return extensions;
	};

	// VKAPI_ATTR and VKAPI_CALL ensure the right signature to call it
	static VKAPI_ATTR VkBool32 VKAPI_CALL
	debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
				  VkDebugUtilsMessageTypeFlagsEXT messageType,
				  const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
				  void *pUserData) {

		if (messageSeverity >=
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
			// message is important enough to show
		}

		std::cerr << "validation layer: " << pCallbackData->pMessage
				  << std::endl;

		return VK_FALSE;
	}

	VkResult CreateDebugUtilsMessengerEXT(
		VkInstance instance,
		const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
		const VkAllocationCallbacks *pAllocator,
		VkDebugUtilsMessengerEXT *pDebugMessenger) {
		auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
			instance, "vkCreateDebugUtilsMessengerEXT");
		if (func != nullptr) {
			return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
		} else {
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}
	}

	void
	DestroyDebugUtilsMessengerEXT(VkInstance instance,
								  VkDebugUtilsMessengerEXT debugMessenger,
								  const VkAllocationCallbacks *pAllocator) {
		auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
			instance, "vkDestroyDebugUtilsMessengerEXT");
		if (func != nullptr) {
			func(instance, debugMessenger, pAllocator);
		}
	}

	void populateDebugMessengerCreateInfo(
		VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
		createInfo = {};
		createInfo.sType =
			VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity =
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = debugCallback;
	}

	void setupDebugMessenger() {
		if (!enableValidationLayers)
			return;

		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		populateDebugMessengerCreateInfo(createInfo);
		createInfo.pUserData = nullptr; // Optional

		if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr,
										 &debugMessenger) != VK_SUCCESS) {
			throw std::runtime_error("failed to set up debug messenger!");
		}
	}

	void createInstance() {
		if (enableValidationLayers && !checkValidationLayerSupport()) {
			throw std::runtime_error(
				"    validation layers requested, but not available!");
		}

		// * generate application info
		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Hello Triangle";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "No Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		// * manage extension
		// uint32_t glfwExtensionCount = 0;
		// const char **glfwExtensions;
		// glfwExtensions =
		// glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		// * generate instance info
		VkInstanceCreateInfo createInfo{};
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		auto extensions = getRequiredExtensions();
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount =
			static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();
		if (enableValidationLayers) {
			createInfo.enabledLayerCount =
				static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();

			populateDebugMessengerCreateInfo(debugCreateInfo);
			createInfo.pNext =
				(VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
		} else {
			createInfo.enabledLayerCount = 0;
			createInfo.pNext = nullptr;
		}

		// * handle to the instance will be stored. return VK_SUCCESS or an
		// error code.
		if (vkCreateInstance(&createInfo, nullptr, &instance)) {
			throw std::runtime_error("Failed to create instance!");
		}

		// * check extension
		// uint32_t extensionCount = 0;
		// vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
		// 									   nullptr);

		// std::vector<VkExtensionProperties> extensions(extensionCount);
		// vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
		// 									   extensions.data());

		// std::cout << "    available extensions:\n";
		// for (const auto &extension : extensions) {
		// 	std::cout << "    \t" << extension.extensionName << "\n";
		// }
		// std::cout << std::endl;

		std::cout << "enableValidationLayers: " << enableValidationLayers
				  << std::endl;
	}

	void mainLoop() {
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
		}
	}

	void cleanup() {
		std::cout << "[INFO] cleanup()" << std::endl;

		if (enableValidationLayers) {
			// DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
		}

		vkDestroyInstance(instance, nullptr);

		// windowing
		glfwDestroyWindow(window);
		glfwTerminate();
	}

	const uint32_t WIDTH = 600;
	const uint32_t HEIGHT = 600;
	GLFWwindow *window;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessenger;
};

int main() {
	HelloTriangleApplication app;

	try {
		app.run();
	} catch (const std::exception &e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}