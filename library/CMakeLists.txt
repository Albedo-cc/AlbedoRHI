if (NOT TARGET AlbedoLog)
    message(STATUS "\nLoading Albedo Log (Albedo::log)")
    add_subdirectory(AlbedoLog)
endif()


# GLFW3
if (NOT TARGET glfw)
    message(STATUS "\nLoading GLFW3 (glfw)")
    option(GLFW_BUILD_EXAMPLES "Build the GLFW example programs" OFF)
    option(GLFW_BUILD_TESTS "Build the GLFW test programs" OFF)
    option(GLFW_BUILD_DOCS "Build the GLFW documentation" OFF)
    option(GLFW_INSTALL "Generate installation target" OFF)
    option(GLFW_VULKAN_STATIC "Assume the Vulkan loader is linked with the application" OFF)
    add_subdirectory(glfw-3.3.8)
endif()

# VMA & Vulkan
if(ALBEDO_RHI_API_VULKAN)
    if(NOT TARGET VulkanMemoryAllocator)
        message(STATUS "\nLoading VMA (VulkanMemoryAllocator)")
        add_subdirectory(VulkanMemoryAllocator-3.0.1)
    endif()
endif(ALBEDO_RHI_API_VULKAN)