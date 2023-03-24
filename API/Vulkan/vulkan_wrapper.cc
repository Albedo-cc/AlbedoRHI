#include "vulkan_wrapper.h"

namespace Albedo {
namespace RHI
{
	RenderPass::RenderPass(std::shared_ptr<RHI::VulkanContext> vulkan_context):
		m_context{ std::move(vulkan_context) }
	{

	}

	void RenderPass::Initialize()
	{
		create_attachments();
		create_subpasses();

		auto subpass_dependencies = set_subpass_dependencies();

		VkRenderPassCreateInfo renderPassCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
			.attachmentCount = static_cast<uint32_t>(m_attachment_descriptions.size()),
			.pAttachments = m_attachment_descriptions.data(),
			.subpassCount = static_cast<uint32_t>(m_subpass_descriptions.size()),
			.pSubpasses = m_subpass_descriptions.data(),
			.dependencyCount = static_cast<uint32_t>(subpass_dependencies.size()),
			.pDependencies = subpass_dependencies.data()
		};

		if (vkCreateRenderPass(
			m_context->m_device,
			&renderPassCreateInfo,
			m_context->m_memory_allocation_callback,
			&m_render_pass) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Render Pass!");

		create_pipelines();
	}

	void RenderPass::Begin(CommandPool::CommandBuffer& command_buffer, VkFramebuffer& framebuffer)
	{
		assert(command_buffer.IsRecording() && "You must Begin() the command buffer before Begin() the render pass!");

		static auto clear_color = set_clear_colors();
		VkRenderPassBeginInfo renderPassBeginInfo
		{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = m_render_pass,
			.framebuffer = framebuffer,
			.renderArea = set_render_area(),
			.clearValueCount = static_cast<uint32_t>(clear_color.size()),
			.pClearValues = clear_color.data()
		};
		VkSubpassContents contents = command_buffer.GetLevel() == VK_COMMAND_BUFFER_LEVEL_PRIMARY? 
																 VK_SUBPASS_CONTENTS_INLINE : VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS;
		vkCmdBeginRenderPass(command_buffer, &renderPassBeginInfo, contents);
	}

	void RenderPass::End(CommandPool::CommandBuffer& command_buffer)
	{
		assert(command_buffer.IsRecording() && "You must Begin() the command buffer before End() the render pass!");

		vkCmdEndRenderPass(command_buffer);
	}

	GraphicsPipeline::GraphicsPipeline(std::shared_ptr<RHI::VulkanContext> vulkan_context,
		VkRenderPass owner, uint32_t subpass_bind_point,
		VkPipeline base_pipeline/* = VK_NULL_HANDLE*/, int32_t base_pipeline_index/* = -1*/):
		m_context{std::move(vulkan_context)},
		m_owner{owner}, 
		m_subpass_bind_point { subpass_bind_point },
		m_base_pipeline { base_pipeline },
		m_base_pipeline_index { base_pipeline_index }
	{

	}

	void GraphicsPipeline::Initialize()
	{
		auto pipeline_layout_state		= prepare_pipeline_layout_state();

		if (vkCreatePipelineLayout(
			m_context->m_device,
			&pipeline_layout_state,
			m_context->m_memory_allocation_callback,
			&m_pipeline_layout) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Pipeline Layout!");

		auto shader_stage_state		= prepare_shader_stage_state();
		auto vertex_inpute_state			= prepare_vertex_inpute_state();
		auto input_assembly_state		= prepare_input_assembly_state();
		auto viewport_state					= prepare_viewport_state();
		auto rasterization_state			= prepare_rasterization_state();
		auto multisampling_state		= prepare_multisampling_state();
		auto depth_stencil_state			= prepare_depth_stencil_state();
		auto color_blend_state			= prepare_color_blend_state();
		auto dynamic_state					= prepare_dynamic_state();

		VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.pNext = nullptr,
			.flags = 0x0,

			.stageCount = static_cast<uint32_t>(shader_stage_state.size()),
			.pStages = shader_stage_state.data(),

			.pVertexInputState = &vertex_inpute_state,
			.pInputAssemblyState = &input_assembly_state,
			.pTessellationState = nullptr,
			.pViewportState = &viewport_state,
			.pRasterizationState = &rasterization_state,
			.pMultisampleState = &multisampling_state,
			.pDepthStencilState = &depth_stencil_state,
			.pColorBlendState = &color_blend_state,
			.pDynamicState = &dynamic_state,

			.layout = m_pipeline_layout,
			.renderPass = m_owner,
			.subpass = m_subpass_bind_point,
			.basePipelineHandle = m_base_pipeline,
			.basePipelineIndex = m_base_pipeline_index
		};

		if (vkCreateGraphicsPipelines(
			m_context->m_device,
			m_pipeline_cache,
			1,
			&graphicsPipelineCreateInfo,
			m_context->m_memory_allocation_callback,
			&m_pipeline) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Graphics Pipeline!");

		for (auto& shader_stage : shader_stage_state)
		{
			vkDestroyShaderModule(m_context->m_device, shader_stage.module, m_context->m_memory_allocation_callback);
		}
	}

	VkShaderModule GraphicsPipeline::create_shader_module(std::string_view shader_file)
	{
		// Check Reload
		VkShaderModule shader_module{};

		// Read File
		std::ifstream file(shader_file.data(), std::ios::ate | std::ios::binary);
		if (!file.is_open()) throw std::runtime_error(std::format("Failed to open the shader file {}!", shader_file));

		size_t file_size = static_cast<size_t>(file.tellg());
		std::vector<char> buffer(file_size);

		file.seekg(0);
		file.read(buffer.data(), file_size);

		file.close();

		// Register Shader Module
		VkShaderModuleCreateInfo shaderModuleCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
			.codeSize = buffer.size(),
			.pCode = reinterpret_cast<const uint32_t*>(buffer.data())
		};

		if (vkCreateShaderModule(
			m_context->m_device,
			&shaderModuleCreateInfo,
			m_context->m_memory_allocation_callback,
			&shader_module) != VK_SUCCESS)
			throw std::runtime_error("Failed to create shader module!");

		return shader_module;
	}

	FramebufferPool::FramebufferPool(std::shared_ptr<VulkanContext> vulkan_context) :
		m_context{ std::move(vulkan_context) }
	{

	}

	FramebufferPool::FramebufferToken FramebufferPool::
		AllocateFramebuffer(VkFramebufferCreateInfo& create_info)
	{
		auto& framebuffer = m_framebuffers.emplace_back();
		if (vkCreateFramebuffer(
			m_context->m_device,
 			&create_info,
			m_context->m_memory_allocation_callback,
			&framebuffer) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Framebuffer!");

		return size() - 1;
	}

	CommandPool::CommandBuffer::CommandBuffer(
		CommandPool* command_pool,
		VkCommandBufferLevel level):
		m_parent{ command_pool },
		m_level{ level }
	{
		VkCommandBufferAllocateInfo commandBufferAllocateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = *m_parent,
			.level = m_level,
			.commandBufferCount = 1
		};
		if (vkAllocateCommandBuffers(m_parent->m_context->m_device, &commandBufferAllocateInfo, &m_command_buffer) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Command Buffer!");
	}

	void CommandPool::CommandBuffer::Begin(VkCommandBufferUsageFlags usage_flags, VkCommandBufferResetFlags reset_flags, const VkCommandBufferInheritanceInfo* inheritanceInfo/* = nullptr*/)
	{
		assert(!IsRecording() && "You cannot Begin() a recording Vulkan Command Buffer!");

		vkResetCommandBuffer(m_command_buffer, reset_flags);

		VkCommandBufferBeginInfo commandBufferBeginInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = usage_flags,
			.pInheritanceInfo = inheritanceInfo
		};
		if (vkBeginCommandBuffer(m_command_buffer, &commandBufferBeginInfo) != VK_SUCCESS)
			throw std::runtime_error("Failed to begin the Vulkan Command Buffer!");

		m_is_recording = true;
	}

	void CommandPool::CommandBuffer::End()
	{
		assert(IsRecording() && "You cannot End() an idle Vulkan Command Buffer!");
		if (vkEndCommandBuffer(m_command_buffer) != VK_SUCCESS)
			throw std::runtime_error("Failed to end the Vulkan Command Buffer!");

		m_is_recording = false;
	}

	void CommandPool::CommandBuffer::Submit(
		uint32_t target_queue_index,
		VkFence fence,
		VkPipelineStageFlags which_pipeline_stages_to_wait,
		std::vector<VkSemaphore> wait_semaphores,
		std::vector<VkSemaphore> signal_semaphores)
	{
		VkSubmitInfo submitInfo
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size()),
			.pWaitSemaphores = wait_semaphores.data(),
			.pWaitDstStageMask = &which_pipeline_stages_to_wait,
			.commandBufferCount = 1,
			.pCommandBuffers = &m_command_buffer,
			.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data()
		};
		VkQueue target_queue = m_parent->m_context->GetQueue(m_parent->m_target_queue_family, target_queue_index);
		if (vkQueueSubmit(target_queue, 1, &submitInfo, fence) != VK_SUCCESS)
			throw std::runtime_error("Failed to submit the Vulkan Command Buffer!");
	}

	CommandPool::CommandPool(
		std::shared_ptr<VulkanContext> vulkan_context,
		uint32_t target_queue_family,
		VkCommandPoolCreateFlags command_pool_flags) :
		m_context{ std::move(vulkan_context) },
		m_target_queue_family{ target_queue_family }
	{
		VkCommandPoolCreateInfo commandPoolCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = command_pool_flags,
			.queueFamilyIndex = target_queue_family
		};

		if (vkCreateCommandPool(
			m_context->m_device,
			&commandPoolCreateInfo,
			m_context->m_memory_allocation_callback,
			&m_command_pool) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Command Pool!");
	}

	CommandPool::CommandBufferToken CommandPool::
		AllocateCommandBuffer(VkCommandBufferLevel level)
	{
		// Command buffers will be automatically freed when their command pool is destroyed
		m_command_buffers.emplace_back(this, level);
		
		return size() - 1;
	}

	Semaphore::Semaphore(std::shared_ptr<RHI::VulkanContext> vulkan_context, VkSemaphoreCreateFlags flags) :
		m_context{ std::move(vulkan_context) }
	{
		VkSemaphoreCreateInfo semaphoreCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			.flags = flags
		};
		if (vkCreateSemaphore(
			m_context->m_device,
			&semaphoreCreateInfo,
			m_context->m_memory_allocation_callback,
			&m_semaphore) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Semaphore!");
	}

	Semaphore::Semaphore(Semaphore&& rvalue) noexcept :
		m_context{ rvalue.m_context },
		m_semaphore{ rvalue.m_semaphore }
	{

	}

	Fence::Fence(std::shared_ptr<RHI::VulkanContext> vulkan_context, VkFenceCreateFlags flags) :
		m_context{ std::move(vulkan_context) }
	{
		VkFenceCreateInfo fenceCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			.flags = flags
		};
		if (vkCreateFence(
			m_context->m_device,
			&fenceCreateInfo,
			m_context->m_memory_allocation_callback,
			&m_fence) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Fence!");
	}

	Fence::Fence(Fence&& rvalue) noexcept :
		m_context{ rvalue.m_context },
		m_fence{ rvalue.m_fence }
	{

	}

}} // namespace Albedo::RHI