#include "vulkan_context.h"
#include "vulkan_memory.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <cstring>

namespace Albedo {
namespace RHI
{

	VMA::VulkanMemoryAllocator(VulkanContext* vulkan_context) :
		m_context{ vulkan_context }
	{
		VmaAllocatorCreateInfo vmaAllocatorCreateInfo
		{ 
			.flags = 0x0, //VmaAllocatorCreateFlagBits
			.physicalDevice = m_context->m_physical_device,
			.device = m_context->m_device,
			.preferredLargeHeapBlockSize = 0, // 0 means Default (256MiB)
			.pAllocationCallbacks = m_context->m_memory_allocation_callback,
			.pDeviceMemoryCallbacks = nullptr,
			.instance = m_context->m_instance,
			.vulkanApiVersion  = VK_API_VERSION_1_3 // Default Version 1.0
		};
		
		if (vmaCreateAllocator(&vmaAllocatorCreateInfo, &m_allocator) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the VMA (Vulkan Memory Allocator)!");
	}

	VMA::~VulkanMemoryAllocator()
	{
		vmaDestroyAllocator(m_allocator);
	}

	std::shared_ptr<VMA::Buffer> VMA::AllocateBuffer(size_t size, VkBufferUsageFlags usage, 
			bool is_exclusive /*= true*/, bool is_writable/* = false*/, bool is_readable/* = false*/, bool is_persistent/* = false*/)
		// If both is_writable and is_readable are false, the memory property is Device Local
	{
		VkBufferCreateInfo bufferCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage,
			.sharingMode = is_exclusive? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT
		};

		VmaAllocationCreateFlags allocation_flags = 0;
		if (is_writable)		allocation_flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		if (is_readable)		allocation_flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		if (is_persistent)	allocation_flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
		VmaAllocationCreateInfo allocationInfo
		{
			.flags = allocation_flags,
			.usage = VMA_MEMORY_USAGE_AUTO,
			//.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		}; 

		auto buffer = std::make_shared<VMA::Buffer>(shared_from_this());

		if (vmaCreateBuffer(
			m_allocator,
			&bufferCreateInfo,
			&allocationInfo,
			&buffer->m_buffer,
			&buffer->m_allocation,
			nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Buffer!");
		
		return buffer;
	}

	VMA::Buffer::~Buffer() 
	{ 
		vmaDestroyBuffer(m_parent->m_allocator, m_buffer, m_allocation); 
	}



	void VMA::Buffer::Write(void* data)
	{
		assert(m_allocation->IsMappingAllowed() && "This buffer is not mapping-allowed!");

		void* mappedArea;
		if (m_allocation->IsPersistentMap())
		{
			mappedArea = m_allocation->GetMappedData();
			memcpy(mappedArea, data, m_allocation->GetSize());
		}
		else
		{
			vmaMapMemory(m_parent->m_allocator, m_allocation, &mappedArea);
			memcpy(mappedArea, data, m_allocation->GetSize());
			vmaUnmapMemory(m_parent->m_allocator, m_allocation);
		}
	}

	void* VMA::Buffer::Access()
	{
		assert(m_allocation->IsPersistentMap() && "This buffer is not persistently mapped!");
		return m_allocation->GetMappedData();
	}

	VkDeviceSize VMA::Buffer::Size()
	{
		return m_allocation->GetSize();
	}

	std::shared_ptr<VMA::Image> VMA::AllocateImage(
		uint32_t width, uint32_t height, uint32_t channel, 
		VkFormat format,VkImageUsageFlags usage,
		VkImageTiling tiling_mode/* = VK_IMAGE_TILING_OPTIMAL*/,
		uint32_t miplevel/* = 1*/)
	{
		VkImageCreateInfo imageCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			.flags = 0x0,
			.imageType = VK_IMAGE_TYPE_2D,
			.format = format,
			.extent{.width = width, .height = height, .depth = 1}, // 2D Texture
			.mipLevels = miplevel,
			.arrayLayers = 1,
			.samples = VK_SAMPLE_COUNT_1_BIT,
			.tiling = tiling_mode, // P206
			.usage = usage,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE, // The image will only be used by one queue family: the one that supports graphics (and therefore also) transfer operations.
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED // P207 Top
		};

		VmaAllocationCreateFlags allocation_flags = 0;
		VmaAllocationCreateInfo allocationInfo
		{
			.flags = allocation_flags,
			.usage = VMA_MEMORY_USAGE_AUTO,
			//.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		};

		auto image = std::make_shared<VMA::Image>(shared_from_this());

		if (vmaCreateImage(
			m_allocator,
			&imageCreateInfo,
			&allocationInfo,
			&image->m_image,
			&image->m_allocation,
			nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Image!");

		image->m_image_format = format;
		image->m_image_width = width;
		image->m_image_height = height;
		image->m_image_channel = channel;
		image->m_mipmap_level = miplevel;

		VkImageViewCreateInfo imageViewCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = image->m_image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.components = VK_COMPONENT_SWIZZLE_IDENTITY,
			.subresourceRange
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};
		if (vkCreateImageView(
			m_context->m_device,
			&imageViewCreateInfo,
			m_context->m_memory_allocation_callback,
			&image->m_image_view
			) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Image View!");

		return image;
	}
	
	VMA::Image::~Image()
	{
		vmaDestroyImage(m_parent->m_allocator, m_image, m_allocation);
	}

	void VMA::Image::Write(void* data)
	{
		// Staging Buffer
		auto staging_buffer = m_parent->AllocateBuffer( // Not equal to Image Memory Size
			static_cast<size_t>(m_image_width) * m_image_height * m_image_channel, 
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, true);
		staging_buffer->Write(data);

		TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		// Copy buffer to image requires the image to be in the right layout first
		VkBufferImageCopy copyinfo
		{
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = m_mipmap_level,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.imageOffset = {0,0,0},
			.imageExtent = {m_image_width, m_image_height, 1}
		};

		auto commandBuffer = m_parent->m_context->GetOneTimeCommandBuffer();
		commandBuffer->Begin();
		vkCmdCopyBufferToImage(*commandBuffer, *staging_buffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyinfo);
		commandBuffer->End();
		commandBuffer->Submit(true); // Must wait for transfer operation

		TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VMA::Image::TransitionImageLayout(VkImageLayout target_layout)
	{
		VkImageMemoryBarrier imageMemoryBarrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			//.srcAccessMask = 0x0, (Specify later)
			//.dstAccessMask = 0x0, (Specify later)
			.oldLayout = m_image_layout,
			.newLayout = target_layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image  = m_image,
			.subresourceRange 
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		// Barriers are primarily used for synchronization purposes, so you must specify which types of operations that involve 
		//the resource must happen before the barrier, 
		//and which operations that involve the resource must wait on the barrier.
		VkPipelineStageFlags sourceStage;
		VkPipelineStageFlags destinationStage;

		if (m_image_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
			target_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) 
		{
			imageMemoryBarrier.srcAccessMask = 0;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (m_image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && 
			target_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) 
		{
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else throw std::invalid_argument("Failed to transition the Vulkan Image Layout - Unsupported layout transition!");

		auto commandBuffer = m_parent->m_context->GetOneTimeCommandBuffer();
		commandBuffer->Begin();
		vkCmdPipelineBarrier( // P213
			*commandBuffer,
			sourceStage,
			destinationStage,
			0x0,
			0, nullptr,
			0, nullptr,
			1, &imageMemoryBarrier);
		commandBuffer->End();
		commandBuffer->Submit(true); // Must wait for transfer operation

		m_image_layout = target_layout; // Update Layout
	}

	VkDeviceSize VMA::Image::Size()
	{
		return m_allocation->GetSize();
	}

}} // namespace Albedo::RHI