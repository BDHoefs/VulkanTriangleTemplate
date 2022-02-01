#include "Renderer.hpp"

#include <Renderer/VulkanInitializers.hpp>

void Renderer::init()
{
    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);
    m_globalData->windowSize = { (uint32_t)w, (uint32_t)h };

    m_presentPass.set_global_context(m_globalData);

    init_instance();
    m_presentPass.create_swapchain(false);
    init_global_descriptor();
    init_render_passes();
    prepare_resources();
    create_command_buffers();

    m_initialized = true;
}

void Renderer::update()
{
    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);
    if (w != m_globalData->windowSize.width || h != m_globalData->windowSize.height) {
        recreate_swapchain();
        m_globalData->windowSize.width = w;
        m_globalData->windowSize.height = h;
    }
    if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) {
        return;
    }

    m_presentPass.update();

    m_globalData->frameIndex = (m_globalData->frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    m_globalData->frameNumber++;
}

void Renderer::exit()
{
    m_presentPass.exit();
}

void Renderer::recreate_swapchain()
{
    vkDeviceWaitIdle(m_globalData->device);

    vkDestroyCommandPool(m_globalData->device, m_globalData->commandPool, nullptr);

    for (auto framebuffer : m_presentPass.m_passData.framebuffers) {
        vkDestroyFramebuffer(m_globalData->device, framebuffer, nullptr);
    }

    m_presentPass.create_swapchain(true);
    m_presentPass.create_framebuffers(true);
    create_command_buffers();
}

std::shared_ptr<GlobalRenderContext> Renderer::get_global_data()
{
    return m_globalData;
}

void Renderer::init_instance()
{
    vkb::InstanceBuilder instanceBuilder;
    auto instanceRet = instanceBuilder.use_default_debug_messenger().request_validation_layers().build();
    if (!instanceRet) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    m_globalData->instance = instanceRet.value();
    m_cleanupQueue.push_front([this]() {
        vkb::destroy_instance(m_globalData->instance);
    });

    if (SDL_Vulkan_CreateSurface(m_window, m_globalData->instance, &m_surface) == SDL_FALSE) {
        throw std::runtime_error("Failed to create window surface");
    }
    m_cleanupQueue.push_front([this]() {
        vkDestroySurfaceKHR(m_globalData->instance, m_surface, nullptr);
    });

    vkb::PhysicalDeviceSelector selector(m_globalData->instance);
    auto physDeviceRet = selector.set_surface(m_surface).select();
    if (!physDeviceRet) {
        throw std::runtime_error("No suitable GPUs found");
    }

    vkb::DeviceBuilder deviceBuilder(physDeviceRet.value());
    auto deviceRet = deviceBuilder.build();
    if (!deviceRet) {
        throw std::runtime_error("Failed to create device");
    }

    m_globalData->device = deviceRet.value();
    m_cleanupQueue.push_front([this]() {
        vkb::destroy_device(m_globalData->device);
    });

    auto gq = m_globalData->device.get_queue(vkb::QueueType::graphics);
    auto pq = m_globalData->device.get_queue(vkb::QueueType::present);
    if (!gq.has_value() || !pq.has_value()) {
        throw std::runtime_error("Failed to find queues");
    }
    m_globalData->graphicsQueue = gq.value();
    m_globalData->presentQueue = pq.value();

    VmaAllocatorCreateInfo allocatorInfo {};
    allocatorInfo.physicalDevice = physDeviceRet.value();
    allocatorInfo.device = m_globalData->device;
    allocatorInfo.instance = m_globalData->instance;
    vmaCreateAllocator(&allocatorInfo, &m_globalData->allocator);
    m_cleanupQueue.push_front([this]() {
        vmaDestroyAllocator(m_globalData->allocator);
    });
}

void Renderer::init_global_descriptor()
{
    m_globalData->cameraData.resize(m_globalData->numSwapchainImages);
    m_globalData->sceneData.resize(m_globalData->numSwapchainImages);

    m_globalData->globalDescriptor.resize(m_globalData->numSwapchainImages);

    VkDescriptorSetLayoutBinding camBufferBinding {};
    camBufferBinding.binding = 0;
    camBufferBinding.descriptorCount = 1;
    camBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    camBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding sceneBufferBinding {};
    sceneBufferBinding.binding = 1;
    sceneBufferBinding.descriptorCount = 1;
    sceneBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sceneBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding bindings[] = { camBufferBinding, sceneBufferBinding };

    VkDescriptorSetLayoutCreateInfo globalSetInfo {};
    globalSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    globalSetInfo.flags = 0;
    globalSetInfo.bindingCount = 2;
    globalSetInfo.pBindings = bindings;

    vkCreateDescriptorSetLayout(m_globalData->device, &globalSetInfo, nullptr, &m_globalData->globalSetLayout);

    std::vector<VkDescriptorPoolSize> sizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 }
    };

    VkDescriptorPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = 0;
    poolInfo.maxSets = 10;
    poolInfo.poolSizeCount = (uint32_t)sizes.size();
    poolInfo.pPoolSizes = sizes.data();

    vkCreateDescriptorPool(m_globalData->device, &poolInfo, nullptr, &m_globalData->descriptorPool);

    m_cleanupQueue.push_front([this]() {
        vkDestroyDescriptorSetLayout(m_globalData->device, m_globalData->globalSetLayout, nullptr);
        vkDestroyDescriptorPool(m_globalData->device, m_globalData->descriptorPool, nullptr);
    });

    for (int i = 0; i < m_globalData->numSwapchainImages; i++) {
        m_globalData->cameraData[i] = create_buffer(&m_globalData->allocator,
            sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_globalData->sceneData[i] = create_buffer(&m_globalData->allocator,
            ECS::MAX_ENTITIES * sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_cleanupQueue.push_front([this, i]() {
            vmaDestroyBuffer(m_globalData->allocator, m_globalData->cameraData[i].buffer, m_globalData->cameraData[i].allocation);
            vmaDestroyBuffer(m_globalData->allocator, m_globalData->sceneData[i].buffer, m_globalData->sceneData[i].allocation);
        });

        VkDescriptorSetAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_globalData->descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_globalData->globalSetLayout;

        vkAllocateDescriptorSets(m_globalData->device, &allocInfo, &m_globalData->globalDescriptor[i]);

        VkDescriptorBufferInfo camBufInfo {};
        camBufInfo.buffer = m_globalData->cameraData[i].buffer;
        camBufInfo.offset = 0;
        camBufInfo.range = sizeof(GPUCameraData);

        VkWriteDescriptorSet camSetWrite {};
        camSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        camSetWrite.dstBinding = 0;
        camSetWrite.dstSet = m_globalData->globalDescriptor[i];
        camSetWrite.descriptorCount = 1;
        camSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        camSetWrite.pBufferInfo = &camBufInfo;

        VkDescriptorBufferInfo sceneBufInfo {};
        sceneBufInfo.buffer = m_globalData->sceneData[i].buffer;
        sceneBufInfo.offset = 0;
        sceneBufInfo.range = ECS::MAX_ENTITIES * sizeof(glm::mat4);

        VkWriteDescriptorSet sceneSetWrite {};
        sceneSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sceneSetWrite.dstBinding = 1;
        sceneSetWrite.dstSet = m_globalData->globalDescriptor[i];
        sceneSetWrite.descriptorCount = 1;
        sceneSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sceneSetWrite.pBufferInfo = &sceneBufInfo;

        VkWriteDescriptorSet setWrites[] = { camSetWrite, sceneSetWrite };

        vkUpdateDescriptorSets(m_globalData->device, 2, setWrites, 0, nullptr);
    }
}

void Renderer::init_render_passes()
{
    m_presentPass.init(m_globalData);
    m_cleanupQueue.push_front([this]() {
        m_presentPass.exit();
    });
}

void Renderer::prepare_resources()
{
    m_globalData->camera = std::make_pair<Camera, Transform>(Camera(), Transform(glm::vec3(0.0f, 0.0f, -2.0f)));
}

void Renderer::create_command_buffers()
{
    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_globalData->device.get_queue_index(vkb::QueueType::graphics).value();

    if (vkCreateCommandPool(m_globalData->device, &poolInfo, nullptr, &m_globalData->commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
    if (!m_initialized) {
        m_cleanupQueue.push_front([this]() {
            vkDestroyCommandPool(m_globalData->device, m_globalData->commandPool, nullptr);
        });
    }

    m_globalData->commandBuffers.resize(m_globalData->numSwapchainImages);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_globalData->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_globalData->commandBuffers.size();

    if (vkAllocateCommandBuffers(m_globalData->device, &allocInfo, m_globalData->commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    if (!m_initialized) {
        if (vkCreateCommandPool(m_globalData->device, &poolInfo, nullptr, &m_uploadData->commandPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create upload command buffer");
        }

        allocInfo.commandPool = m_uploadData->commandPool;
        allocInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(m_globalData->device, &allocInfo, &m_uploadData->commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate command buffers");
        }

        m_cleanupQueue.push_front([this]() {
            vkDestroyCommandPool(m_globalData->device, m_uploadData->commandPool, nullptr);
        });
    }
}

void Renderer::immediate_submit(std::function<void(VkCommandBuffer cmd)> func)
{
    VkCommandBuffer cmd = m_uploadData->commandBuffer;

    VkCommandBufferBeginInfo beginInfo {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin command buffer");
    }

    func(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("");
    }

    VkSubmitInfo submitInfo {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    if (vkQueueSubmit(m_globalData->graphicsQueue, 1, &submitInfo, m_uploadData->uploadFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit queue");
    }

    vkWaitForFences(m_globalData->device, 1, &m_uploadData->uploadFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_globalData->device, 1, &m_uploadData->uploadFence);

    vkResetCommandPool(m_globalData->device, m_uploadData->commandPool, 0);
}
