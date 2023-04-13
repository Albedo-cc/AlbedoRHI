#include "vulkan_context.h"
#include "vulkan_memory.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <cstring>

namespace Albedo {
namespace RHI
{

	VMA::VulkanMemoryAllocator(std::shared_ptr<VulkanContext> vulkan_context) :
		m_context{ std::move(vulkan_context) }
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

	void VMA::Buffer::Copy(std::shared_ptr<Buffer> destination, 
		VkDeviceSize size /* = ALL*/, VkDeviceSize offset_src/* = 0*/, VkDeviceSize offset_dst/* = 0*/)
	{
		auto commandBuffer = m_parent->m_context->GetOneTimeCommandBuffer();
		commandBuffer->Begin();
		CopyCommand(commandBuffer, destination, size, offset_src, offset_dst);
		commandBuffer->End();
		commandBuffer->Submit(true); // Must wait for transfer operation
	}

	void VMA::Buffer::CopyCommand(
		std::shared_ptr<CommandBuffer> commandBuffer, 
		std::shared_ptr<Buffer> destination,
		VkDeviceSize size /* = ALL*/, VkDeviceSize offset_src/* = 0*/, VkDeviceSize offset_dst/* = 0*/)
	{
		assert(commandBuffer->IsRecording() &&
			"You have to ensure that the command buffer is recording while using XXXCommand funcitons!");
		size = size ? size : Size();
		assert(size <= (destination->Size() - offset_dst) && "You cannot copy data to another small buffer!");
		VkBufferCopy bufferCopy
		{
			.srcOffset = offset_src,
			.dstOffset = offset_dst,
			.size = size ? size : Size()
		};

		vkCmdCopyBuffer(*commandBuffer, m_buffer, *destination, 1, &bufferCopy);
	}

	VkDeviceSize VMA::Buffer::Size()
	{
		return m_allocation->GetSize();
	}

	std::shared_ptr<VMA::Image> VMA::AllocateImage(
		VkImageAspectFlags aspect,
		VkImageUsageFlags usage,
		uint32_t width, uint32_t height,
		uint32_t channel, VkFormat format,
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
			.usage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			.sharingMode = VK_SHARING_MODE_EXCLUSIVE, // The image will only be used by one queue family: the one that supports graphics (and therefore also) transfer operations.
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED // P207 Top
		};

		VmaAllocationCreateInfo allocationInfo
		{
			.flags = 0x0,
			.usage = VMA_MEMORY_USAGE_AUTO,
			.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
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
				.aspectMask = aspect,
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
		vkDestroyImageView(m_parent->m_context->m_device, m_image_view, m_parent->m_context->m_memory_allocation_callback);
	}

	void VMA::Image::Write(std::shared_ptr<RHI::VMA::Buffer> data)
	{
		auto commandBuffer = m_parent->m_context->GetOneTimeCommandBuffer();
		commandBuffer->Begin();
		WriteCommand(commandBuffer, data);
		commandBuffer->End();
		commandBuffer->Submit(true); // Must wait for transfer operation
	}

	void VMA::Image::WriteCommand(std::shared_ptr<RHI::CommandBuffer> commandBuffer, std::shared_ptr<VMA::Buffer> data)
	{
		assert(commandBuffer->IsRecording() &&
			"You have to ensure that the command buffer is recording while using XXXCommand funcitons!");
		assert(data->Size() <= Size() && "It is not recommanded to write the image from a bigger buffer!");
		if (m_image_channel != 4) log::warn("Writing a {} channels image, but automatically treating it as 4 channels", m_image_channel);

		// Staging Buffer
		size_t image_size = static_cast<size_t>(m_image_width) * m_image_height * 4;

		// Copy buffer to image requires the image to be in the right layout first
		VkBufferImageCopy copyRegion
		{
			.bufferOffset = 0,
			.bufferRowLength = 0,
			.bufferImageHeight = 0,
			.imageSubresource
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
			.imageOffset = {0,0,0},
			.imageExtent = {m_image_width, m_image_height, 1}
		};

		TransitionLayoutCommand(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		vkCmdCopyBufferToImage(*commandBuffer, *data, m_image, m_image_layout, 1, &copyRegion);
		TransitionLayoutCommand(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VMA::Image::BindSampler(std::shared_ptr<RHI::Sampler> sampler)
	{
		m_image_sampler = std::move(sampler);
	}

	void VMA::Image::TransitionLayout(VkImageLayout target_layout)
	{
		auto commandBuffer = m_parent->m_context->GetOneTimeCommandBuffer();
		commandBuffer->Begin();
		TransitionLayoutCommand(commandBuffer, target_layout);
		commandBuffer->End();
		commandBuffer->Submit(true); // Must wait for transfer operation

		m_image_layout = target_layout; // Update Layout
	}

	void VMA::Image::TransitionLayoutCommand(std::shared_ptr<RHI::CommandBuffer> commandBuffer, VkImageLayout target_layout)
	{
		assert(commandBuffer->IsRecording() &&
			"You have to ensure that the command buffer is recording while using XXXCommand funcitons!");
		VkPipelineStageFlags sourceStage, destinationStage;
		auto imageMemoryBarrier = deduce_transition_layout_barrier(target_layout, sourceStage, destinationStage);

		vkCmdPipelineBarrier( // P213
			*commandBuffer,
			sourceStage,
			destinationStage,
			0x0,				// Dependency Flags
			0, nullptr,	// Memory Barrier
			0, nullptr,	// Buffer Memory Barrier
			1, &imageMemoryBarrier);

		m_image_layout = target_layout; // Update Layout
	}

	VkSampler VMA::Image::GetImageSampler()
	{ 
		assert(m_image_sampler != nullptr && "You must call BindSampler() first!");
		return *m_image_sampler;
	}

	bool VMA::Image::HasStencilComponent()
	{
		if (VK_FORMAT_S8_UINT <= m_image_format &&
			m_image_format <= VK_FORMAT_D32_SFLOAT_S8_UINT)
			return true;
		return false;
	}

	VkDeviceSize VMA::Image::Size()
	{
		return m_allocation->GetSize();
	}

	VkImageMemoryBarrier VMA::Image::
		deduce_transition_layout_barrier(VkImageLayout target_layout, 
			VkPipelineStageFlags& stage_src, VkPipelineStageFlags& stage_dst)
	{
		// Barriers are primarily used for synchronization purposes, so you must specify which types of operations that involve 
		//the resource must happen before the barrier, 
		//and which operations that involve the resource must wait on the barrier.
		VkImageAspectFlags imageAspect;
		VkAccessFlags srcAccessMask;
		VkAccessFlags dstAccessMask;

		if ( // Undefined Image => Transfer Destination Image
			VK_IMAGE_LAYOUT_UNDEFINED == m_image_layout &&
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL == target_layout)
		{
			imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;

			srcAccessMask = 0;
			dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			stage_src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			stage_dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if ( // Undefined Image => Depth Stencil Image
			VK_IMAGE_LAYOUT_UNDEFINED == m_image_layout &&
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL == target_layout)
		{
			imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (HasStencilComponent()) imageAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

			srcAccessMask = 0;
			dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			stage_src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			stage_dst = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		}
		else if ( // Transfer Destination Image => Shader Read-only Image
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL == m_image_layout &&
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL == target_layout)
		{
			imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;

			srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			stage_src = VK_PIPELINE_STAGE_TRANSFER_BIT;
			stage_dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else throw std::invalid_argument("Failed to transition the Vulkan Image Layout - Unsupported layout transition!");

		return VkImageMemoryBarrier
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcAccessMask = srcAccessMask,
			.dstAccessMask = dstAccessMask,
			.oldLayout = m_image_layout,
			.newLayout = target_layout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = m_image,
			.subresourceRange
			{
				.aspectMask = imageAspect,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};
	}

	std::shared_ptr<VMA::Buffer> VMA::
		AllocateStagingBuffer(VkDeviceSize buffer_size)
	{
		return AllocateBuffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, true, false);
	}

}} // namespace Albedo::RHI