cmake_minimum_required(VERSION 3.15)

project (VulkanTriangle)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(ThirdParty)
add_subdirectory(Engine)
add_subdirectory(Game)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl2)