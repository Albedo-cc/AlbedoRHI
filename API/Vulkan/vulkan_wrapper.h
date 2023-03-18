#pragma once

#include "vulkan_context.h"

#include <fstream>

namespace Albedo {
namespace RHI
{
	// Wrapper List
	class RenderPass;
	class GraphicsPipeline;

	// Implementation
	class RenderPass
	{
	public:
		
		operator VkRenderPass() { return m_render_pass; }

	protected:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkRenderPass m_render_pass = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> m_frame_buffers;

		// You may use some enum classes to manage the following descriptiions
		std::vector<VkAttachmentDescription> m_attachment_descriptions;

		std::vector<VkSubpassDescription> m_subpass_descriptions;
		std::vector<std::unique_ptr<GraphicsPipeline>> m_graphics_pipelines;

	public:
		RenderPass() = delete;
		RenderPass(std::shared_ptr<RHI::VulkanContext> vulkan_context);
		virtual ~RenderPass() noexcept 
		{ 
			for (auto& frame_buffer : m_frame_buffers)
				vkDestroyFramebuffer(m_context->m_device, frame_buffer, m_context->m_memory_allocator);
			vkDestroyRenderPass(m_context->m_device, m_render_pass, m_context->m_memory_allocator); 
		}
	};

	class GraphicsPipeline
	{
	public:
		virtual void Initialize(); // All derived classes have to call this function in the constructor

		VkPipelineLayout GetPipelineLayout() { return m_pipeline_layout; }
		operator VkPipeline() { return m_pipeline; }

	protected:
		virtual std::vector<VkShaderModule>						prepare_shader_modules() = 0;

		virtual VkPipelineLayoutCreateInfo							prepare_pipeline_layout_state() = 0;
		virtual VkRenderPassCreateInfo								prepare_render_pass_state() = 0;

		virtual VkPipelineVertexInputStateCreateInfo			prepare_vertex_inpute_state() = 0;
		virtual VkPipelineInputAssemblyStateCreateInfo	prepare_input_assembly_state() = 0;
		virtual VkPipelineViewportStateCreateInfo				prepare_viewport_state() = 0;
		virtual VkPipelineRasterizationStateCreateInfo		prepare_rasterization_state() = 0;
		virtual VkPipelineMultisampleStateCreateInfo		prepare_multisampling_state() = 0;
		virtual VkPipelineDepthStencilStateCreateInfo		prepare_depth_stencil_state() = 0;
		virtual VkPipelineColorBlendStateCreateInfo			prepare_color_blend_state() = 0;
		virtual VkPipelineDynamicStateCreateInfo				prepare_dynamic_state() = 0;

	public:
		GraphicsPipeline() = delete;
		GraphicsPipeline(std::shared_ptr<RHI::VulkanContext> vulkan_context) : m_context{ std::move(vulkan_context) } {}
		virtual ~GraphicsPipeline() noexcept
		{
			vkDestroyPipelineLayout(m_context->m_device, m_pipeline_layout, m_context->m_memory_allocator);
			vkDestroyPipeline(m_context->m_device, m_pipeline, m_context->m_memory_allocator);
		}

	protected:
		VkShaderModule create_shader_module(std::string_view shader_file);

	protected:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
	};

}} // namespace Albedo::RHI