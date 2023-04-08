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

# SPIRV-Reflect
if (NOT TARGET spirv-reflect-static)
    message(STATUS "\nLoading SPIRV-Reflect (spirv-reflect-static)")
    OPTION(SPIRV_REFLECT_EXECUTABLE     "Build spirv-reflect executable" OFF)
    OPTION(SPIRV_REFLECT_EXAMPLES       "Build stripper examples" OFF)
    OPTION(SPIRV_REFLECT_STRIPPER       "Build stripper utility" OFF)
    OPTION(SPIRV_REFLECT_STATIC_LIB     "Build a SPIRV-Reflect static library" ON)
    OPTION(SPIRV_REFLECT_BUILD_TESTS    "Build the SPIRV-Reflect test suite" OFF)
    OPTION(SPIRV_REFLECT_ENABLE_ASSERTS "Enable asserts for debugging" OFF)
    add_subdirectory(SPIRV-Reflect-2023.4.8)
endif()