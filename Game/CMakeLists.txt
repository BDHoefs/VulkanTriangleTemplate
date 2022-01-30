set(SOURCES
	Main.cpp
)

add_executable(Frontier ${SOURCES})

target_link_libraries(Frontier Engine)

add_custom_command(
	TARGET Frontier
	POST_BUILD
	COMMAND
	${CMAKE_COMMAND} -E copy_directory
	${CMAKE_SOURCE_DIR}/Resources ${CMAKE_CURRENT_BINARY_DIR}
	DEPENDS ${CMAKE_CURRENT_BINARY_DIR}
)