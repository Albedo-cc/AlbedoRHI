#pragma once

#include <vulkan/vulkan.h>

#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <stdexcept>
#include <algorithm>
#include <optional>
#include <memory>
#include <numeric>
#include <cassert>
#include <format>
#include <thread>
#include <vector>
#include <string>
#include <array>

// Predeclaration
typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace Albedo {
namespace RHI
{
	class VulkanContext;
	class CommandBuffer;
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
			void		Copy(std::shared_ptr<Buffer> destination, VkDeviceSize size = 0/*ALL*/, VkDeviceSize offset_src = 0, VkDeviceSize offset_dst = 0);
			void		CopyCommand(std::shared_ptr<CommandBuffer> commandBuffer, std::shared_ptr<Buffer> destination, VkDeviceSize size = 0/*ALL*/, VkDeviceSize offset_src = 0, VkDeviceSize offset_dst = 0);
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
			void Write(std::shared_ptr<Buffer> data); // Write from Staging Buffer
			void WriteCommand(std::shared_ptr<RHI::CommandBuffer> commandBuffer, std::shared_ptr<Buffer> data); // Write from Staging Buffer
			void WriteAndTransition(std::shared_ptr<Buffer> data, VkImageLayout final_layout);
			void WriteAndTransitionCommand(std::shared_ptr<RHI::CommandBuffer> commandBuffer, std::shared_ptr<Buffer> data, VkImageLayout final_layout);
			void BindSampler(std::shared_ptr<RHI::Sampler> sampler);

			void TransitionLayout(VkImageLayout target_layout);
			void TransitionLayoutCommand(std::shared_ptr<RHI::CommandBuffer> commandBuffer, VkImageLayout target_layout);

			VkImageLayout GetImageLayout() { return m_image_layout; }
			VkImageView GetImageView() { return m_image_view; }
			VkSampler GetImageSampler();
			bool HasStencilComponent();
			VkDeviceSize Size();
			uint32_t Width() const { return m_image_width; }
			uint32_t Height() const { return m_image_height; }
			uint32_t Channel() const { return m_image_channel; }

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

		public:
			VkImageMemoryBarrier deduce_transition_layout_barrier(VkImageLayout target_layout, VkPipelineStageFlags& stage_src, VkPipelineStageFlags& stage_dst);
		};

	public:
		std::shared_ptr<Buffer> AllocateBuffer(size_t size, VkBufferUsageFlags usage, 
			bool is_exclusive = true, bool is_writable = false, bool is_readable = false, bool is_persistent = false);
		std::shared_ptr<Image> AllocateImage(	VkImageAspectFlags aspect,
																				VkImageUsageFlags usage,
																				uint32_t width, uint32_t height, 
																				uint32_t channel, VkFormat format,
																				VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED,
																				VkImageTiling tiling_mode = VK_IMAGE_TILING_OPTIMAL,
																				uint32_t miplevel = 1);
		std::shared_ptr<Buffer> AllocateStagingBuffer(VkDeviceSize buffer_size);

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