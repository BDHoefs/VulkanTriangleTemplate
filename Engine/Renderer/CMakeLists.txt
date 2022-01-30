set(RENDERER_SOURCES
	Renderer/Renderer.cpp
	Renderer/Renderer.hpp
	Renderer/VulkanInitializers.cpp
	Renderer/VulkanInitializers.hpp
	Renderer/VulkanTypes.hpp
)

set(SOURCES ${SOURCES} ${RENDERER_SOURCES} PARENT_SCOPE)