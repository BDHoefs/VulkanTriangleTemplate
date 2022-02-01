#pragma once
#include <memory>

#include <ThirdParty/vk_mem_alloc.h>

struct AllocatedBuffer {
    VkBuffer buffer {};
    VmaAllocation allocation {};

    VmaAllocator* allocator;
    bool inUse = false;
};

struct AllocatedImage {
    VkImage image {};
    VmaAllocation allocation {};

    std::shared_ptr<VmaAllocator> allocator = nullptr;
    bool inUse = false;
};