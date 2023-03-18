#include "vulkan_context.h"

namespace Albedo {
namespace RHI
{
	std::vector<uint32_t> VulkanContext::s_debug_message_statistics(4, 0);

	VulkanContext::VulkanContext(GLFWwindow* window) :
		m_window{ window }
	{
		enable_validation_layers();

		create_vulkan_instance();
		create_debug_messenger();
		create_surface();
		create_physical_device();
		create_logical_device();
		create_swap_chain();
	}

	VulkanContext::~VulkanContext()
	{
		destroy_swap_chain();
		destroy_logical_device();
		destroy_surface();
		destroy_debug_messenger();
		destroy_vulkan_instance();
	}

	void VulkanContext::RecreateSwapChain()
	{
		static bool RECREATING = false;
		if (RECREATING) throw std::runtime_error("Failed to recreate the Swap Chain - more than one caller at the same time!");

		RECREATING = true;
		destroy_swap_chain();
		create_swap_chain();
		RECREATING = false;
	}

	void VulkanContext::enable_validation_layers()
	{
		if (!EnableValidationLayers) return;

		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const char* layerName : m_validation_layers)
		{
			bool layerFound = false;

			for (const auto& layerProperties : availableLayers)
			{
				if (strcmp(layerName, layerProperties.layerName) == 0)
				{
					layerFound = true;
					break;
				}
			}
			if (!layerFound) throw std::runtime_error(std::format("Failed to enable the Vulkan Validation Layer {}", layerName));
		}
	}

	void VulkanContext::create_vulkan_instance()
	{
		// Extensions
		std::vector<const char*> requiredExtensions;
		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount); // Include WSI extensions
		std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
		if (EnableValidationLayers) extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

		// Instance
		VkApplicationInfo appInfo
		{
			.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
			.pApplicationName = "Albedo RHI",
			.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
			.pEngineName = "Albedo",
			.engineVersion = VK_MAKE_VERSION(1, 0, 0),
			.apiVersion = VK_API_VERSION_1_0,
		};

		VkInstanceCreateInfo instanceCreateInfo{};
		instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instanceCreateInfo.pApplicationInfo = &appInfo;

		instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		instanceCreateInfo.ppEnabledExtensionNames = extensions.data();

		auto messengerCreateInfo = VulkanContext::GetDefaultDebuggerMessengerCreateInfo();
		if (EnableValidationLayers)
		{
			instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(m_validation_layers.size());
			instanceCreateInfo.ppEnabledLayerNames = m_validation_layers.data();
			instanceCreateInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)(&messengerCreateInfo);
		}
		else
		{
			instanceCreateInfo.enabledLayerCount = 0;
			instanceCreateInfo.ppEnabledLayerNames = nullptr;
			instanceCreateInfo.pNext = nullptr;
		}

		if (vkCreateInstance(&instanceCreateInfo, nullptr, &m_instance) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the VkInstance");
	}

	void VulkanContext::create_debug_messenger()
	{
		if (!EnableValidationLayers) return;

		auto messengerCreateInfo = VulkanContext::GetDefaultDebuggerMessengerCreateInfo();
		auto loadedFunction = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
		if (loadedFunction == nullptr ||
			loadedFunction(m_instance, &messengerCreateInfo, nullptr, &m_debug_messenger) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create the Vulkan Debug Messenger!");
		}
	}

	void VulkanContext::create_surface()
	{
		/*
		*  The window surface needs to be created right after the instance creation,
		*  because it can actually influence the physical device selection.
		*/
		if (glfwCreateWindowSurface(
			m_instance,
			m_window,
			m_memory_allocator,
			&m_surface) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Window Surface!");
	}


	void VulkanContext::create_physical_device()
	{
		uint32_t phyDevCnt = 0;
		vkEnumeratePhysicalDevices(m_instance, &phyDevCnt, nullptr);
		if (!phyDevCnt) throw std::runtime_error("Failed to enumerate GPUs with Vulkan support!");

		std::vector<VkPhysicalDevice> physicalDevices(phyDevCnt);
		vkEnumeratePhysicalDevices(m_instance, &phyDevCnt, physicalDevices.data());

		bool is_physical_device_suitable = false;
		for (const auto& physicalDevice : physicalDevices)
		{
			m_physical_device = physicalDevice;
			if (check_physical_device_features_support() &&
				check_physical_device_queue_families_support() &&
				check_physical_device_extensions_support() &&
				check_physical_device_surface_support())
				is_physical_device_suitable = true;
			if (is_physical_device_suitable) break;
		}
		if (!is_physical_device_suitable)
			throw std::runtime_error("Failed to find a suitable GPU!");

		vkGetPhysicalDeviceFeatures(m_physical_device, &m_physical_device_features);
		vkGetPhysicalDeviceMemoryProperties(m_physical_device, &m_physical_device_memory_properties);
	}

	void VulkanContext::create_logical_device()
	{
		std::vector<VkDeviceQueueCreateInfo> deviceQueueCreateInfos;
		std::unordered_map<uint32_t, size_t> visitedIndicesAndSize;
		for (const auto& [required_family, priorities] : m_required_queue_families_with_priorities)
		{
			auto familyIndex = required_family->value();
			//[Vulkan Tutorial - P77]: If the queue families are the same, then we only need to pass its index once.
			if (visitedIndicesAndSize.find(familyIndex) != visitedIndicesAndSize.end())
			{
				if (priorities.size() != visitedIndicesAndSize[familyIndex])
					throw std::runtime_error("Failed to initialize logical device - You are creating more than 1 queues at same QueueFamily but have different size!");
				continue;
			}
			else visitedIndicesAndSize[familyIndex] = priorities.size();

			deviceQueueCreateInfos.emplace_back(VkDeviceQueueCreateInfo
				{
					.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					.queueFamilyIndex = familyIndex,
					.queueCount = static_cast<uint32_t>(priorities.size()),
					.pQueuePriorities = priorities.data()
				});
		}

		VkDeviceCreateInfo deviceCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
			.queueCreateInfoCount = static_cast<uint32_t>(deviceQueueCreateInfos.size()),
			.pQueueCreateInfos = deviceQueueCreateInfos.data(),
			.enabledExtensionCount = static_cast<uint32_t>(m_device_extensions.size()),
			.ppEnabledExtensionNames = m_device_extensions.data(),
			.pEnabledFeatures = &m_physical_device_features
		};

		if (vkCreateDevice(m_physical_device, &deviceCreateInfo, m_memory_allocator, &m_device) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the logical device!");
	}

	void VulkanContext::create_swap_chain()
	{
		if (!check_swap_chain_image_format_support())
			throw std::runtime_error(std::format("Failed to create the Vulkan Swap Chain - Image format is not supported!"));
		if (!check_swap_chain_present_mode_support())
			throw std::runtime_error(std::format("Failed to create the Vulkan Swap Chain - Present mode is not supported!"));


		VkSurfaceCapabilitiesKHR current_surface_capabilities{};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &current_surface_capabilities);

		// Begin(Choose Swap Extent (resolution of images in swap chain))
		{
			constexpr const uint32_t SPECIAL_VALUE_OF_WINDOW_MANAGER = std::numeric_limits<uint32_t>::max(); // More details in textbook P85
			if (current_surface_capabilities.currentExtent.height == SPECIAL_VALUE_OF_WINDOW_MANAGER)
			{
				int width, height;
				glfwGetFramebufferSize(m_window, &width, &height);
				while (width == 0 || height == 0)
				{
					glfwGetFramebufferSize(m_window, &width, &height);
					glfwWaitEvents();
				}

				m_swapchain_current_extent = VkExtent2D
				{
					.width = std::clamp(static_cast<uint32_t>(width),
														current_surface_capabilities.minImageExtent.width,
														current_surface_capabilities.maxImageExtent.width),
					.height = std::clamp(static_cast<uint32_t>(height),
														current_surface_capabilities.minImageExtent.height,
														current_surface_capabilities.maxImageExtent.height)
				};
			}
			else m_swapchain_current_extent = current_surface_capabilities.currentExtent;
		} // End(Choose Swap Extent (resolution of images in swap chain)

		// Decide how many images we would like to have in the swap chain
		m_swapchain_image_count = current_surface_capabilities.minImageCount + 1;
		if (current_surface_capabilities.maxImageCount != 0) // 0 means no limits
			m_swapchain_image_count = std::clamp(m_swapchain_image_count,
																					  m_swapchain_image_count,
																					  current_surface_capabilities.maxImageCount);

		bool is_exclusive_device = (m_device_queue_graphics == m_device_queue_present);
		uint32_t queue_family_indices[] { m_device_queue_graphics.value(), m_device_queue_present.value() };
		VkSwapchainCreateInfoKHR swapChainCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
			.surface = m_surface,
			.minImageCount = m_swapchain_image_count, // Note that, this is just a minimum number of images in the swap chain, the implementation could make it more.
			.imageFormat = m_swapchain_image_format,
			.imageColorSpace = m_swapchain_color_space,
			.imageExtent = m_swapchain_current_extent,
			.imageArrayLayers = 1,
			.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			.imageSharingMode = is_exclusive_device ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
			.queueFamilyIndexCount = is_exclusive_device ? 0U : 2U,
			.pQueueFamilyIndices = is_exclusive_device ? nullptr : queue_family_indices,
			.preTransform = current_surface_capabilities.currentTransform, // Do not want any pretransformation
			.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			.presentMode = m_swapchain_present_mode,
			.clipped = VK_TRUE, // Means that we do not care about the color of pixels that are obscured for the best performance. (P89)
			.oldSwapchain = VK_NULL_HANDLE // Modify this field later (resize)
		};
		if (vkCreateSwapchainKHR(m_device, &swapChainCreateInfo, m_memory_allocator, &m_swapchain) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Swap Chain!");

		// Retrieve the swap chain images
		vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_swapchain_image_count, nullptr);
		m_swapchain_images.resize(m_swapchain_image_count);
		vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_swapchain_image_count, m_swapchain_images.data());

		// Create Image Views
		m_swapchain_imageviews.resize(m_swapchain_image_count);
		for (size_t idx = 0; idx < m_swapchain_image_count; ++idx)
		{
			VkImageViewCreateInfo imageViewCreateInfo
			{
				.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				.image = m_swapchain_images[idx],
				.viewType = VK_IMAGE_VIEW_TYPE_2D,
				.format = m_swapchain_image_format,
				.components{.r = VK_COMPONENT_SWIZZLE_IDENTITY,
										.g = VK_COMPONENT_SWIZZLE_IDENTITY,
										.b = VK_COMPONENT_SWIZZLE_IDENTITY,
										.a = VK_COMPONENT_SWIZZLE_IDENTITY},
				.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
													.baseMipLevel = 0,
													.levelCount = 1,
													.baseArrayLayer = 0,
													.layerCount = 1}
			};
			if (vkCreateImageView(m_device, &imageViewCreateInfo, m_memory_allocator, &m_swapchain_imageviews[idx]) != VK_SUCCESS)
				throw std::runtime_error("Failed to create all image views");
		}
	}

	void VulkanContext::destroy_swap_chain()
	{
		for (auto imageview : m_swapchain_imageviews)
			vkDestroyImageView(m_device, imageview, m_memory_allocator);
		vkDestroySwapchainKHR(m_device, m_swapchain, m_memory_allocator);
	}

	void VulkanContext::destroy_logical_device()
	{
		vkDeviceWaitIdle(m_device);
		vkDestroyDevice(m_device, m_memory_allocator);
	}

	void VulkanContext::destroy_debug_messenger()
	{
		log::warn("\n[Vulkan Messenger Statistics]");
		log::info("VERBOSE: {}", s_debug_message_statistics[VERBOSE]);
		log::info("INFO: {}", s_debug_message_statistics[INFO]);
		log::info("WARN: {}", s_debug_message_statistics[WARN]);
		log::info("ERROR: {}\n", s_debug_message_statistics[ERROR]);
		auto loadFunction = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
		if (loadFunction != nullptr) loadFunction(m_instance, m_debug_messenger, m_memory_allocator);
		else throw std::runtime_error("Failed to load function: vkDestroyDebugUtilsMessengerEXT");
	}

	void VulkanContext::destroy_surface()
	{
		vkDestroySurfaceKHR(m_instance, m_surface, m_memory_allocator);
	}

	void VulkanContext::destroy_vulkan_instance()
	{
		vkDestroyInstance(m_instance, m_memory_allocator);
	}

	bool VulkanContext::check_physical_device_features_support()
	{
		VkPhysicalDeviceProperties phyDevProperties;
		VkPhysicalDeviceFeatures phyDevFeatures;
		vkGetPhysicalDeviceProperties(m_physical_device, &phyDevProperties);
		vkGetPhysicalDeviceFeatures(m_physical_device, &phyDevFeatures);

		//if (!phyDevFeatures.samplerAnisotropy) return false;

		/*if(phyDevProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ||
			phyDevFeatures.geometryShader != VK_TRUE)
			return false;*/
		return true;
	}

	bool VulkanContext::check_physical_device_queue_families_support()
	{
		// Find Queue Families
		m_device_queue_graphics.reset();
		m_device_queue_present.reset();
		m_device_queue_compute.reset();
		m_device_queue_transfer.reset();
		m_device_queue_sparsebinding.reset();

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queueFamilyCount, queueFamilies.data());

		int idx = 0;
		for (const auto& queueFamily : queueFamilies)
		{
			bool graphicsSupport = false;
			bool computeSupport = false;
			bool transferSupport = false;
			bool sparseBindingSupport = false;
			VkBool32 presentSupport = VK_FALSE;

			if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) graphicsSupport = true;
			if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) computeSupport = true;
			if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) transferSupport = true;
			if (queueFamily.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) sparseBindingSupport = true;
			vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, idx, m_surface, &presentSupport);

			// Any queue family with VK_QUEUE_GRAPHICS_BIT or VK_QUEUE_COMPUTE_BIT capabilities already implicitly support VK_QUEUE_TRANSFER_BIT operations
			if (graphicsSupport && !m_device_queue_graphics.has_value())
				m_device_queue_graphics = idx;
			if (computeSupport && !m_device_queue_compute.has_value())
				m_device_queue_compute = idx;
			if (transferSupport && !m_device_queue_transfer.has_value())
				m_device_queue_transfer = idx;
			if (sparseBindingSupport && !m_device_queue_sparsebinding.has_value())
				m_device_queue_sparsebinding = idx;
			if (presentSupport == VK_TRUE && !m_device_queue_present.has_value())
				m_device_queue_present = idx;

			// The Graphics Queue Index is better to be same as the Present Queue Index.
			if (graphicsSupport && (presentSupport == VK_TRUE) &&
				(m_device_queue_graphics != m_device_queue_present))
			{
				m_device_queue_graphics = idx;
				m_device_queue_present = idx;
			}
			++idx;
		}// End Loop - find family

		// Check Required Queue Family
		for (const auto&[queue_family, priorities] : m_required_queue_families_with_priorities)
		{
			if (!queue_family->has_value()) return false;
		}
		return true;
	}

	bool VulkanContext::check_physical_device_extensions_support()
	{
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(m_physical_device, nullptr, &extensionCount, availableExtensions.data());
		std::unordered_set<std::string> requiredExtensions(m_device_extensions.begin(), m_device_extensions.end());
		for (const auto& extension : availableExtensions)
			requiredExtensions.erase(extension.extensionName);

		return requiredExtensions.empty();
	}


	bool VulkanContext::check_physical_device_surface_support()
	{
		// 1. Surface Formats
		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &formatCount, nullptr);
		if (formatCount)
		{
			m_surface_formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &formatCount, m_surface_formats.data());
		}

		// 2. Present Modes
		uint32_t presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, m_surface, &presentModeCount, nullptr);
		if (presentModeCount)
		{
			m_surface_present_modes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, m_surface, &presentModeCount, m_surface_present_modes.data());
		}

		return !m_surface_formats.empty() && !m_surface_present_modes.empty();
	}

	bool VulkanContext::check_swap_chain_image_format_support()
	{
		for (const auto& surface_format : m_surface_formats)
		{
			if (surface_format.format == m_swapchain_image_format &&
				surface_format.colorSpace == m_swapchain_color_space)
				return true;
		}
		return false;
	}

	bool VulkanContext::check_swap_chain_present_mode_support()
	{
		for (const auto& surface_present_mode : m_surface_present_modes)
		{
			if (surface_present_mode == m_swapchain_present_mode)
				return true;
		}
		return false;
	}

}} // namespace Albedo::RHI