#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

// Predeclaration
typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace Albedo {
namespace RHI
{
	class VulkanContext;
	class Sampler;

	class VulkanMemoryAllocator : public std::enable_shared_from_this<VulkanMemoryAllocator>
	{
		friend class VulkanContext;
		friend class Buffer;
		friend class Image;
	public:
		// Buffer
		class Buffer
		{
			friend class VulkanMemoryAllocator;
		public:
			void		Write(void* data);	// The buffer must be mapping-allowed and writable
			void*	Access();				// If the buffer is persistently mapped, you can access its memory directly
			VkDeviceSize Size();

		public:
			Buffer() = delete;
			Buffer(std::shared_ptr<VulkanMemoryAllocator> parent) : m_parent{ std::move(parent)} {};
			~Buffer();
			operator VkBuffer() { return m_buffer; }

		private:
			std::shared_ptr<VulkanMemoryAllocator> m_parent;
			VmaAllocation m_allocation = VK_NULL_HANDLE;
			VkBuffer m_buffer = VK_NULL_HANDLE;
		};

		// Image
		class Image
		{
			friend class VulkanMemoryAllocator;
		public:
			void Write(void* data);
			void BindSampler(std::shared_ptr<RHI::Sampler> sampler);

			void TransitionImageLayout(VkImageLayout target_layout);

			VkImageLayout& GetImageLayout() { return m_image_layout; }
			VkImageView& GetImageView() { return m_image_view; }
			VkSampler GetImageSampler();
			bool HasStencilComponent();
			VkDeviceSize Size();

		public:
			Image() = delete;
			Image(std::shared_ptr<VulkanMemoryAllocator> parent) : m_parent{ std::move(parent) } {};
			~Image();
			operator VkImage() { return m_image; }
		
		private:
			std::shared_ptr<VulkanMemoryAllocator> m_parent;
			VmaAllocation m_allocation = VK_NULL_HANDLE;
			VkImage m_image = VK_NULL_HANDLE;
			VkImageView m_image_view = VK_NULL_HANDLE;
			std::shared_ptr<RHI::Sampler> m_image_sampler;

			VkImageLayout m_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkFormat m_image_format;
			uint32_t m_image_width;
			uint32_t m_image_height;
			uint32_t m_image_channel;
			uint32_t m_mipmap_level;
		};

	public:
		std::shared_ptr<Buffer> AllocateBuffer(size_t size, VkBufferUsageFlags usage, 
			bool is_exclusive = true, bool is_writable = false, bool is_readable = false, bool is_persistent = false);
		std::shared_ptr<Image> AllocateImage(	VkImageAspectFlags aspect,
																				VkImageUsageFlags usage,
																				uint32_t width, uint32_t height, 
																				uint32_t channel, VkFormat format, 
																				VkImageTiling tiling_mode = VK_IMAGE_TILING_OPTIMAL,
																				uint32_t miplevel = 1);

		~VulkanMemoryAllocator();

	private: // Created by VulkanContext
		static auto	Create(std::shared_ptr<VulkanContext> vulkan_context)
		{
			struct VMACreator :public VulkanMemoryAllocator 
			{ VMACreator(std::shared_ptr<VulkanContext> vulkan_context) : VulkanMemoryAllocator{ vulkan_context } {}};
			return std::make_shared<VMACreator>(vulkan_context);
		}
		VulkanMemoryAllocator(std::shared_ptr<VulkanContext> vulkan_context);

	private:
		std::shared_ptr<VulkanContext> m_context;
		VmaAllocator m_allocator = VK_NULL_HANDLE;
	};
	using VMA = VulkanMemoryAllocator;

}} // namespace Albedo::RHI