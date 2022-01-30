#pragma once
#include <stdexcept>

#include <ThirdParty/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <Renderer/VulkanTypes.hpp>

VkImageCreateInfo image_create_info(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);

VkImageViewCreateInfo image_view_create_info(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);

VkPipelineDepthStencilStateCreateInfo depth_stencil_create_info(bool depthTest, bool depthWrite, VkCompareOp compareOp);

AllocatedBuffer create_buffer(std::shared_ptr<VmaAllocator> allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
