find_package(Vulkan REQUIRED)

set(SOURCES
	Application.cpp
	Application.hpp
	Camera.hpp
	Mesh.cpp
	Mesh.hpp
	Transform.hpp

	ECS/ECS.cpp
	ECS/ECS.hpp
)

add_subdirectory(Renderer)

add_library(Engine ${SOURCES})

target_include_directories(Engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(Engine SDL2-static vk-bootstrap Vulkan::Vulkan)