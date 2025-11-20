cmake_minimum_required(VERSION 3.10)
project(VulkanLearning)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Vulkan
find_package(Vulkan REQUIRED)

# Add GLFW (submodule in ext/glfw)
add_subdirectory(ext/glfw)

add_subdirectory(ext/glm)

add_subdirectory(ext/vk-bootstrap)

add_subdirectory(ext/fmt)

#both vma and glm and header only libs so we only need the include path
add_library(vma INTERFACE)
target_include_directories(vma INTERFACE ext/vma)

add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE ext/tinygltf)

# Collect all main.cpp files inside src/
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS src/main*.cpp)

file(GLOB LIB_SOURCES src/lib/*.cpp src/lib/*.h)

# Create an executable for each main.cpp
foreach(source_file ${SRC_FILES})
    # Extract filename without extension to use as target name
    get_filename_component(exec_name ${source_file} NAME_WE)

    add_executable(${exec_name} ${source_file})

    target_include_directories(${exec_name} PRIVATE ${Vulkan_INCLUDE_DIRS}  ${CMAKE_SOURCE_DIR}/ext)

    target_sources(${exec_name} PRIVATE ${LIB_SOURCES})

    target_link_libraries(${exec_name} PRIVATE ${Vulkan_LIBRARIES} glfw glm vk-bootstrap::vk-bootstrap fmt::fmt tinygltf)

endforeach()
