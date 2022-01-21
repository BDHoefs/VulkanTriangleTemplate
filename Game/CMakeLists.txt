set(SOURCES
	Main.cpp
)

add_executable(VulkanTriangle ${SOURCES})

target_link_libraries(VulkanTriangle Engine)

add_custom_command(
	TARGET VulkanTriangle
	POST_BUILD
	COMMAND
	${CMAKE_COMMAND} -E copy_directory
	${CMAKE_SOURCE_DIR}/Resources ${CMAKE_CURRENT_BINARY_DIR}
	DEPENDS ${CMAKE_CURRENT_BINARY_DIR}
)