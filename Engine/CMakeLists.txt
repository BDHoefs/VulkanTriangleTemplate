find_package(Vulkan REQUIRED)

set(SOURCES
	Application.cpp
	Application.hpp
	Renderer.cpp
	Renderer.hpp
	VulkanInitializers.cpp
	VulkanInitializers.hpp
	Mesh.cpp
	Mesh.hpp
	VulkanTypes.hpp
	Camera.hpp
	Transform.hpp
)

add_library(Engine ${SOURCES})

target_include_directories(Engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Engine SDL2-static vk-bootstrap Vulkan::Vulkan)