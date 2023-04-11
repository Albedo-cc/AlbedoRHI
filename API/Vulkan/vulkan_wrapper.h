#pragma once

#include "vulkan_memory.h"

namespace Albedo {
namespace RHI
{
	class VulkanContext;

	// Wrapper List
	class RenderPass;			// Abstract Class
	class GraphicsPipeline;	// Abstract Class

	class CommandPool;		// Factory
	class CommandBuffer;
	class CommandBufferReset;
	class CommandBufferOneTime;

	class DescriptorPool;		// Factory
	class DescriptorSet;
	class DescriptorBinding;

	class Sampler;

	class Semaphore;	// Add order between queue operations (same queue or different queues) on the GPU
	class Fence;				// order the execution on the CPU

	// Implementation
	class CommandPool : public std::enable_shared_from_this<CommandPool>
	{
		friend class CommandBufferReset;
		friend class CommandBufferOneTime;
	public:
		std::shared_ptr<CommandBuffer> AllocateCommandBuffer(VkCommandBufferLevel level);
		operator VkCommandPool() { return m_command_pool; }

	public:
		CommandPool() = delete;
		CommandPool(std::shared_ptr<VulkanContext> vulkan_context,
									uint32_t submit_queue_family_index,
									VkCommandPoolCreateFlags command_pool_flags);
		~CommandPool();

	private:
		std::shared_ptr<VulkanContext> m_context;
		VkQueue m_submit_queue_family;

		VkCommandPool m_command_pool = VK_NULL_HANDLE;
		VkCommandPoolCreateFlags m_command_pool_flags;
	};

	class CommandBuffer
	{
		friend class CommandPool;
	public:
		virtual void Begin(VkCommandBufferInheritanceInfo* inheritanceInfo = nullptr) = 0;
		virtual void End() = 0;
		virtual void Submit(bool wait_queue_idle = false,
			VkFence fence = VK_NULL_HANDLE,
			std::vector<VkSemaphore> wait_semaphores = {},
			std::vector<VkSemaphore> signal_semaphores = {},
			VkPipelineStageFlags which_pipeline_stages_to_wait = 0,
			uint32_t target_queue_index = 0) = 0;

		VkCommandBufferLevel GetLevel() const { return m_level; }
		bool IsRecording() const { return m_is_recording; }
		operator VkCommandBuffer() { return m_command_buffer; }

	public:
		CommandBuffer() = delete;
		CommandBuffer(std::shared_ptr<CommandPool> parent, VkCommandBufferLevel level);
		virtual ~CommandBuffer() {};

	protected:
		std::shared_ptr<CommandPool> m_parent;
		VkCommandBuffer m_command_buffer = VK_NULL_HANDLE;
		VkCommandBufferLevel m_level;
		bool m_is_recording = false;

	}; // class CommandBuffer

	class CommandBufferReset :
		public CommandBuffer
	{
	public:
		virtual void Begin(VkCommandBufferInheritanceInfo* inheritanceInfo = nullptr) override;
		virtual void End() override;
		virtual void Submit(bool wait_queue_idle = false,
			VkFence fence = VK_NULL_HANDLE,
			std::vector<VkSemaphore> wait_semaphores = {},
			std::vector<VkSemaphore> signal_semaphores = {},
			VkPipelineStageFlags which_pipeline_stages_to_wait = 0,
			uint32_t target_queue_index = 0) override;

	public:
		CommandBufferReset() = delete;
		CommandBufferReset(std::shared_ptr<CommandPool> parent, VkCommandBufferLevel level) :
			CommandBuffer{ parent, level } {}
		virtual ~CommandBufferReset() override {};
	};

	class CommandBufferOneTime :
		public CommandBuffer
	{
	public:
		virtual void Begin(VkCommandBufferInheritanceInfo* inheritanceInfo = nullptr) override;
		virtual void End() override;
		virtual void Submit(bool wait_queue_idle = false,
			VkFence fence = VK_NULL_HANDLE,
			std::vector<VkSemaphore> wait_semaphores = {},
			std::vector<VkSemaphore> signal_semaphores = {},
			VkPipelineStageFlags which_pipeline_stages_to_wait = 0,
			uint32_t target_queue_index = 0) override;

	public:
		CommandBufferOneTime() = delete;
		CommandBufferOneTime(std::shared_ptr<CommandPool> parent, VkCommandBufferLevel level) :
			CommandBuffer{ parent, level } {}
		virtual ~CommandBufferOneTime() override {};
	};

	class RenderPass
	{
	public:
		// All derived classes have to call initialize() before beginning the render pass.
		virtual void Initialize();

		virtual void Begin(std::shared_ptr<CommandBuffer> command_buffer);
		const std::vector<GraphicsPipeline*>& GetGraphicsPipelines() {return m_graphics_pipelines; } // Call pipeline.Bind() first, and then callvkCmdDraw
		virtual void End(std::shared_ptr<CommandBuffer> command_buffer);

		void SetCurrentFrameBufferIndex(size_t index) { m_current_frame_buffer_index = index; }
		operator VkRenderPass() { return m_render_pass; }

	protected:
		virtual std::vector<VkSubpassDependency> set_subpass_dependencies() = 0;
	
		virtual void create_attachments() = 0;
		virtual void create_subpasses() = 0;	
		virtual void create_framebuffers() = 0;
		virtual void create_pipelines() = 0;

		virtual std::vector<VkClearValue>	set_attachment_clear_colors() = 0; // Note that the order of clearValues should be identical to the order of your attachments.
		virtual VkRect2D								set_render_area()							/*[Optional]*/;

	protected:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkRenderPass m_render_pass = VK_NULL_HANDLE;
		size_t m_current_frame_buffer_index = 0;

		// You may use some enum classes to manage the following descriptiions
		std::vector<VkFramebuffer> m_framebuffers;
		std::vector<VkAttachmentDescription> m_attachment_descriptions;
		std::vector<VkAttachmentReference> m_attachment_references;

		std::vector<VkSubpassDescription> m_subpass_descriptions;
		std::vector<GraphicsPipeline*> m_graphics_pipelines;

	public:
		RenderPass() = delete;
		RenderPass(std::shared_ptr<RHI::VulkanContext> vulkan_context);
		virtual ~RenderPass() noexcept;
	};


	class DescriptorBinding
	{
	public:
		uint32_t set;
		uint32_t binding;
		VkDescriptorType type;
		uint32_t count;
		VkShaderStageFlags stages;

		operator VkDescriptorSetLayoutBinding() const
		{
			return VkDescriptorSetLayoutBinding
			{
				.binding = binding,
				.descriptorType = type,
				.descriptorCount = count,
				.stageFlags = stages,
				.pImmutableSamplers = nullptr
			};
		}
		
		bool operator==(const DescriptorBinding& another) const
		{
			return set == another.set && binding == another.binding; // Type? (Shader Union)
		}

		struct Hash
		{
			uint64_t operator()(const DescriptorBinding& binding) const
			{
				uint64_t hash{ binding.set };
				return (hash << 32) | binding.binding;
			}
		};
	};

	class GraphicsPipeline
	{
	public:
		// All derived classes have to call initialize() before beginning the render pass.
		virtual void Initialize();

		virtual void Bind(std::shared_ptr<RHI::CommandBuffer> command_buffer) = 0; // vkCmdDraw ...

		VkPipelineLayout& GetPipelineLayout() { return m_pipeline_layout; }
		VkDescriptorSetLayout& GetDescriptorSetLayout(size_t index) { assert(index < m_descriptor_set_layouts.size()); return m_descriptor_set_layouts[index]; }
		operator VkPipeline() { return m_pipeline; }

	protected:
		virtual std::vector<VkDescriptorSetLayout>  						prepare_descriptor_layouts()			/* [Optional]: Layout will be reflected automatically*/;
		virtual std::vector<VkPushConstantRange>						prepare_push_constant_state()		/* [Optional]: Layout will be reflected automatically*/;

		enum ShaderTypes{vertex_shader, fragment_shader, MAX_SHADER_COUNT};
		virtual std::array<std::string, MAX_SHADER_COUNT>		prepare_shader_files()						= 0; // Use ShaderTypes enum
		virtual VkPipelineVertexInputStateCreateInfo						prepare_vertex_input_state()			/* [Optional]: Layout will be reflected automatically*/;
		virtual VkPipelineTessellationStateCreateInfo					prepare_tessellation_state()			/* [Optional]*/;
		virtual VkPipelineInputAssemblyStateCreateInfo				prepare_input_assembly_state()	= 0;
		virtual VkPipelineViewportStateCreateInfo							prepare_viewport_state()				= 0; // m_viewports & m_scissors
		virtual VkPipelineRasterizationStateCreateInfo					prepare_rasterization_state()			= 0;
		virtual VkPipelineMultisampleStateCreateInfo					prepare_multisampling_state()		/* [Optional]*/;
		virtual VkPipelineDepthStencilStateCreateInfo					prepare_depth_stencil_state()		/* [Optional]*/;
		virtual VkPipelineColorBlendStateCreateInfo						prepare_color_blend_state()			= 0;
		virtual VkPipelineDynamicStateCreateInfo							prepare_dynamic_state()				/* [Optional]*/;

	public:
		GraphicsPipeline() = delete;
		GraphicsPipeline(std::shared_ptr<RHI::VulkanContext> vulkan_context, 
										VkRenderPass owner, uint32_t subpass_bind_point, 
										VkPipeline base_pipeline = VK_NULL_HANDLE, int32_t base_pipeline_index = -1);
		virtual ~GraphicsPipeline() noexcept;

	protected:
		VkShaderModule	create_shader_module(std::string_view shader_file, std::vector<char>& buffer);

	protected:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkPipeline								m_pipeline								= VK_NULL_HANDLE;
		VkPipelineLayout					m_pipeline_layout				= VK_NULL_HANDLE;
		VkPipelineCache					m_pipeline_cache					= VK_NULL_HANDLE;
		VkPipeline								m_base_pipeline					= VK_NULL_HANDLE;
		int32_t									m_base_pipeline_index;

		VkRenderPass						m_owner;
		uint32_t									m_subpass_bind_point;
		std::vector<VkViewport>		m_viewports;
		std::vector<VkRect2D>		m_scissors;
		std::vector<VkDescriptorSetLayout> m_descriptor_set_layouts;

	private:
		void deduce_pipeline_states_from_shaders(
			std::vector<char>& vertex_shader, 
			std::vector<char>& fragment_shader,
			std::vector<VkDescriptorSetLayout>* descriptor_set_layouts, 
			std::vector<VkPushConstantRange>* push_constants);
	};

	class DescriptorSet
	{
		friend class DescriptorPool;
	public:
		void WriteBuffer(VkDescriptorType buffer_type, uint32_t buffer_binding, std::shared_ptr<VMA::Buffer> data);
		void WriteImage(VkDescriptorType image_type, uint32_t image_binding, std::shared_ptr<VMA::Image> data);

		VkDescriptorSetLayout& GetDescriptorSetLayout() { return descriptor_set_layout; }

	public:
		DescriptorSet() = delete;
		DescriptorSet(std::shared_ptr<DescriptorPool> parent) :m_parent{ std::move(parent) } {}
		~DescriptorSet();
		operator VkDescriptorSet() { return m_descriptor_set; }

	private:
		std::shared_ptr<DescriptorPool> m_parent;
		VkDescriptorSet m_descriptor_set = VK_NULL_HANDLE;
		VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
	};

	class DescriptorPool : public std::enable_shared_from_this<DescriptorPool>
	{
		friend class DescriptorSet;
	public:
		std::shared_ptr<DescriptorSet> AllocateDescriptorSet(std::vector<VkDescriptorSetLayoutBinding> descriptor_bindings);

	public:
		DescriptorPool() = delete;
		DescriptorPool(std::shared_ptr<RHI::VulkanContext> vulkan_context, const std::vector<VkDescriptorPoolSize>& pool_size, uint32_t limit_max_sets);
		~DescriptorPool();
		operator VkDescriptorPool() { return m_descriptor_pool; }

	private:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkDescriptorPool	 m_descriptor_pool = VK_NULL_HANDLE;
	};

	class Sampler
	{
	public:
		Sampler() = delete;
		Sampler(std::shared_ptr<RHI::VulkanContext> vulkan_context,
						VkSamplerAddressMode address_mode,
						VkBorderColor border_color = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
						VkCompareOp compare_mode = VK_COMPARE_OP_NEVER,
						bool anisotropy_enable = true);
		~Sampler();
		operator VkSampler() { return m_sampler; }

	private:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkSampler m_sampler;
	};

	class Semaphore
	{
	public:
		Semaphore() = delete;
		Semaphore(std::shared_ptr<RHI::VulkanContext> vulkan_context, VkSemaphoreCreateFlags flags);
		~Semaphore();
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
		void Wait(bool reset = false, uint64_t timeout = std::numeric_limits<uint64_t>::max());
		void Reset();

		Fence() = delete;
		Fence(std::shared_ptr<RHI::VulkanContext> vulkan_context, VkFenceCreateFlags flags);
		~Fence();
		Fence(const Fence&) = delete;
		Fence(Fence&& rvalue) noexcept;
		operator VkFence() { return m_fence; }

	private:
		std::shared_ptr<RHI::VulkanContext> m_context;
		VkFence m_fence = VK_NULL_HANDLE;
	};

}} // namespace Albedo::RHI