#define VMA_IMPLEMENTATION
#include "Renderer.hpp"

#include <fstream>

#include <SDL_vulkan.h>
#include <ThirdParty/glm/gtx/transform.hpp>

#include <VulkanInitializers.hpp>

void RenderObject::record_draw_commands(VkCommandBuffer cmd, RenderData& renderData, glm::mat4 view, glm::mat4 projection)
{
    if (mesh == nullptr)
        return;

    if (mesh->m_needsUpload) {
        throw std::runtime_error("recordDrawCommands() called on a RenderObject with a mesh yet to be uploaded");
    }

    glm::mat4 model(1.0f);
    if (transform != nullptr) {
        model = transform->getTransform();
    }

    GPUCameraData camData;
    camData.proj = projection;
    camData.view = view;

    void* data;
    vmaMapMemory(renderData.allocator, renderData.cameraData[renderData.frameIndex].allocation, &data);
    memcpy(data, &camData, sizeof(GPUCameraData));
    vmaUnmapMemory(renderData.allocator, renderData.cameraData[renderData.frameIndex].allocation);

    vmaMapMemory(renderData.allocator, renderData.sceneData[renderData.frameIndex].allocation, &data);
    data = (void*)((char*)data + index * sizeof(glm::mat4));
    memcpy(data, &model, sizeof(glm::mat4));
    vmaUnmapMemory(renderData.allocator, renderData.sceneData[renderData.frameIndex].allocation);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.pipelineLayout,
        0, 1, &renderData.globalDescriptor[renderData.frameIndex], 0, nullptr);

    MeshPushConstants constants;
    constants.index = index;

    vkCmdPushConstants(cmd, renderData.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->m_buffer.buffer, &offset);
    vkCmdDraw(cmd, mesh->m_vertices.size(), 1, 0, 0);
}

void Renderer::init()
{
    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);
    m_renderData.windowSize = { (uint32_t)w, (uint32_t)h };

    create_instance();
    create_swapchain();
    create_render_pass();
    prepare_resources();
    create_framebuffers();
    create_descriptors();
    create_pipelines();
    create_command_buffers();
    create_sync_objects();

    m_initialized = true;
}

void Renderer::update()
{
    m_renderObjects[0].transform->rot = glm::vec3(0.0f, m_renderData.frameNumber * 0.4f, 0.0f);

    int w, h;
    SDL_GetWindowSize(m_window, &w, &h);
    if (w != m_renderData.windowSize.width || h != m_renderData.windowSize.height) {
        recreate_swapchain();
        m_renderData.windowSize.width = w;
        m_renderData.windowSize.height = h;
    }
    if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) {
        return;
    }

    vkWaitForFences(m_device, 1, &m_renderData.inFlightFences[m_renderData.frameIndex], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(m_device,
        m_swapchain,
        UINT64_MAX,
        m_renderData.availableSemaphores[m_renderData.frameIndex],
        VK_NULL_HANDLE,
        &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        result = vkAcquireNextImageKHR(m_device,
            m_swapchain,
            UINT64_MAX,
            m_renderData.availableSemaphores[m_renderData.frameIndex],
            VK_NULL_HANDLE,
            &imageIndex);
    }
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to acquire next swapchain image");
    }

    if (m_renderData.imageInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_device, 1, &m_renderData.imageInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }

    m_renderData.imageInFlight[imageIndex] = m_renderData.inFlightFences[m_renderData.frameIndex];

    vkResetFences(m_device, 1, &m_renderData.inFlightFences[m_renderData.frameIndex]);

    VkRenderPassBeginInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_renderData.renderPass;
    rpInfo.framebuffer = m_renderData.framebuffers[imageIndex];
    rpInfo.renderArea.offset = { 0, 0 };
    rpInfo.renderArea.extent = m_swapchain.extent;
    VkClearValue clearColor { { { 0.0f, 0.0f, 0.0f, 1.0f } } };
    VkClearValue clearDepth {};
    clearDepth.depthStencil.depth = 1.0f;

    VkClearValue clearValues[2] = { clearColor, clearDepth };
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clearValues;

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchain.extent.width;
    viewport.height = (float)m_swapchain.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor {};
    scissor.offset = { 0, 0 };
    scissor.extent = m_swapchain.extent;

    VkCommandBufferBeginInfo beginInfo {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(m_renderData.commandBuffers[imageIndex], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin command buffer");
    }

    vkCmdSetViewport(m_renderData.commandBuffers[imageIndex], 0, 1, &viewport);
    vkCmdSetScissor(m_renderData.commandBuffers[imageIndex], 0, 1, &scissor);

    vkCmdBeginRenderPass(m_renderData.commandBuffers[imageIndex], &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    record_commands(m_renderData.commandBuffers[imageIndex]);

    vkCmdEndRenderPass(m_renderData.commandBuffers[imageIndex]);

    vkEndCommandBuffer(m_renderData.commandBuffers[imageIndex]);

    VkSemaphore waitSemaphores[] = { m_renderData.availableSemaphores[m_renderData.frameIndex] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_renderData.commandBuffers[imageIndex];

    VkSemaphore signalSemaphores[] = { m_renderData.finishedSemaphores[m_renderData.frameIndex] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    if (vkQueueSubmit(m_renderData.graphicsQueue, 1, &submitInfo, m_renderData.inFlightFences[m_renderData.frameIndex]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit queue");
    }

    VkSwapchainKHR swapchain = m_swapchain;

    VkPresentInfoKHR presentInfo {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    if (vkQueuePresentKHR(m_renderData.presentQueue, &presentInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to present image");
    }

    m_renderData.frameIndex = (m_renderData.frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
    m_renderData.frameNumber++;
}

void Renderer::exit()
{
    vkDeviceWaitIdle(m_device);
    for (auto& f : m_cleanupQueue) {
        f();
    }
}

void Renderer::create_instance()
{
    vkb::InstanceBuilder instanceBuilder;
    auto instanceRet = instanceBuilder.use_default_debug_messenger().request_validation_layers().build();
    if (!instanceRet) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    m_instance = instanceRet.value();
    m_cleanupQueue.push_front([this]() {
        vkb::destroy_instance(m_instance);
    });

    if (SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface) == SDL_FALSE) {
        throw std::runtime_error("Failed to create window surface");
    }
    m_cleanupQueue.push_front([this]() {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    });

    vkb::PhysicalDeviceSelector selector(m_instance);
    auto physDeviceRet = selector.set_surface(m_surface).select();
    if (!physDeviceRet) {
        throw std::runtime_error("No suitable GPUs found");
    }

    vkb::DeviceBuilder deviceBuilder(physDeviceRet.value());
    auto deviceRet = deviceBuilder.build();
    if (!deviceRet) {
        throw std::runtime_error("Failed to create device");
    }

    m_device = deviceRet.value();
    m_cleanupQueue.push_front([this]() {
        vkb::destroy_device(m_device);
    });

    auto gq = m_device.get_queue(vkb::QueueType::graphics);
    auto pq = m_device.get_queue(vkb::QueueType::present);
    if (!gq.has_value() || !pq.has_value()) {
        throw std::runtime_error("Failed to find queues");
    }
    m_renderData.graphicsQueue = gq.value();
    m_renderData.presentQueue = pq.value();

    VmaAllocatorCreateInfo allocatorInfo {};
    allocatorInfo.physicalDevice = physDeviceRet.value();
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    vmaCreateAllocator(&allocatorInfo, &m_renderData.allocator);
    m_cleanupQueue.push_front([this]() {
        vmaDestroyAllocator(m_renderData.allocator);
    });
}

void Renderer::create_swapchain()
{
    vkb::SwapchainBuilder swapchainBuilder(m_device);
    auto swapRet = swapchainBuilder.set_old_swapchain(m_swapchain).build();
    if (!swapRet) {
        throw std::runtime_error("Failed to create swapchain");
    }

    vkb::destroy_swapchain(m_swapchain);
    m_swapchain = swapRet.value();

    m_renderData.swapchainImages = m_swapchain.get_images().value();
    m_renderData.swapchainImageViews = m_swapchain.get_image_views().value();

    if (!m_initialized) {
        m_cleanupQueue.push_front([this]() {
            m_swapchain.destroy_image_views(m_renderData.swapchainImageViews);
            vkb::destroy_swapchain(m_swapchain);
        });
    }

    m_renderData.depthImages.resize(m_renderData.swapchainImages.size());
    m_renderData.depthImageViews.resize(m_renderData.swapchainImageViews.size());

    VkExtent3D depthImageExtent = { m_swapchain.extent.width, m_swapchain.extent.height, 1 };

    VkImageCreateInfo depthCreateInfo = image_create_info(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

    VmaAllocationCreateInfo depthAllocInfo {};
    depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    depthAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    for (size_t i = 0; i < m_renderData.swapchainImages.size(); i++) {
        if (m_initialized) {
            vkDestroyImageView(m_device, m_renderData.depthImageViews[i], nullptr);
            vmaDestroyImage(m_renderData.allocator, m_renderData.depthImages[i].image, m_renderData.depthImages[i].allocation);
        }

        if (vmaCreateImage(m_renderData.allocator,
                &depthCreateInfo,
                &depthAllocInfo,
                &m_renderData.depthImages[i].image,
                &m_renderData.depthImages[i].allocation,
                nullptr)
            != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate depth buffer");
        }

        m_renderData.depthImages[i].inUse = true;
        m_renderData.depthImages[i].allocator = std::make_shared<VmaAllocator>(m_renderData.allocator);

        VkImageViewCreateInfo depthViewInfo = image_view_create_info(VK_FORMAT_D32_SFLOAT, m_renderData.depthImages[i].image, VK_IMAGE_ASPECT_DEPTH_BIT);
        if (vkCreateImageView(m_device, &depthViewInfo, nullptr, &m_renderData.depthImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image view");
        }

        if (!m_initialized) {
            m_cleanupQueue.push_front([this, i]() {
                vkDestroyImageView(m_device, m_renderData.depthImageViews[i], nullptr);
                vmaDestroyImage(m_renderData.allocator, m_renderData.depthImages[i].image, m_renderData.depthImages[i].allocation);
            });
        }
    }
}

void Renderer::create_render_pass()
{
    VkAttachmentDescription colorAttachment {};
    colorAttachment.format = m_swapchain.image_format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment {};
    depthAttachment.flags = 0;
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency depthDependency {};
    depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    depthDependency.dstSubpass = 0;
    depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependency.srcAccessMask = 0;
    depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };
    VkSubpassDependency dependencies[2] = { dependency, depthDependency };

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;

    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderData.renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
    m_cleanupQueue.push_front([this]() {
        vkDestroyRenderPass(m_device, m_renderData.renderPass, nullptr);
    });
}

void Renderer::prepare_resources()
{
    std::shared_ptr<Mesh> triangle = std::make_shared<Mesh>();
    std::vector<Vertex> triVerts(3);
    triVerts[0].pos = glm::vec3(1.f, 1.f, 0.f);
    triVerts[1].pos = glm::vec3(-1.f, 1.f, 0.f);
    triVerts[2].pos = glm::vec3(0.f, -1.f, 0.f);

    triVerts[0].color = glm::vec3(1.f, 0.f, 0.f);
    triVerts[1].color = glm::vec3(0.f, 1.f, 0.f);
    triVerts[2].color = glm::vec3(0.f, 0.f, 1.f);

    triangle->set_vertices(triVerts);

    add_render_object(RenderObject { .mesh = triangle, .transform = std::make_shared<Transform>(glm::vec3(0.0f, 0.0f, 0.0f)) });

    m_camera = std::make_tuple<Camera, Transform>(Camera(), Transform(glm::vec3(0.0f, 0.0f, -2.0f)));
}

void Renderer::create_framebuffers()
{
    m_renderData.framebuffers.resize(m_renderData.swapchainImageViews.size());

    for (size_t i = 0; i < m_renderData.swapchainImages.size(); ++i) {
        VkImageView attachments[2] = { m_renderData.swapchainImageViews[i], m_renderData.depthImageViews[i] };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderData.renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchain.extent.width;
        framebufferInfo.height = m_swapchain.extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_renderData.framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffers");
        }

        if (!m_initialized) {
            m_cleanupQueue.push_front([this, i]() {
                vkDestroyFramebuffer(m_device, m_renderData.framebuffers[i], nullptr);
            });
        }
    }
}

void Renderer::create_descriptors()
{
    m_renderData.cameraData.resize(m_renderData.swapchainImages.size());
    m_renderData.sceneData.resize(m_renderData.swapchainImages.size());

    m_renderData.globalDescriptor.resize(m_renderData.swapchainImages.size());

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

    vkCreateDescriptorSetLayout(m_device, &globalSetInfo, nullptr, &m_renderData.globalSetLayout);

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

    vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_renderData.descriptorPool);

    m_cleanupQueue.push_front([this]() {
        vkDestroyDescriptorSetLayout(m_device, m_renderData.globalSetLayout, nullptr);
        vkDestroyDescriptorPool(m_device, m_renderData.descriptorPool, nullptr);
    });

    for (int i = 0; i < m_renderData.swapchainImages.size(); i++) {
        m_renderData.cameraData[i] = create_buffer(std::make_shared<VmaAllocator>(m_renderData.allocator),
            sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_renderData.sceneData[i] = create_buffer(std::make_shared<VmaAllocator>(m_renderData.allocator),
            MAX_OBJECTS * sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        m_cleanupQueue.push_front([this, i]() {
            vmaDestroyBuffer(m_renderData.allocator, m_renderData.cameraData[i].buffer, m_renderData.cameraData[i].allocation);
            vmaDestroyBuffer(m_renderData.allocator, m_renderData.sceneData[i].buffer, m_renderData.sceneData[i].allocation);
        });

        VkDescriptorSetAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_renderData.descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_renderData.globalSetLayout;

        vkAllocateDescriptorSets(m_device, &allocInfo, &m_renderData.globalDescriptor[i]);

        VkDescriptorBufferInfo camBufInfo {};
        camBufInfo.buffer = m_renderData.cameraData[i].buffer;
        camBufInfo.offset = 0;
        camBufInfo.range = sizeof(GPUCameraData);

        VkWriteDescriptorSet camSetWrite {};
        camSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        camSetWrite.dstBinding = 0;
        camSetWrite.dstSet = m_renderData.globalDescriptor[i];
        camSetWrite.descriptorCount = 1;
        camSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        camSetWrite.pBufferInfo = &camBufInfo;

        VkDescriptorBufferInfo sceneBufInfo {};
        sceneBufInfo.buffer = m_renderData.sceneData[i].buffer;
        sceneBufInfo.offset = 0;
        sceneBufInfo.range = MAX_OBJECTS * sizeof(glm::mat4);

        VkWriteDescriptorSet sceneSetWrite {};
        sceneSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sceneSetWrite.dstBinding = 1;
        sceneSetWrite.dstSet = m_renderData.globalDescriptor[i];
        sceneSetWrite.descriptorCount = 1;
        sceneSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sceneSetWrite.pBufferInfo = &sceneBufInfo;

        VkWriteDescriptorSet setWrites[] = { camSetWrite, sceneSetWrite };

        vkUpdateDescriptorSets(m_device, 2, setWrites, 0, nullptr);
    }
}

void Renderer::create_pipelines()
{
    auto createShaderModule = [this](std::string filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file");
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

        file.seekg(0);
        file.read((char*)buffer.data(), fileSize * sizeof(uint32_t));

        file.close();

        VkShaderModuleCreateInfo createInfo {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = buffer.size() * sizeof(uint32_t);
        createInfo.pCode = buffer.data();

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create shader module");
        }

        return shaderModule;
    };

    VkShaderModule vertModule = createShaderModule("meshvert.spv");
    VkShaderModule fragModule = createShaderModule("meshfrag.spv");

    VkPipelineShaderStageCreateInfo vertStageInfo {};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo {};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStageInfo, fragStageInfo };

    VertexInputDescription vertexDescription = Vertex::get_vertex_description();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();
    vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();
    vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_swapchain.extent.width;
    viewport.height = (float)m_swapchain.extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = m_swapchain.extent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    VkPushConstantRange pushConstant;
    pushConstant.offset = 0;
    pushConstant.size = sizeof(MeshPushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayout layouts[] = { m_renderData.globalSetLayout };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = layouts;

    if (vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_renderData.pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamicInfo = {};
    dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicInfo.pDynamicStates = dynamicStates.data();

    VkPipelineDepthStencilStateCreateInfo depthStencilInfo = depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicInfo;
    pipelineInfo.pDepthStencilState = &depthStencilInfo;
    pipelineInfo.layout = m_renderData.pipelineLayout;
    pipelineInfo.renderPass = m_renderData.renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_renderData.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    m_cleanupQueue.push_front([this]() {
        vkDestroyPipelineLayout(m_device, m_renderData.pipelineLayout, nullptr);
        vkDestroyPipeline(m_device, m_renderData.pipeline, nullptr);
    });

    vkDestroyShaderModule(m_device, fragModule, nullptr);
    vkDestroyShaderModule(m_device, vertModule, nullptr);
}

void Renderer::create_command_buffers()
{
    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_device.get_queue_index(vkb::QueueType::graphics).value();

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_renderData.commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
    if (!m_initialized) {
        m_cleanupQueue.push_front([this]() {
            vkDestroyCommandPool(m_device, m_renderData.commandPool, nullptr);
        });
    }

    m_renderData.commandBuffers.resize(m_renderData.framebuffers.size());

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_renderData.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_renderData.commandBuffers.size();

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_renderData.commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }
}

void Renderer::create_sync_objects()
{
    m_renderData.availableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderData.finishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderData.inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderData.imageInFlight.resize(m_swapchain.image_count, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderData.availableSemaphores[i]) != VK_SUCCESS
            || vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderData.finishedSemaphores[i]) != VK_SUCCESS
            || vkCreateFence(m_device, &fenceInfo, nullptr, &m_renderData.inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create sync objects");
        }

        m_cleanupQueue.push_front([this, i]() {
            vkDestroySemaphore(m_device, m_renderData.availableSemaphores[i], nullptr);
            vkDestroySemaphore(m_device, m_renderData.finishedSemaphores[i], nullptr);
            vkDestroyFence(m_device, m_renderData.inFlightFences[i], nullptr);
        });
    }
}

void Renderer::recreate_swapchain()
{
    vkDeviceWaitIdle(m_device);

    vkDestroyCommandPool(m_device, m_renderData.commandPool, nullptr);

    for (auto framebuffer : m_renderData.framebuffers) {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }

    m_swapchain.destroy_image_views(m_renderData.swapchainImageViews);

    create_swapchain();
    create_framebuffers();
    create_command_buffers();
}

void Renderer::record_commands(VkCommandBuffer cmd)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_renderData.pipeline);

    for (RenderObject& r : m_renderObjects) {
        if (r.mesh != nullptr) {
            if (r.mesh->m_needsUpload) {
                upload_mesh(*r.mesh);
            }
            glm::mat4 view = std::get<1>(m_camera).getTransform();
            glm::mat4 projection = std::get<0>(m_camera).getProjMatrix(m_renderData.windowSize.width, m_renderData.windowSize.height);

            r.record_draw_commands(cmd, m_renderData, view, projection);
        }
    }
}

void Renderer::upload_mesh(Mesh& mesh)
{
    if (mesh.m_buffer.inUse) {
        vmaDestroyBuffer(m_renderData.allocator, mesh.m_buffer.buffer, mesh.m_buffer.allocation);
    }

    mesh.m_buffer = create_buffer(
        std::make_shared<VmaAllocator>(m_renderData.allocator),
        mesh.m_vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    void* data;

    vmaMapMemory(m_renderData.allocator, mesh.m_buffer.allocation, &data);

    memcpy(data, mesh.m_vertices.data(), mesh.m_vertices.size() * sizeof(Vertex));

    vmaUnmapMemory(m_renderData.allocator, mesh.m_buffer.allocation);

    mesh.m_buffer.allocator = std::make_shared<VmaAllocator>(m_renderData.allocator);
    mesh.m_buffer.inUse = true;

    mesh.m_needsUpload = false;
}

void Renderer::add_render_object(RenderObject&& renderObject)
{
    renderObject.index = m_renderObjects.size();

    if (renderObject.mesh != nullptr && renderObject.mesh->m_vertices.size() != 0) {
        upload_mesh(*renderObject.mesh);
    }

    m_renderObjects.push_back(renderObject);

    m_cleanupQueue.push_front([renderObject]() {
        if (renderObject.mesh != nullptr) {
            renderObject.mesh->cleanup();
        }
    });
}
