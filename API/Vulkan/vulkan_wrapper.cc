#include "vulkan_context.h"
#include "vulkan_wrapper.h"

#include <fstream>

#include <spirv_reflect.h>

namespace Albedo {
namespace RHI
{
	RenderPass::RenderPass(std::shared_ptr<RHI::VulkanContext> vulkan_context):
		m_context{ std::move(vulkan_context) }
	{

	}

	RenderPass::~RenderPass()
	{
		for (auto& graphics_pipeline : m_graphics_pipelines)
		{
			delete graphics_pipeline;
		}
		for (auto& frame_buffer : m_framebuffers)
		{
			vkDestroyFramebuffer(m_context->m_device, frame_buffer, m_context->m_memory_allocation_callback);
		}
		vkDestroyRenderPass(m_context->m_device, m_render_pass, m_context->m_memory_allocation_callback);
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

		create_framebuffers();
		create_pipelines();
	}

	void RenderPass::Begin(std::shared_ptr<CommandBuffer> command_buffer)
	{
		assert(command_buffer->IsRecording() && "You must Begin() the command buffer before Begin() the render pass!");

		static auto clear_color = set_attachment_clear_colors();
		auto& current_framebuffer = m_framebuffers[m_context->m_swapchain_current_image_index];
		VkRenderPassBeginInfo renderPassBeginInfo
		{
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = m_render_pass,
			.framebuffer = current_framebuffer,
			.renderArea = set_render_area(),
			.clearValueCount = static_cast<uint32_t>(clear_color.size()),
			.pClearValues = clear_color.data()
		};
		VkSubpassContents contents = command_buffer->GetLevel() == VK_COMMAND_BUFFER_LEVEL_PRIMARY? 
																 VK_SUBPASS_CONTENTS_INLINE : VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS;
		vkCmdBeginRenderPass(*command_buffer, &renderPassBeginInfo, contents);
	}

	void RenderPass::End(std::shared_ptr<CommandBuffer> command_buffer)
	{
		assert(command_buffer->IsRecording() && "You must Begin() the command buffer before End() the render pass!");

		vkCmdEndRenderPass(*command_buffer);
	}

	VkRect2D RenderPass::set_render_area()
	{ 
		return { { 0,0 }, m_context->m_swapchain_current_extent };
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

	GraphicsPipeline::~GraphicsPipeline()
	{
		for (auto& descriptor_set_layout : m_descriptor_set_layouts)
			vkDestroyDescriptorSetLayout(m_context->m_device, descriptor_set_layout, m_context->m_memory_allocation_callback);
		vkDestroyPipelineLayout(m_context->m_device, m_pipeline_layout, m_context->m_memory_allocation_callback);
		vkDestroyPipeline(m_context->m_device, m_pipeline, m_context->m_memory_allocation_callback);
	}

	void GraphicsPipeline::Initialize()
	{
		// --------------------------------------------------------------------------------------------------------------------------------//
		// 1. Create Shader Stages
		// --------------------------------------------------------------------------------------------------------------------------------//
		// Shaders
		std::vector<VkPipelineShaderStageCreateInfo> shaderInfos(MAX_SHADER_COUNT);
		auto shaders = prepare_shader_files();
		// Vertex Shader
		std::vector<char> vertex_shader_buffer;
		shaderInfos[vertex_shader].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderInfos[vertex_shader].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderInfos[vertex_shader].module = create_shader_module(shaders[vertex_shader], vertex_shader_buffer);
		shaderInfos[vertex_shader].pName = "main";
		// Fragment Shader
		std::vector<char> fragment_shader_buffer;
		shaderInfos[fragment_shader].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderInfos[fragment_shader].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderInfos[fragment_shader].module = create_shader_module(shaders[fragment_shader], fragment_shader_buffer);
		shaderInfos[fragment_shader].pName = "main";
		// --------------------------------------------------------------------------------------------------------------------------------//



		// --------------------------------------------------------------------------------------------------------------------------------//
		// 2. Create Pipeline Layout
		// --------------------------------------------------------------------------------------------------------------------------------//
		// Descriptor Set Layouts & Push Constants
		m_descriptor_set_layouts = prepare_descriptor_layouts();
		auto push_constant_state = prepare_push_constant_state();
		deduce_pipeline_states_from_shaders(vertex_shader_buffer, fragment_shader_buffer,
			(m_descriptor_set_layouts.empty() ? &m_descriptor_set_layouts : nullptr),
			(push_constant_state.empty() ? &push_constant_state : nullptr));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = static_cast<uint32_t>(m_descriptor_set_layouts.size()),
			.pSetLayouts = m_descriptor_set_layouts.data(),
			.pushConstantRangeCount = static_cast<uint32_t>(push_constant_state.size()),
			.pPushConstantRanges = push_constant_state.data()
		};

		if (vkCreatePipelineLayout(
			m_context->m_device,
			&pipelineLayoutCreateInfo,
			m_context->m_memory_allocation_callback,
			&m_pipeline_layout) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Pipeline Layout!");
		// --------------------------------------------------------------------------------------------------------------------------------//



		// --------------------------------------------------------------------------------------------------------------------------------//
		// 3. Create Graphics Pipeline
		// --------------------------------------------------------------------------------------------------------------------------------//
		auto vertex_inpute_state			= prepare_vertex_input_state();
		auto input_assembly_state		= prepare_input_assembly_state();
		auto tessellation_state			= prepare_tessellation_state();
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

			.stageCount = static_cast<uint32_t>(shaderInfos.size()),
			.pStages = shaderInfos.data(),

			.pVertexInputState = &vertex_inpute_state,
			.pInputAssemblyState = &input_assembly_state,
			.pTessellationState = &tessellation_state,
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
		// --------------------------------------------------------------------------------------------------------------------------------//



		// --------------------------------------------------------------------------------------------------------------------------------//
		// 4. Free Resource
		// --------------------------------------------------------------------------------------------------------------------------------//
		// Shader Modules
		for (auto& shaderInfo : shaderInfos)
		{
			vkDestroyShaderModule(m_context->m_device, shaderInfo.module,
														 m_context->m_memory_allocation_callback);
		}
		// --------------------------------------------------------------------------------------------------------------------------------//
	}

	std::vector<VkDescriptorSetLayout> GraphicsPipeline::
		prepare_descriptor_layouts()
	{
		return {};
	}

	std::vector<VkPushConstantRange> GraphicsPipeline::
		prepare_push_constant_state()
	{
		return {};
	}

	VkPipelineVertexInputStateCreateInfo GraphicsPipeline::
		prepare_vertex_input_state()
	{
		return {};
	}

	VkPipelineTessellationStateCreateInfo GraphicsPipeline::
		prepare_tessellation_state()
	{
		

		return VkPipelineTessellationStateCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
			.patchControlPoints = 0 // Disable
		};
	}

	VkPipelineMultisampleStateCreateInfo GraphicsPipeline::
		prepare_multisampling_state()
	{
		return VkPipelineMultisampleStateCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
			.sampleShadingEnable = VK_FALSE,
			.minSampleShading = 1.0f,
			.pSampleMask = nullptr,
			.alphaToCoverageEnable = VK_FALSE,
			.alphaToOneEnable = VK_FALSE
		};
	}

	VkPipelineDepthStencilStateCreateInfo GraphicsPipeline::
		prepare_depth_stencil_state()
	{
		return VkPipelineDepthStencilStateCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_FALSE,
			.depthWriteEnable = VK_FALSE,
			.depthCompareOp = VK_COMPARE_OP_LESS, // Keep fragments, which has lower depth
			.depthBoundsTestEnable = VK_FALSE,  // Only keep fragments that fall within the specified depth range.
			.stencilTestEnable = VK_FALSE,
			.front = {},
			.back = {},
			.minDepthBounds = 0.0,
			.maxDepthBounds = 1.0
		};
	}

	VkPipelineDynamicStateCreateInfo GraphicsPipeline::
		prepare_dynamic_state()
	{
		return VkPipelineDynamicStateCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 0,
			.pDynamicStates = nullptr
		};
	}

	VkShaderModule GraphicsPipeline::create_shader_module(std::string_view shader_file, std::vector<char>& buffer)
	{
		// Check Reload
		VkShaderModule shader_module{};

		// Read File
		std::ifstream file(shader_file.data(), std::ios::ate | std::ios::binary);
		if (!file.is_open()) throw std::runtime_error(std::format("Failed to open the shader file {}!", shader_file));

		size_t file_size = static_cast<size_t>(file.tellg());
		buffer.resize(file_size);

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
			throw std::runtime_error(std::format("Failed to create shader module {}!", shader_file));

		return shader_module;
	}

	void GraphicsPipeline::deduce_pipeline_states_from_shaders(
		std::vector<char>& vertex_shader,
		std::vector<char>& fragment_shader,
		std::vector<VkDescriptorSetLayout>* descriptor_set_layouts,
		std::vector<VkPushConstantRange>* push_constants)
	{
		SpvReflectShaderModule spvContext;
		std::vector<DescriptorBinding> descriptor_set_layout_bindings;
		uint32_t count = 0;
		// 1. Vertex Shader
		{
			if (spvReflectCreateShaderModule(vertex_shader.size(), vertex_shader.data(), &spvContext) != SPV_REFLECT_RESULT_SUCCESS)
				throw std::runtime_error("Failed to reflect vertex shader!");

			if (descriptor_set_layouts)
			{
				if (spvReflectEnumerateDescriptorSets(&spvContext, &count, NULL) != SPV_REFLECT_RESULT_SUCCESS)
					throw std::runtime_error("Failed to reflect descriptor sets of vertex shader!");

				std::vector<SpvReflectDescriptorSet*> pDescriptorSets(count);
				if (spvReflectEnumerateDescriptorSets(&spvContext, &count, pDescriptorSets.data()) != SPV_REFLECT_RESULT_SUCCESS)
					throw std::runtime_error("Failed to enumerate descriptor sets of vertex shader!");

				for(const auto& descriptorSets : pDescriptorSets)
				{
					DescriptorBinding descriptorBinding{ .set = descriptorSets->set, .stages = VK_SHADER_STAGE_VERTEX_BIT };

					log::debug("Set {} Bindings {}", descriptorSets->set, descriptorSets->binding_count);
					auto& bindings = descriptorSets->bindings;
					for (uint32_t i = 0; i < descriptorSets->binding_count; ++i)
					{
						descriptorBinding.binding = bindings[i]->binding;
						descriptorBinding.count = bindings[i]->count;
						descriptorBinding.type = static_cast<VkDescriptorType>(bindings[i]->descriptor_type);
						log::debug("binding {}, name {}, count {}", bindings[i]->binding, bindings[i]->name, bindings[i]->count);
						descriptor_set_layout_bindings.emplace_back(descriptorBinding);
					}
				}
			} // End deduce Descriptor Sets

			if (push_constants)
			{
				if (spvReflectEnumeratePushConstants(&spvContext, &count, NULL) != SPV_REFLECT_RESULT_SUCCESS)
					throw std::runtime_error("Failed to reflect push constants of vertex shader!");

				std::vector<SpvReflectBlockVariable*> pPushConstants(count);
				if (spvReflectEnumeratePushConstants(&spvContext, &count, pPushConstants.data()) != SPV_REFLECT_RESULT_SUCCESS)
					throw std::runtime_error("Failed to reflect enumerate constants of vertex shader!");

				for(const auto& pushConstant : pPushConstants)
				{
					log::debug("Push Constant: offset {}, size {}", pushConstant->offset, pushConstant->size);
					push_constants->emplace_back(
						VkPushConstantRange
						{
							.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
							.offset = pushConstant->offset,
							.size = pushConstant->size
						});
				}
			} // End deduce Push Constants

			spvReflectDestroyShaderModule(&spvContext);
		} // End parse Vertex Shader
		


		// 2. Fragment Shader
		{
			if (spvReflectCreateShaderModule(fragment_shader.size(), fragment_shader.data(), &spvContext) != SPV_REFLECT_RESULT_SUCCESS)
				throw std::runtime_error("Failed to reflect fragment shader!");

			if (descriptor_set_layouts)
			{
				if (spvReflectEnumerateDescriptorSets(&spvContext, &count, NULL) != SPV_REFLECT_RESULT_SUCCESS)
					throw std::runtime_error("Failed to reflect descriptor sets of fragment shader!");

				std::vector<SpvReflectDescriptorSet*> pDescriptorSets(count);
				if (spvReflectEnumerateDescriptorSets(&spvContext, &count, pDescriptorSets.data()) != SPV_REFLECT_RESULT_SUCCESS)
					throw std::runtime_error("Failed to enumerate descriptor sets of fragment shader!");

				for (const auto& descriptorSets : pDescriptorSets)
				{
					DescriptorBinding descriptorBinding{ .set = descriptorSets->set, .stages = VK_SHADER_STAGE_FRAGMENT_BIT };

					log::debug("Set {} Bindings {}", descriptorSets->set, descriptorSets->binding_count);
					auto& bindings = descriptorSets->bindings;
					for (uint32_t i = 0; i < descriptorSets->binding_count; ++i)
					{
						descriptorBinding.binding = bindings[i]->binding;
						descriptorBinding.count = bindings[i]->count;
						descriptorBinding.type = static_cast<VkDescriptorType>(bindings[i]->descriptor_type);
						log::debug("binding {}, name {}, count {}", bindings[i]->binding, bindings[i]->name, bindings[i]->count);
						descriptor_set_layout_bindings.emplace_back(descriptorBinding);
					}
				}
			} // End deduce Descriptor Sets

			if (push_constants)
			{
				if (spvReflectEnumeratePushConstants(&spvContext, &count, NULL) != SPV_REFLECT_RESULT_SUCCESS)
					throw std::runtime_error("Failed to reflect push constants of vertex shader!");

				std::vector<SpvReflectBlockVariable*> pPushConstants(count);
				if (spvReflectEnumeratePushConstants(&spvContext, &count, pPushConstants.data()) != SPV_REFLECT_RESULT_SUCCESS)
					throw std::runtime_error("Failed to reflect enumerate constants of vertex shader!");

				for (const auto& pushConstant : pPushConstants)
				{
					log::debug("Push Constant: offset {}, size {}", pushConstant->offset, pushConstant->size);
					push_constants->emplace_back(
						VkPushConstantRange
						{
							.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
							.offset = pushConstant->offset,
							.size = pushConstant->size
						});
				}
			} // End deduce Push Constants

			spvReflectDestroyShaderModule(&spvContext);
		} // End parse Fragment Shader



		// Final. Create Resource
		if (descriptor_set_layouts && !descriptor_set_layout_bindings.empty())
		{
			if (descriptor_set_layout_bindings.size() > 1)
				std::sort(descriptor_set_layout_bindings.begin(), descriptor_set_layout_bindings.end(),
					[](const DescriptorBinding& prev, const DescriptorBinding& next)->bool
					{
						if (next.set != prev.set) return next.set < prev.set; // Descending Set Index
						else return next.binding < prev.binding; // Descending Binding Index
					});

			size_t max_set = descriptor_set_layout_bindings.front().set + 1;
			descriptor_set_layouts->resize(max_set);
			std::vector<std::vector<VkDescriptorSetLayoutBinding>> descriptorSets(max_set);

			for (auto& currentBinding : descriptor_set_layout_bindings)
			{
				auto& currentSet = descriptorSets[currentBinding.set];
				if (currentSet.empty())
				{
					currentSet.reserve(currentBinding.binding + 1); // DESC Sorted
					currentSet.emplace_back(currentBinding);
				}
				else
				{
					auto& previousBinding = currentSet.back();
					if (currentBinding.binding == previousBinding.binding)
					{
						previousBinding.stageFlags |= currentBinding.stages; // Same binding but different stages.
					}
					else currentSet.emplace_back(currentBinding); // Differernt bindings
				}
			}
			// Create Descriptor Set Layouts
			for (size_t current_set = 0; current_set < max_set; ++current_set)
			{
				auto& descriptor_set_layout = m_descriptor_set_layouts[current_set];
				VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo
				{
					.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
					.bindingCount = static_cast<uint32_t>(descriptorSets[current_set].size()),
					.pBindings = descriptorSets[current_set].data()
				};
				if (vkCreateDescriptorSetLayout(
					m_context->m_device,
					&descriptorSetLayoutCreateInfo,
					m_context->m_memory_allocation_callback,
					&descriptor_set_layout) != VK_SUCCESS)
					throw std::runtime_error("Failed to create the Vulkan Desceriptor Set Layout automatically!");
			}
		} // End create Descriptor Set Layouts
		
		if (push_constants && !push_constants->empty())
		{
			if (push_constants->size() > 1)
				std::sort(push_constants->begin(), push_constants->end(),
					[](const VkPushConstantRange& next, const VkPushConstantRange& prev)->bool
					{
						if (next.offset == prev.offset) return next.size < prev.size; // Descending Range Size
						else return next.offset < prev.offset; // Descending Range Offset
					});

			std::vector<VkPushConstantRange> pushConstants{ push_constants->front() };
			for (size_t i = 1; i < push_constants->size(); ++i)
			{
				auto& prevPushConstant = (*push_constants)[i - 1];
				auto& curPushConstant = (*push_constants)[i];
				if (curPushConstant.offset == prevPushConstant.offset &&
					curPushConstant.size == prevPushConstant.size)
				{
					prevPushConstant.stageFlags |= curPushConstant.stageFlags; // Same PushConstant but different stages.
				}
				else pushConstants.emplace_back(curPushConstant); // Differernt PushConstants
			}
			push_constants->swap(pushConstants);
		} // End arrange Descriptor Set Layouts

	}

	CommandBuffer::CommandBuffer(std::shared_ptr<CommandPool> parent, VkCommandBufferLevel level) :
		m_parent{ std::move(parent) }, m_level{ level }
	{
		
	}

	void CommandBufferReset::Begin(VkCommandBufferInheritanceInfo* inheritanceInfo/* = nullptr*/)
	{
		assert(!IsRecording() && "You cannot Begin() a recording Vulkan Command Buffer!");

		vkResetCommandBuffer(command_buffer, 0);

		VkCommandBufferBeginInfo commandBufferBeginInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = 0,
			.pInheritanceInfo = inheritanceInfo
		};
		if (vkBeginCommandBuffer(command_buffer, &commandBufferBeginInfo) != VK_SUCCESS)
			throw std::runtime_error("Failed to begin the Vulkan Command Buffer!");

		m_is_recording = true;
	}

	void CommandBufferReset::End()
	{
		assert(IsRecording() && "You cannot End() an idle Vulkan Command Buffer!");
		if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS)
			throw std::runtime_error("Failed to end the Vulkan Command Buffer!");
		m_is_recording = false;
	}

	void CommandBufferReset::Submit(
		bool wait_queue_idle/* = false*/,
		VkFence fence/* = VK_NULL_HANDLE*/,
		std::vector<VkSemaphore> wait_semaphores/* = {}*/,
		std::vector<VkSemaphore> signal_semaphores/* = {}*/,
		VkPipelineStageFlags which_pipeline_stages_to_wait/* = 0*/,
		uint32_t target_queue_index/* = 0*/)
	{
		VkSubmitInfo submitInfo
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size()),
			.pWaitSemaphores = wait_semaphores.data(),
			.pWaitDstStageMask = &which_pipeline_stages_to_wait,
			.commandBufferCount = 1,
			.pCommandBuffers = &command_buffer,
			.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data()
		};
		
		auto& submit_queue = m_parent->m_submit_queue_family;
		if (vkQueueSubmit(submit_queue, 1, &submitInfo, fence) != VK_SUCCESS)
			throw std::runtime_error("Failed to submit the Vulkan Command Buffer!");
		if (wait_queue_idle) vkQueueWaitIdle(submit_queue);
	}

	void CommandBufferOneTime::Begin(VkCommandBufferInheritanceInfo* inheritanceInfo/* = nullptr*/)
	{
		assert(!IsRecording() && "You cannot Begin() a recording Vulkan Command Buffer!");

		VkCommandBufferBeginInfo commandBufferBeginInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
			.pInheritanceInfo = inheritanceInfo
		};
		if (vkBeginCommandBuffer(command_buffer, &commandBufferBeginInfo) != VK_SUCCESS)
			throw std::runtime_error("Failed to begin the Vulkan Command Buffer!");

		m_is_recording = true;
	}

	void CommandBufferOneTime::End()
	{
		assert(IsRecording() && "You cannot End() an idle Vulkan Command Buffer!");
		if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS)
			throw std::runtime_error("Failed to end the Vulkan Command Buffer!");
		m_is_recording = false;
	}

	void CommandBufferOneTime::Submit(
		bool wait_queue_idle/* = false*/,
		VkFence fence/* = VK_NULL_HANDLE*/,
		std::vector<VkSemaphore> wait_semaphores/* = {}*/,
		std::vector<VkSemaphore> signal_semaphores/* = {}*/,
		VkPipelineStageFlags which_pipeline_stages_to_wait/* = 0*/,
		uint32_t target_queue_index/* = 0*/)
	{
		VkSubmitInfo submitInfo
		{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size()),
			.pWaitSemaphores = wait_semaphores.data(),
			.pWaitDstStageMask = &which_pipeline_stages_to_wait,
			.commandBufferCount = 1,
			.pCommandBuffers = &command_buffer,
			.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data()
		};

		auto& submit_queue = m_parent->m_submit_queue_family;
		if (vkQueueSubmit(submit_queue, 1, &submitInfo, fence) != VK_SUCCESS)
			throw std::runtime_error("Failed to submit the Vulkan Command Buffer!");
		if (wait_queue_idle) vkQueueWaitIdle(submit_queue);

		vkFreeCommandBuffers(m_parent->m_context->m_device, *m_parent, 1, &command_buffer);
	}

	CommandPool::CommandPool(
		std::shared_ptr<VulkanContext> vulkan_context,
		QueueFamilyIndex& submit_queue_family_index,
		VkCommandPoolCreateFlags command_pool_flags) :
		m_context{ std::move(vulkan_context) },
		m_submit_queue_family{ m_context->GetQueue(submit_queue_family_index) },
		m_command_pool_flags{ command_pool_flags }
	{
		VkCommandPoolCreateInfo commandPoolCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = command_pool_flags,
			.queueFamilyIndex = submit_queue_family_index.value()
		};

		if (vkCreateCommandPool(
			m_context->m_device,
			&commandPoolCreateInfo,
			m_context->m_memory_allocation_callback,
			&m_command_pool) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Command Pool!");
	}

	CommandPool::~CommandPool()
	{
		vkDestroyCommandPool(m_context->m_device, m_command_pool, m_context->m_memory_allocation_callback);
	}

	std::shared_ptr<CommandBuffer> CommandPool::
		AllocateCommandBuffer(VkCommandBufferLevel level)
	{
		// Command buffers will be automatically freed when their command pool is destroyed
		std::shared_ptr<CommandBuffer> commandbuffer;
		if (m_command_pool_flags & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
		{
			commandbuffer = std::make_shared<CommandBufferReset>(shared_from_this(), level);
		}
		else if (m_command_pool_flags & VK_COMMAND_POOL_CREATE_TRANSIENT_BIT)
		{
			commandbuffer = std::make_shared<CommandBufferOneTime>(shared_from_this(), level);
		}
		else throw std::runtime_error("Failed to allocate a proper Vulkan Command Buffer!");

		VkCommandBufferAllocateInfo commandBufferAllocateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = m_command_pool,
			.level = level,
			.commandBufferCount = 1
		};

		if (vkAllocateCommandBuffers(
			m_context->m_device, 
			&commandBufferAllocateInfo, 
			&commandbuffer->command_buffer) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Command Buffer!");

		return commandbuffer;
	}

	DescriptorSetLayout::DescriptorSetLayout(std::shared_ptr<RHI::VulkanContext> vulkan_context,
		const std::vector<VkDescriptorSetLayoutBinding>& descriptor_bindings) :
		m_context{ std::move(vulkan_context) }
	{
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = static_cast<uint32_t>(descriptor_bindings.size()),
			.pBindings = descriptor_bindings.data()
		};

		if (vkCreateDescriptorSetLayout(
			m_context->m_device,
			&descriptorSetLayoutCreateInfo,
			m_context->m_memory_allocation_callback,
			&m_descriptor_set_layout) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Descriptor Set Layout!");
	}

	DescriptorSetLayout::~DescriptorSetLayout()
	{
		vkDestroyDescriptorSetLayout(m_context->m_device, m_descriptor_set_layout, m_context->m_memory_allocation_callback);
	}

	DescriptorPool::DescriptorPool(std::shared_ptr<RHI::VulkanContext> vulkan_context, 
		const std::vector<VkDescriptorPoolSize>& pool_size, uint32_t limit_max_sets) :
		m_context{ std::move(vulkan_context) }
	{
		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
			.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
			.maxSets = limit_max_sets,
			.poolSizeCount = static_cast<uint32_t>(pool_size.size()),
			.pPoolSizes = pool_size.data()
		};
		if (vkCreateDescriptorPool(
			m_context->m_device,
			&descriptorPoolCreateInfo,
			m_context->m_memory_allocation_callback,
			&m_descriptor_pool) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Descriptor Pool!");
	}

	DescriptorPool::~DescriptorPool()
	{
		vkDestroyDescriptorPool(m_context->m_device, m_descriptor_pool, m_context->m_memory_allocation_callback);
	}

	std::shared_ptr<DescriptorSet> DescriptorPool::
		AllocateDescriptorSet(std::shared_ptr<DescriptorSetLayout> descriptor_set_layout)
	{
		return std::make_shared<DescriptorSet>(shared_from_this(), descriptor_set_layout);
	}

	DescriptorSet::DescriptorSet(std::shared_ptr<DescriptorPool> parent, std::shared_ptr<DescriptorSetLayout> descriptor_set_layout)
		:m_parent{ std::move(parent) }, m_descriptor_set_layout{ std::move(descriptor_set_layout) }
	{
		VkDescriptorSetLayout layout = *m_descriptor_set_layout;
		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = *m_parent,
			.descriptorSetCount = 1,
			.pSetLayouts = &layout
		};

		if (vkAllocateDescriptorSets(
			m_parent->m_context->m_device,
			&descriptorSetAllocateInfo,
			&m_descriptor_set) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Descriptor Sets!");
	}

	DescriptorSet::~DescriptorSet()
	{
		vkFreeDescriptorSets(m_parent->m_context->m_device, *m_parent, 1, &m_descriptor_set);
	}

	void DescriptorSet::WriteBuffer(VkDescriptorType buffer_type, uint32_t buffer_binding, std::shared_ptr<VMA::Buffer> data)
	{
		VkDescriptorBufferInfo descriptorBufferInfo
		{
			.buffer = *data,
			.offset = 0,
			.range = VK_WHOLE_SIZE // data->Size()
		};

		VkWriteDescriptorSet writeDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_descriptor_set,
			.dstBinding = buffer_binding,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = buffer_type,
			.pImageInfo = nullptr,
			.pBufferInfo = &descriptorBufferInfo,
			.pTexelBufferView = nullptr
		};

		vkUpdateDescriptorSets(m_parent->m_context->m_device, 1, &writeDescriptorSet, 0, nullptr);
	}

	void DescriptorSet::WriteImage(VkDescriptorType image_type, uint32_t image_binding, std::shared_ptr<VMA::Image> data)
	{
		assert(data->GetImageSampler() != VK_NULL_HANDLE && "Cannot write the image without a sampler!");
		VkDescriptorImageInfo descriptorImageInfo
		{
			.sampler = data->GetImageSampler(),
			.imageView = data->GetImageView(),
			.imageLayout = data->GetImageLayout()
		};

		VkWriteDescriptorSet writeDescriptorSet
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = m_descriptor_set,
			.dstBinding = image_binding,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = image_type,
			.pImageInfo = &descriptorImageInfo,
			.pBufferInfo = nullptr,
			.pTexelBufferView = nullptr
		};

		vkUpdateDescriptorSets(m_parent->m_context->m_device, 1, &writeDescriptorSet, 0, nullptr);
	}

	void DescriptorSet::WriteImages(VkDescriptorType image_type, std::vector<std::shared_ptr<VMA::Image>> data, uint32_t offset/* = 0*/)
	{
		std::vector<VkDescriptorImageInfo> descriptorImageInfos(data.size());
		std::vector<VkWriteDescriptorSet> writeDescriptorSets(data.size());

		for (uint32_t i = 0; i < data.size(); ++i)
		{
			assert(data[i]->GetImageSampler() != VK_NULL_HANDLE && "Cannot write the image without a sampler!");

			descriptorImageInfos[i] = VkDescriptorImageInfo
			{
				.sampler = data[i]->GetImageSampler(),
				.imageView = data[i]->GetImageView(),
				.imageLayout = data[i]->GetImageLayout()
			};

			writeDescriptorSets[i] = VkWriteDescriptorSet
			{
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = m_descriptor_set,
				.dstBinding = i + offset,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = image_type,
				.pImageInfo = &descriptorImageInfos[i],
				.pBufferInfo = nullptr,
				.pTexelBufferView = nullptr
			};
		}
		vkUpdateDescriptorSets(m_parent->m_context->m_device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
	}

	Sampler::Sampler(std::shared_ptr<RHI::VulkanContext> vulkan_context,
		VkSamplerAddressMode address_mode,
		VkBorderColor border_color/* = VK_BORDER_COLOR_INT_OPAQUE_BLACK*/,
		VkCompareOp compare_mode/* = VK_COMPARE_OP_NEVER*/,
		bool anisotropy_enable/* = true*/):
		m_context {std::move(vulkan_context)}
	{
		VkSamplerCreateInfo samplerCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,

			.magFilter = VK_FILTER_LINEAR,
			.minFilter = VK_FILTER_LINEAR,

			.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,

			.addressModeU = address_mode,
			.addressModeV = address_mode,
			.addressModeW = address_mode,

			.mipLodBias = 0.0,

			.anisotropyEnable = anisotropy_enable ? VK_TRUE : VK_FALSE,
			.maxAnisotropy = m_context->m_physical_device_properties.limits.maxSamplerAnisotropy,

			.compareEnable = compare_mode ? VK_TRUE : VK_FALSE,
			.compareOp = compare_mode,
			
			.minLod = 0.0,
			.maxLod = 0.0,

			.borderColor = border_color,

			.unnormalizedCoordinates = VK_FALSE
		};

		if (vkCreateSampler(
			m_context->m_device,
			&samplerCreateInfo,
			m_context->m_memory_allocation_callback,
			&m_sampler) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Sampler!");
	}

	Sampler::~Sampler()
	{
		vkDestroySampler(m_context->m_device, m_sampler, m_context->m_memory_allocation_callback);
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

	Semaphore::~Semaphore()
	{
		vkDestroySemaphore(m_context->m_device, m_semaphore, m_context->m_memory_allocation_callback);
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

	Fence::~Fence()
	{ 
		vkDestroyFence(m_context->m_device, m_fence, m_context->m_memory_allocation_callback); 
	}

	void Fence::Wait(bool reset/* = false*/, uint64_t timeout/* = std::numeric_limits<uint64_t>::max()*/)
	{
		vkWaitForFences(m_context->m_device, 1, &m_fence, VK_TRUE, timeout);
		if (reset) Reset();
	}

	void Fence::Reset()
	{
		vkResetFences(m_context->m_device, 1, &m_fence);
	}

}} // namespace Albedo::RHI