#include "vulkan_context.h"
#include "vulkan_memory.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <cstring>

namespace Albedo {
namespace RHI
{

	VulkanMemoryAllocator::VulkanMemoryAllocator(VulkanContext* vulkan_context) :
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
			.vulkanApiVersion  = VK_API_VERSION_1_2 // Default Version 1.0
		};
		
		if (vmaCreateAllocator(&vmaAllocatorCreateInfo, &m_allocator) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the VMA (Vulkan Memory Allocator)!");
	}

	VulkanMemoryAllocator::~VulkanMemoryAllocator()
	{
		vmaDestroyAllocator(m_allocator);
	}

	VulkanMemoryAllocator::BufferToken VulkanMemoryAllocator::
		CreateBuffer(size_t size, VkBufferUsageFlags usage, VkSharingMode sharing_mode)
	{
		VkBufferCreateInfo bufferCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
			.size = size,
			.usage = usage,
			.sharingMode = sharing_mode
		};

		VmaAllocationCreateInfo allocationInfo
		{
			.usage = VMA_MEMORY_USAGE_AUTO
		}; 

		auto& buffer = m_buffers.emplace_back(m_allocator);

		if (vmaCreateBuffer(
			m_allocator,
			&bufferCreateInfo,
			&allocationInfo,
			&buffer.m_buffer,
			&buffer.m_allocation,
			nullptr) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Buffer!");

		return m_buffers.size() - 1;
	}

	VulkanMemoryAllocator::Buffer::~Buffer() 
	{ 
		vmaDestroyBuffer(m_allocator, m_buffer, m_allocation); 
	}

	void VulkanMemoryAllocator::Buffer::Write(void* data)
	{
		vmaMapMemory(m_allocator, m_allocation, &data);
		memcpy(data, &m_buffer, m_allocation->GetSize());
		vmaUnmapMemory(m_allocator, m_allocation);
	}

}} // namespace Albedo::RHI