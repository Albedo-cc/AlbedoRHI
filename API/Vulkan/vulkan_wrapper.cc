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
		vkDestroyPipelineLayout(m_context->m_device, m_pipeline_layout, m_context->m_memory_allocation_callback);
		vkDestroyPipeline(m_context->m_device, m_pipeline, m_context->m_memory_allocation_callback);
	}

	void GraphicsPipeline::Initialize()
	{
		// --------------------------------------------------------------------------------------------------------------------------------//
		// 1. Create Shader Stages & Deduce Pipeline Layout
		// --------------------------------------------------------------------------------------------------------------------------------//
		// Descriptor Set Layouts & Push Constants
		auto descriptor_set_layouts = prepare_descriptor_layouts();
		auto push_constant_state = prepare_push_constant_state();
		auto descriptor_bindings = descriptor_set_layouts.empty() ? new std::vector<DescriptorBinding>() : nullptr;
		auto push_constant_ranges = push_constant_state.empty() ? new std::vector<PushConstantRange>() : nullptr;

		// Shaders
		std::vector<VkPipelineShaderStageCreateInfo> shaderInfos(MAX_SHADER_COUNT);
		auto shaders = prepare_shader_files();
		// Vertex Shader
		shaderInfos[vertex_shader].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderInfos[vertex_shader].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderInfos[vertex_shader].module = create_shader_module(shaders[vertex_shader], VK_SHADER_STAGE_VERTEX_BIT, descriptor_bindings, nullptr);
		shaderInfos[vertex_shader].pName = "main";
		// Fragment Shader
		shaderInfos[fragment_shader].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderInfos[fragment_shader].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderInfos[fragment_shader].module = create_shader_module(shaders[fragment_shader], VK_SHADER_STAGE_FRAGMENT_BIT, descriptor_bindings, nullptr);
		shaderInfos[fragment_shader].pName = "main";

		// Deduce Pipeline Layout
		if (descriptor_bindings && !descriptor_bindings->empty())
		{
			if (descriptor_bindings->size() > 1)
				std::sort(descriptor_bindings->begin(), descriptor_bindings->end(),
					[](const DescriptorBinding& next, const DescriptorBinding& prev)->bool
					{
						if (next.set == prev.set) return next.binding < prev.binding; // Descending Binding Index
						else return next.set < prev.set; // Descending Set Index
					});

			size_t max_set = descriptor_bindings->front().set + 1;
			descriptor_set_layouts.resize(max_set);
			std::vector<std::vector<VkDescriptorSetLayoutBinding>> descriptorSets(max_set);
			
			for (auto& currentBinding : *descriptor_bindings)
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


			// Debug)))))>>>>>>>>>>>>>>
			for (auto& set : descriptorSets)
			{
				static int cnt = 0;
				log::debug("\nCurrent Set {}", cnt++);
				for (auto& binding : set)
				{
					log::info("flags {}, count {}, type {}", binding.stageFlags, binding.descriptorCount, binding.descriptorType);
				}
			}

			// Create Descriptor Set Layouts
			for (size_t current_set = 0; current_set < max_set; ++current_set)
			{
				auto& descriptor_set_layout = descriptor_set_layouts[current_set];
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
		} // End deduce descriptor set layouts

		if (push_constant_ranges && !push_constant_ranges->empty()) // && >0
		{
			//push_constant_state.resize(INTERSECTION)
		}
		// --------------------------------------------------------------------------------------------------------------------------------//
		


		// --------------------------------------------------------------------------------------------------------------------------------//
		// 2. Create Pipeline Layout
		// --------------------------------------------------------------------------------------------------------------------------------//
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
			.setLayoutCount = static_cast<uint32_t>(descriptor_set_layouts.size()),
			.pSetLayouts = descriptor_set_layouts.data(),
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
		// Deducers
		delete descriptor_bindings;
		delete push_constant_ranges;
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

	VkShaderModule GraphicsPipeline::create_shader_module(
		std::string_view shader_file, 
		VkShaderStageFlags shader_stage, 
		std::vector<DescriptorBinding>* descriptor_set_layout_bindings,
		std::vector<PushConstantRange>* push_constants)
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
			throw std::runtime_error(std::format("Failed to create shader module {}!", shader_file));

		if (descriptor_set_layout_bindings || push_constants)
		{
			SpvReflectShaderModule spvContext;
			SpvReflectResult result = spvReflectCreateShaderModule(file_size, buffer.data(), &spvContext);
			assert(result == SPV_REFLECT_RESULT_SUCCESS);
			uint32_t count = 0;

			if (descriptor_set_layout_bindings)
			{
				if (spvReflectEnumerateDescriptorSets(&spvContext, &count, NULL) != SPV_REFLECT_RESULT_SUCCESS)
					throw std::runtime_error(std::format("Failed to reflect descriptor sets of shader {}!", shader_file));

				SpvReflectDescriptorSet** pDescriptorSets = new SpvReflectDescriptorSet * [count];
				spvReflectEnumerateDescriptorSets(&spvContext, &count, pDescriptorSets);

				for (uint32_t i = 0; i < count; ++i)
				{
					DescriptorBinding descriptorBinding{ .set = pDescriptorSets[i]->set, .stages = shader_stage };
					
					log::debug("Set {} Bindings {}", pDescriptorSets[i]->set, pDescriptorSets[i]->binding_count);
					auto& bindings = pDescriptorSets[i]->bindings;
					for (uint32_t j = 0; j < pDescriptorSets[i]->binding_count; ++j)
					{
						descriptorBinding.binding = bindings[j]->binding;
						descriptorBinding.count = bindings[j]->count;
						descriptorBinding.type = static_cast<VkDescriptorType>(bindings[j]->descriptor_type);
						log::debug("binding {}, name {}, count {}", bindings[j]->binding, bindings[j]->name, bindings[j]->count);
					}
					descriptor_set_layout_bindings->emplace_back(descriptorBinding);
				}
				delete[] pDescriptorSets;
			}

			if (push_constants)
			{
				if (spvReflectEnumeratePushConstants(&spvContext, &count, NULL) != SPV_REFLECT_RESULT_SUCCESS)
					throw std::runtime_error(std::format("Failed to reflect push constants of shader {}!", shader_file));

				SpvReflectBlockVariable** pPushConstants = new SpvReflectBlockVariable * [count];
				spvReflectEnumeratePushConstants(&spvContext, &count, NULL);

				for (uint32_t i = 0; i < count; ++i)
				{
					log::debug("Push Constant: offset {}, size {}", pPushConstants[i]->offset, pPushConstants[i]->size);
					push_constants->emplace_back(
						PushConstantRange
						{
							.stages = shader_stage,
							.offset = pPushConstants[i]->offset,
							.size = pPushConstants[i]->size
						});
				}
				delete[] pPushConstants;
			}

			spvReflectDestroyShaderModule(&spvContext);
		}

		return shader_module;
	}

	CommandBuffer::CommandBuffer(std::shared_ptr<CommandPool> parent, VkCommandBufferLevel level) :
		m_parent{ std::move(parent) }, m_level{ level }
	{
		
	}

	void CommandBufferReset::Begin(VkCommandBufferInheritanceInfo* inheritanceInfo/* = nullptr*/)
	{
		assert(!IsRecording() && "You cannot Begin() a recording Vulkan Command Buffer!");

		vkResetCommandBuffer(m_command_buffer, 0);

		VkCommandBufferBeginInfo commandBufferBeginInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = 0,
			.pInheritanceInfo = inheritanceInfo
		};
		if (vkBeginCommandBuffer(m_command_buffer, &commandBufferBeginInfo) != VK_SUCCESS)
			throw std::runtime_error("Failed to begin the Vulkan Command Buffer!");

		m_is_recording = true;
	}

	void CommandBufferReset::End()
	{
		assert(IsRecording() && "You cannot End() an idle Vulkan Command Buffer!");
		if (vkEndCommandBuffer(m_command_buffer) != VK_SUCCESS)
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
			.pCommandBuffers = &m_command_buffer,
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
			.flags = 0,
			.pInheritanceInfo = inheritanceInfo
		};
		if (vkBeginCommandBuffer(m_command_buffer, &commandBufferBeginInfo) != VK_SUCCESS)
			throw std::runtime_error("Failed to begin the Vulkan Command Buffer!");

		m_is_recording = true;
	}

	void CommandBufferOneTime::End()
	{
		assert(IsRecording() && "You cannot End() an idle Vulkan Command Buffer!");
		if (vkEndCommandBuffer(m_command_buffer) != VK_SUCCESS)
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
			.pCommandBuffers = &m_command_buffer,
			.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size()),
			.pSignalSemaphores = signal_semaphores.data()
		};

		auto& submit_queue = m_parent->m_submit_queue_family;
		if (vkQueueSubmit(submit_queue, 1, &submitInfo, fence) != VK_SUCCESS)
			throw std::runtime_error("Failed to submit the Vulkan Command Buffer!");
		if (wait_queue_idle) vkQueueWaitIdle(submit_queue);

		vkFreeCommandBuffers(m_parent->m_context->m_device, *m_parent, 1, &m_command_buffer);
	}

	CommandPool::CommandPool(
		std::shared_ptr<VulkanContext> vulkan_context,
		uint32_t submit_queue_family_index,
		VkCommandPoolCreateFlags command_pool_flags) :
		m_context{ std::move(vulkan_context) },
		m_submit_queue_family{ m_context->GetQueue(submit_queue_family_index) },
		m_command_pool_flags{ command_pool_flags }
	{
		VkCommandPoolCreateInfo commandPoolCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.flags = command_pool_flags,
			.queueFamilyIndex = submit_queue_family_index
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
			&commandbuffer->m_command_buffer) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Command Buffer!");

		return commandbuffer;
	}

	DescriptorPool::DescriptorPool(std::shared_ptr<RHI::VulkanContext> vulkan_context, 
		const std::vector<VkDescriptorPoolSize>& pool_size, uint32_t limit_max_sets) :
		m_context{ std::move(vulkan_context) }
	{
		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
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

	std::shared_ptr<DescriptorSet> DescriptorPool::AllocateDescriptorSet(std::vector<VkDescriptorSetLayoutBinding> descriptor_bindings)
	{
		auto descriptor_set = std::make_shared<DescriptorSet>(shared_from_this());

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
			&(descriptor_set->descriptor_set_layout)) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Descriptor Set Layout!");
		
		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo
		{
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = m_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &(descriptor_set->descriptor_set_layout)
		};

		if (vkAllocateDescriptorSets(
			m_context->m_device,
			&descriptorSetAllocateInfo,
			&(descriptor_set->m_descriptor_set)) != VK_SUCCESS)
			throw std::runtime_error("Failed to create the Vulkan Descriptor Sets!");

		return descriptor_set;
	}

	DescriptorSet::~DescriptorSet()
	{
		vkDestroyDescriptorSetLayout(m_parent->m_context->m_device, descriptor_set_layout, m_parent->m_context->m_memory_allocation_callback);
	}

	void DescriptorSet::WriteBuffer(VkDescriptorType buffer_type, uint32_t buffer_binding, std::shared_ptr<VMA::Buffer> data)
	{
		VkDescriptorBufferInfo descriptorBufferInfo
		{
			.buffer = *data,
			.offset = 0,
			.range = data->Size()
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