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
			.vulkanApiVersion  = VK_API_VERSION_1_3 // Default Version 1.0
		};
		
		if (vmaCreateAllocator(&vmaAllocatorCreateInfo, &m_allocator) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the VMA (Vulkan Memory Allocator)!");
	}

	VulkanMemoryAllocator::~VulkanMemoryAllocator()
	{
		vmaDestroyAllocator(m_allocator);
	}

	std::shared_ptr<VulkanMemoryAllocator::Buffer> VulkanMemoryAllocator::
		CreateBuffer(size_t size, VkBufferUsageFlags usage, 
			bool is_exclusive /*= true*/, bool is_writable/* = false*/, bool is_readable/* = false*/)
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
		if (is_writable)	allocation_flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		if (is_readable)	allocation_flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		VmaAllocationCreateInfo allocationInfo
		{
			.flags = allocation_flags,
			.usage = VMA_MEMORY_USAGE_AUTO,
			//.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
		}; 

		auto buffer = std::make_shared<VulkanMemoryAllocator::Buffer>(m_allocator);

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

	VulkanMemoryAllocator::Buffer::~Buffer() 
	{ 
		vmaDestroyBuffer(m_allocator, m_buffer, m_allocation); 
	}

	void VulkanMemoryAllocator::Buffer::Write(void* data)
	{
		void* mappedArea;
		vmaMapMemory(m_allocator, m_allocation, &mappedArea);
		memcpy(mappedArea, data, m_allocation->GetSize());
		vmaUnmapMemory(m_allocator, m_allocation);
	}

	VkDeviceSize VulkanMemoryAllocator::Buffer::Size()
	{
		return m_allocation->GetSize();
	}

}} // namespace Albedo::RHI