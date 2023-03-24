#pragma once

#include "vulkan_context.h"

#include <fstream>

namespace Albedo {
namespace RHI
{
	// Wrapper List
	class RenderPass;
	class GraphicsPipeline;

	class CommandPool;		// Factory // Nested Class: CommandPool::CommandBuffer;
	class FramebufferPool;	// Factory

	class Semaphore;				// Add order between queue operations (same queue or different queues) on the GPU
	class Fence;						// order the execution on the CPU

	// Implementation
	class CommandPool
	{
	public:
		using CommandBufferToken = size_t;
		friend class CommandBuffer;
		class CommandBuffer
		{
		public:
			void Begin(VkCommandBufferUsageFlags usage_flags, VkCommandBufferResetFlags reset_flags, const VkCommandBufferInheritanceInfo* inheritanceInfo = nullptr);
			void End();
			void Submit(uint32_t target_queue_index, VkFence fence,
				VkPipelineStageFlags which_pipeline_stages_to_wait,
				std::vector<VkSemaphore> wait_semaphores, 
				std::vector<VkSemaphore> signal_semaphores);

			bool IsRecording() const { return m_is_recording; }
			VkCommandBufferLevel GetLevel() const { return m_level; }
			operator VkCommandBuffer() { return m_command_buffer; }

		public:
			CommandBuffer() = delete;
			CommandBuffer(CommandPool* command_pool, VkCommandBufferLevel level);

		private:
			CommandPool* const m_parent;
			VkCommandBuffer m_command_buffer = VK_NULL_HANDLE;
			VkCommandBufferLevel m_level;
			bool m_is_recording = false;
		}; // class CommandBuffer

	public:
		CommandBufferToken	AllocateCommandBuffer(VkCommandBufferLevel level);
		CommandBuffer&			GetCommandBuffer(CommandBufferToken token) { return m_command_buffers[token]; }

		size_t size() const { return m_command_buffers.size(); }

		operator VkCommandPool() { return m_command_pool; }
		CommandBuffer& operator[](CommandBufferToken token) { return m_command_buffers[token]; }

	public:
		CommandPool() = delete;
		CommandPool(std::shared_ptr<VulkanContext> vulkan_context,
									uint32_t target_queue_family,
									VkCommandPoolCreateFlags command_pool_flags);
		~CommandPool() { vkDestroyCommandPool(m_context->m_device, m_command_pool, m_context->m_memory_allocation_callback); }

	private:
		std::shared_ptr<VulkanContext> m_context;
		uint32_t m_target_queue_family;

		VkCommandPool m_command_pool = VK_NULL_HANDLE;
		std::vector<CommandBuffer> m_command_buffers;
	};

	class FramebufferPool
	{
		using FramebufferToken = size_t;
	public:
		FramebufferToken AllocateFramebuffer(VkFramebufferCreateInfo& create_info);
		VkFramebuffer& GetFramebuffer(FramebufferToken token) { return m_framebuffers[token]; }

		size_t size() const { return m_framebuffers.size(); }
		void clear()
		{
			if (m_framebuffers.empty()) return;
			for (auto& frame_buffer : m_framebuffers)
				vkDestroyFramebuffer(m_context->m_device, frame_buffer, m_context->m_memory_allocation_callback);
			m_framebuffers.clear();
		}
		VkFramebuffer& operator[](FramebufferToken token){ return m_framebuffers[token]; }

	public:
		FramebufferPool() = delete;
		FramebufferPool(std::shared_ptr<VulkanContext> vulkan_context);
		~FramebufferPool() { clear(); }

	private:
		std::shared_ptr<VulkanContext> m_context;
		std::vector<VkFramebuffer> m_framebuffers;
	};

	class RenderPass
	{
	public:
		// All derived classes have to call initialize() before beginning the render pass.
		virtual void Initialize();

		virtual void Begin(CommandPool::CommandBuffer& command_buffer, VkFramebuffer& framebuffer);
		virtual void Render(RHI::CommandPool::CommandBuffer& command_buffer) = 0; // You may call pipelines to Draw() here
		virtual void End(CommandPool::CommandBuffer& command_buffer);

		void SetCurrentFrameBufferIndex(size_t index) { m_current_frame_buffer_index = index; }
		operator VkRenderPass() { return m_render_pass; }

	protected:
		virtual std::vector<VkSubpassDependency> set_subpass_dependencies() = 0;

		virtual void create_attachments() = 0;
		virtual void create_subpasses() = 0;	
		virtual void create_pipelines() = 0;

		virtual std::vector<VkClearValue>	set_clear_colors() { return { { { {0.0,0.0,0.0,1.0} } } }; }  // use for VK_ATTACHMENT_LOAD_OP_CLEAR
		virtual VkRect2D								set_render_area() { return { { 0,0 }, m_context->m_swapchain_current_extent }; }

	protected:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkRenderPass m_render_pass = VK_NULL_HANDLE;
		size_t m_current_frame_buffer_index = 0;

		// You may use some enum classes to manage the following descriptiions
		std::vector<VkAttachmentDescription> m_attachment_descriptions;
		std::vector<VkAttachmentReference> m_attachment_references;

		std::vector<VkSubpassDescription> m_subpass_descriptions;
		std::vector<std::unique_ptr<GraphicsPipeline>> m_graphics_pipelines;

	public:
		RenderPass() = delete;
		RenderPass(std::shared_ptr<RHI::VulkanContext> vulkan_context);
		virtual ~RenderPass() noexcept 
		{ 
			vkDestroyRenderPass(m_context->m_device, m_render_pass, m_context->m_memory_allocation_callback); 
		}
	};

	class GraphicsPipeline
	{
	public:
		// All derived classes have to call initialize() before beginning the render pass.
		virtual void Initialize();

		virtual void Draw(RHI::CommandPool::CommandBuffer& command_buffer) = 0; // vkCmdDraw ...

		VkPipelineLayout GetPipelineLayout() { return m_pipeline_layout; }
		operator VkPipeline() { return m_pipeline; }

	protected:
		virtual std::vector<VkPipelineShaderStageCreateInfo>		prepare_shader_stage_state()		= 0; // Helper: create_shader_module()
		virtual VkPipelineLayoutCreateInfo										prepare_pipeline_layout_state()		= 0;

		virtual VkPipelineVertexInputStateCreateInfo						prepare_vertex_inpute_state()		= 0;
		virtual VkPipelineInputAssemblyStateCreateInfo				prepare_input_assembly_state()	= 0;
		virtual VkPipelineViewportStateCreateInfo							prepare_viewport_state()				= 0; // m_viewports & m_scissors
		virtual VkPipelineRasterizationStateCreateInfo					prepare_rasterization_state()			= 0;
		virtual VkPipelineMultisampleStateCreateInfo					prepare_multisampling_state()		= 0;
		virtual VkPipelineDepthStencilStateCreateInfo					prepare_depth_stencil_state()		= 0;
		virtual VkPipelineColorBlendStateCreateInfo						prepare_color_blend_state()			= 0;
		virtual VkPipelineDynamicStateCreateInfo							prepare_dynamic_state()				= 0;

	public:
		GraphicsPipeline() = delete;
		GraphicsPipeline(std::shared_ptr<RHI::VulkanContext> vulkan_context, 
										VkRenderPass owner, uint32_t subpass_bind_point, 
										VkPipeline base_pipeline = VK_NULL_HANDLE, int32_t base_pipeline_index = -1);
		virtual ~GraphicsPipeline() noexcept
		{
			vkDestroyPipelineLayout(m_context->m_device, m_pipeline_layout, m_context->m_memory_allocation_callback);
			vkDestroyPipeline(m_context->m_device, m_pipeline, m_context->m_memory_allocation_callback);
		}

	protected:
		VkShaderModule create_shader_module(std::string_view shader_file);

	protected:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkPipeline					m_pipeline					= VK_NULL_HANDLE; 
		VkPipelineLayout		m_pipeline_layout	= VK_NULL_HANDLE;
		VkPipelineCache		m_pipeline_cache		= VK_NULL_HANDLE;
		VkPipeline					m_base_pipeline;
		int32_t						m_base_pipeline_index;

		VkRenderPass						m_owner;
		uint32_t									m_subpass_bind_point;
		std::vector<VkViewport>		m_viewports;
		std::vector<VkRect2D>		m_scissors;
	};

	class Semaphore
	{
	public:
		Semaphore() = delete;
		Semaphore(std::shared_ptr<RHI::VulkanContext> vulkan_context, VkSemaphoreCreateFlags flags);
		~Semaphore() { log::warn("Del Sema"); vkDestroySemaphore(m_context->m_device, m_semaphore, m_context->m_memory_allocation_callback); }
		Semaphore(const Semaphore&) = delete;
		Semaphore(Semaphore&& rvalue) noexcept;
		operator VkSemaphore() { return m_semaphore; }

	private:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkSemaphore m_semaphore = VK_NULL_HANDLE;
	};

	class Fence
	{
	public:
		void Wait(bool reset = false, uint64_t timeout = std::numeric_limits<uint64_t>::max()) 
		{ 
			vkWaitForFences(m_context->m_device, 1, &m_fence, VK_TRUE, timeout); 
			if (reset) Reset();
		}

		void Reset()
		{
			vkResetFences(m_context->m_device, 1, &m_fence);
		}

		Fence() = delete;
		Fence(std::shared_ptr<RHI::VulkanContext> vulkan_context, VkFenceCreateFlags flags);
		~Fence() { log::warn("Del Fen"); vkDestroyFence(m_context->m_device, m_fence, m_context->m_memory_allocation_callback); }
		Fence(const Fence&) = delete;
		Fence(Fence&& rvalue) noexcept;
		operator VkFence() { return m_fence; }

	private:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkFence m_fence = VK_NULL_HANDLE;
	};

}} // namespace Albedo::RHI