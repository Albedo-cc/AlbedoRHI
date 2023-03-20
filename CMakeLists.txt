cmake_minimum_required(VERSION 3.21.0)

project("AlbedoRHI" 
    LANGUAGES CXX
    VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Explicitly require the standard
set(CMAKE_CXX_EXTENSIONS OFF) # Vendor-specific Extensions

# Choose backend
option(ALBEDO_RHI_API_VULKAN "Utilize Vulkan API" ON)

if (ALBEDO_RHI_API_VULKAN)
    message("[AlbedoRHI]: Utilized Vulkan API")

    file(GLOB ALBEDO_RHI_HEADER "API/Vulkan/*.h")
    file(GLOB ALBEDO_RHI_SOURCE "API/Vulkan/*.cc")

endif(ALBEDO_RHI_API_VULKAN)

add_subdirectory(library)

source_group("" FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/AlbedoRHI.hpp")
source_group(TREE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    FILES 
    ${ALBEDO_RHI_HEADER} 
    ${ALBEDO_RHI_SOURCE})
    
add_library(${PROJECT_NAME} 
    STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include/AlbedoRHI.hpp"
    ${ALBEDO_RHI_HEADER}
    ${ALBEDO_RHI_SOURCE})
add_library(Albedo::RHI ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} INTERFACE include)

find_package(Vulkan REQUIRED)
target_include_directories(${PROJECT_NAME} PUBLIC ${Vulkan_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC Albedo::log)
target_link_libraries(${PROJECT_NAME} PUBLIC glfw)
target_link_libraries(${PROJECT_NAME} PRIVATE VulkanMemoryAllocator)