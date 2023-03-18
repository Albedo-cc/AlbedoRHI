#include "vulkan_wrapper.h"

namespace Albedo {
namespace RHI
{

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
			m_context->m_memory_allocator,
			&shader_module) != VK_SUCCESS)
			throw std::runtime_error("Failed to create shader module!");

		return shader_module;
	}

}} // namespace Albedo::RHI