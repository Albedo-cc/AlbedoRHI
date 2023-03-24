#pragma once

#include <vulkan/vulkan.h>
#include <vector>

// Predeclaration
typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace Albedo {
namespace RHI
{
	class VulkanContext;

	// [Alias: using VMA = VulkanMemoryAllocator]
	class VulkanMemoryAllocator
	{
		friend class VulkanContext;
	public:
		class Buffer
		{
			friend class VulkanMemoryAllocator;
		public:
			void Write(void* data);

		public:
			Buffer() = delete;
			Buffer(VmaAllocator& allocator) : m_allocator{ allocator } {};
			~Buffer();
			operator VkBuffer() { return m_buffer; }

		private:
			VmaAllocator& m_allocator;
			VmaAllocation m_allocation = VK_NULL_HANDLE;
			VkBuffer m_buffer = VK_NULL_HANDLE;
		};

	public:
		using BufferToken = size_t;
		BufferToken CreateBuffer(size_t size, VkBufferUsageFlags usage, VkSharingMode sharing_mode);
		Buffer& GetBuffer(BufferToken token) 
		{ assert(token < m_buffers.size() && "Invalid Buffer Token!"); return m_buffers[token]; }

		~VulkanMemoryAllocator();

	private: // Created by VulkanContext
		static auto	Create(VulkanContext* vulkan_context)
		{
			struct VMACreator :public VulkanMemoryAllocator 
			{ VMACreator(VulkanContext* vulkan_context) : VulkanMemoryAllocator{ vulkan_context } {}};
			return std::make_unique<VMACreator>(vulkan_context);
		}
		VulkanMemoryAllocator(VulkanContext* vulkan_context);

	private:
		VulkanContext* const m_context;
		VmaAllocator m_allocator = VK_NULL_HANDLE;

		std::vector<Buffer> m_buffers;
	};

	using VMA = VulkanMemoryAllocator;

}} // namespace Albedo::RHI