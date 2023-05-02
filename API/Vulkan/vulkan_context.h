#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <AlbedoLog.hpp>

#include "vulkan_wrapper.h"
#include "vulkan_memory.h"

namespace Albedo {
namespace RHI
{
#ifdef NDEBUG
	constexpr const bool EnableValidationLayers = false;
#else
	constexpr const bool EnableValidationLayers = true;
#endif

	// Factory (You should create most of vulkan objects in via Vulkan Context: CreateXX functions)
	class VulkanContext : public std::enable_shared_from_this<VulkanContext>
	{
	public:
		VkInstance								m_instance									= VK_NULL_HANDLE;
		GLFWwindow*							m_window										= VK_NULL_HANDLE;

		VkSurfaceKHR							m_surface										= VK_NULL_HANDLE;
		std::vector<VkSurfaceFormatKHR>	m_surface_formats;	
		std::vector<VkPresentModeKHR>		m_surface_present_modes;

		VkPhysicalDevice						m_physical_device						= VK_NULL_HANDLE;
		VkPhysicalDeviceFeatures		m_physical_device_features;
		VkPhysicalDeviceProperties	m_physical_device_properties;
		VkPhysicalDeviceMemoryProperties m_physical_device_memory_properties;
		std::optional<VkPhysicalDeviceFeatures2> m_physical_device_features2;	// Future: Enable Bindless

		VkDevice									m_device										= VK_NULL_HANDLE;
		QueueFamilyIndex					m_device_queue_family_graphics;
		QueueFamilyIndex					m_device_queue_family_present;
		QueueFamilyIndex					m_device_queue_family_compute;
		QueueFamilyIndex					m_device_queue_family_transfer;
		QueueFamilyIndex					m_device_queue_family_sparsebinding;

		std::shared_ptr<VMA>				m_memory_allocator;
		VkAllocationCallbacks*			m_memory_allocation_callback = VK_NULL_HANDLE;

		VkSwapchainKHR					m_swapchain								= VK_NULL_HANDLE;
		class											swapchain_error							: public std::exception {}; // Recreation Signal
		uint32_t										m_swapchain_image_count;		// clamp(minImageCount + 1, maxImageCount)
		VkFormat									m_swapchain_image_format		= VK_FORMAT_B8G8R8A8_SRGB;
		VkColorSpaceKHR					m_swapchain_color_space		= VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		VkPresentModeKHR				m_swapchain_present_mode	= VK_PRESENT_MODE_MAILBOX_KHR;
		VkFormat									m_swapchain_depth_stencil_format	= VK_FORMAT_D32_SFLOAT;
		VkImageTiling							m_swapchain_depth_stencil_tiling		= VK_IMAGE_TILING_OPTIMAL;
		uint32_t										m_swapchain_depth_channel;	// Deduced in check_swap_chain_depth_format_support()
		uint32_t										m_swapchain_stencil_channel;	// Ditto, and you can use this to judge whether has a stencil component
		VkExtent2D								m_swapchain_current_extent;
		std::vector<VkImage>				m_swapchain_images;
		std::vector<VkImageView>		m_swapchain_imageviews;
		uint32_t										m_swapchain_current_image_index{ 0 };
		std::shared_ptr<VMA::Image>m_swapchain_depth_stencil_image;

		VkDebugUtilsMessengerEXT	m_debug_messenger					= VK_NULL_HANDLE;

	private:
		std::vector<std::pair<QueueFamilyIndex*, std::vector<float>>>
			m_required_queue_families_with_priorities{
				{&m_device_queue_family_graphics,		{1.0f}},
				{&m_device_queue_family_transfer,			{1.0f}},
				{&m_device_queue_family_present,			{1.0f}}};
		
#ifdef NDEBUG
		std::vector<const char*>			m_validation_layers{"VK_LAYER_RENDERDOC_Capture"};

		std::vector<const char*>			m_device_extensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME,
																									 /*VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME*/ };
#else
		std::vector<const char*>			m_validation_layers{ "VK_LAYER_KHRONOS_validation"
																								/* ,"VK_LAYER_RENDERDOC_Capture" */};

		std::vector<const char*>			m_device_extensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME
																									/* ,VK_EXT_DEBUG_MARKER_EXTENSION_NAME*/
																									/*,VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME*/ };
#endif	

	public:
		void WaitDeviceIdle() { vkDeviceWaitIdle(m_device); }	

		VkQueue GetQueue(QueueFamilyIndex& queue_family_index, uint32_t queue_index = 0) { VkQueue res; vkGetDeviceQueue(m_device, queue_family_index.value(), queue_index, &res); return res; }

		// Swapchain Functions (throw swapchain_error means recreation)
		void Screenshot(std::shared_ptr<VMA::Image> screenshot, std::vector<VkSemaphore> wait_semaphores = {}, std::vector<VkSemaphore> signal_semaphores = {}, VkFence fence = nullptr);
		void NextSwapChainImageIndex(VkSemaphore semaphore, VkFence fence,
			uint64_t timeout = std::numeric_limits<uint64_t>::max()) throw (swapchain_error);
		void PresentSwapChain(const std::vector<VkSemaphore>& wait_semaphore) throw (swapchain_error);
		void RecreateSwapChain();

	public:
		static std::shared_ptr<VulkanContext>	 Create(GLFWwindow* window); // Create Vulkan Context

		// Common Products (Command Buffers and Descriptor Sets are created from Global Pools with Lazy Creation)
		std::weak_ptr<VulkanContext>				CreateVulkanContextView() { return shared_from_this(); }

		std::shared_ptr<CommandBuffer>		CreateOneTimeCommandBuffer(QueueFamilyIndex& submit_queue_family_index, bool primary = true, std::thread::id thread_id = std::this_thread::get_id());
		std::shared_ptr<CommandBuffer>		CreateResetableCommandBuffer(QueueFamilyIndex& submit_queue_family_index, bool primary = true, std::thread::id thread_id = std::this_thread::get_id());

		std::shared_ptr<DescriptorSetLayout>	CreateDescripotrSetLayout(std::vector<VkDescriptorSetLayoutBinding> descriptor_bindings);
		std::shared_ptr<DescriptorSet>				CreateDescriptorSet(std::shared_ptr<DescriptorSetLayout> descriptor_set_layout, std::thread::id thread_id = std::this_thread::get_id());

		std::shared_ptr<Sampler>						CreateSampler(VkSamplerAddressMode address_mode,
																										VkBorderColor border_color = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
																										VkCompareOp compare_mode = VK_COMPARE_OP_NEVER,
																										bool anisotropy_enable = true);

		std::unique_ptr<Semaphore>					CreateSemaphore(VkSemaphoreCreateFlags flags);
		std::unique_ptr<Fence>							CreateFence(VkFenceCreateFlags flags);

		// Advanced Products (Create Local Pools)
		std::shared_ptr<CommandPool>			CreateCommandPool(QueueFamilyIndex& submit_queue_family_index,
																													VkCommandPoolCreateFlags command_pool_flags);
		std::shared_ptr<DescriptorPool>			CreateDescriptorPool(std::vector<VkDescriptorPoolSize> pool_size, uint32_t limit_max_sets);

	public:
		// Global Objects
		std::shared_ptr<CommandPool>			GetGlobalOneTimeCommandPool(QueueFamilyIndex& queue_family_index, std::thread::id thread_id = std::this_thread::get_id());
		std::shared_ptr<CommandPool>			GetGlobalResetableCommandPool(QueueFamilyIndex& queue_family_index, std::thread::id thread_id = std::this_thread::get_id());
		
		std::shared_ptr<DescriptorPool>			GetGlobalDescriptorPool(std::thread::id thread_id = std::this_thread::get_id());

	private:
		// Global Resource
		using GlobalCommandPool = std::unordered_map<QueueFamilyIndex, std::shared_ptr<CommandPool>>;
		std::unordered_map<std::thread::id, GlobalCommandPool> m_global_onetime_command_pools;
		std::unordered_map<std::thread::id, GlobalCommandPool> m_global_resetable_command_pools;

		using GlobalDescriptorPool = std::shared_ptr<DescriptorPool>;
		std::unordered_map<std::thread::id, GlobalDescriptorPool> m_global_descriptor_pool;

	private:
		VulkanContext() = delete;
		VulkanContext(GLFWwindow* window); // VulkanContext::Create(GLFWwindow* window)
		~VulkanContext();

	protected:
		// Creation
		static std::mutex	VULKAN_CONTEXT_CREATION_MUTEX;
		static bool				NO_VULKAN_CONTEXTS;	
		// Initialization
		void enable_validation_layers();
		void create_vulkan_instance();
		void create_debug_messenger();
		void create_surface();
		void create_physical_device();
		void create_logical_device();
		void create_memory_allocator();
		void create_swap_chain();
		// Destroy (reverse order of initialization) 
		// Physical Devices will be implicitly destroyed when the VkInstance is destroyed.
		void destroy_swap_chain();
		void destroy_memory_allocator();
		void destroy_logical_device();
		void destroy_surface();
		void destroy_debug_messenger();
		void destroy_vulkan_instance();

	protected:
		// Physical Device Support
		bool check_physical_device_features_support();
		bool check_physical_device_queue_families_support();
		bool check_physical_device_extensions_support();
		bool check_physical_device_surface_support();

		// Swap Chain Support
		bool check_swap_chain_image_format_support();
		bool check_swap_chain_depth_format_support();
		bool check_swap_chain_present_mode_support();

	protected:
		// Debug Messenger
		enum vulkan_message_type { VERBOSE, INFO, WARN, ERROR, MAX_MESSAGE_TYPE };
		static std::vector<uint32_t> s_debug_message_statistics;

		inline static auto GetDefaultDebuggerMessengerCreateInfo()
		{
			return VkDebugUtilsMessengerCreateInfoEXT
			{
				.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
				.messageSeverity =
				//VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
				.messageType =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
				.pfnUserCallback = VulkanContext::callback_debug_messenger,
				.pUserData = nullptr
			};
		}

		inline static VKAPI_ATTR VkBool32 VKAPI_CALL 
		callback_debug_messenger
			(
				VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
				VkDebugUtilsMessageTypeFlagsEXT messageType,
				const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
				void* pUserData
			)
		{
			if (VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT & messageSeverity)
			{
				VulkanContext::s_debug_message_statistics[VERBOSE]++;
			}
			else if (VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT & messageSeverity)
			{
				VulkanContext::s_debug_message_statistics[INFO]++;
				//log::info("\n[Vulkan]: {}", pCallbackData->pMessage);
			}
			else if (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT & messageSeverity)
			{
				VulkanContext::s_debug_message_statistics[WARN]++;
				log::warn("\n[Vulkan]: {}", pCallbackData->pMessage);
			}
			else if (VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT & messageSeverity)
			{
				VulkanContext::s_debug_message_statistics[ERROR]++;
				log::error("\n[Vulkan]: {}", pCallbackData->pMessage);
			}
			else log::critical("\n[Vulkan]: Unknow Message Severity {}", messageSeverity);

			return VK_FALSE; // Always return false
		}
	};

}} // namespace Albedo::RHI