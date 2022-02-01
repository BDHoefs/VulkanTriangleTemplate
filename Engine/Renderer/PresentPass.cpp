#define VMA_IMPLEMENTATION
#include "PresentPass.hpp"

#include <fstream>

#include <SDL_vulkan.h>
#include <ThirdParty/glm/gtx/transform.hpp>

#include <Renderer/Renderer.hpp>
#include <Renderer/VulkanInitializers.hpp>

void PresentPass::init(std::shared_ptr<GlobalRenderContext> globalData)
{
    m_globalData = globalData;

    create_render_pass();
    create_framebuffers(false);
    create_pipelines();
    create_sync_objects();
}

void PresentPass::update()
{
    vkWaitForFences(m_globalData->device, 1, &m_passData.inFlightFences[m_globalData->frameIndex], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(m_globalData->device,
        m_swapchain,
        UINT64_MAX,
        m_passData.availableSemaphores[m_globalData->frameIndex],
        VK_NULL_HANDLE,
        &m_globalData->swapchainIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        result = vkAcquireNextImageKHR(m_globalData->device,
            m_swapchain,
            UINT64_MAX,
            m_passData.availableSemaphores[m_globalData->frameIndex],
            VK_NULL_HANDLE,
            &m_globalData->swapchainIndex);
    }
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to acquire next swapchain image");
    }

    if (m_passData.imageInFlight[m_globalData->swapchainIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(m_globalData->device, 1, &m_passData.imageInFlight[m_globalData->swapchainIndex], VK_TRUE, UINT64_MAX);
    }

    m_passData.imageInFlight[m_globalData->swapchainIndex] = m_passData.inFlightFences[m_globalData->frameIndex];

    vkResetFences(m_globalData->device, 1, &m_passData.inFlightFences[m_globalData->frameIndex]);

    VkRenderPassBeginInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_passData.renderPass;
    rpInfo.framebuffer = m_passData.framebuffers[m_globalData->swapchainIndex];
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

    if (vkBeginCommandBuffer(m_globalData->commandBuffers[m_globalData->swapchainIndex], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin command buffer");
    }

    vkCmdSetViewport(m_globalData->commandBuffers[m_globalData->swapchainIndex], 0, 1, &viewport);
    vkCmdSetScissor(m_globalData->commandBuffers[m_globalData->swapchainIndex], 0, 1, &scissor);

    vkCmdBeginRenderPass(m_globalData->commandBuffers[m_globalData->swapchainIndex], &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    record_commands(m_globalData->commandBuffers[m_globalData->swapchainIndex]);

    vkCmdEndRenderPass(m_globalData->commandBuffers[m_globalData->swapchainIndex]);

    vkEndCommandBuffer(m_globalData->commandBuffers[m_globalData->swapchainIndex]);

    VkSemaphore waitSemaphores[] = { m_passData.availableSemaphores[m_globalData->frameIndex] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_globalData->commandBuffers[m_globalData->swapchainIndex];

    VkSemaphore signalSemaphores[] = { m_passData.finishedSemaphores[m_globalData->frameIndex] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    if (vkQueueSubmit(m_globalData->graphicsQueue, 1, &submitInfo, m_passData.inFlightFences[m_globalData->frameIndex]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit queue");
    }

    VkSwapchainKHR swapchain = m_swapchain;

    VkPresentInfoKHR presentInfo {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &m_globalData->swapchainIndex;

    if (vkQueuePresentKHR(m_globalData->presentQueue, &presentInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to present image");
    }
}

void PresentPass::exit()
{
    vkDeviceWaitIdle(m_globalData->device);
    m_em.each_component<Mesh>([this](ECS::Entity& e, Mesh* m) {
        m->cleanup();
    });

    for (auto& f : m_cleanupQueue) {
        f();
    }
}

void PresentPass::create_swapchain(bool recreation = false)
{
    if (recreation) {
        m_swapchain.destroy_image_views(m_passData.imageViews);
    }
    vkb::SwapchainBuilder swapchainBuilder(m_globalData->device);
    auto swapRet = swapchainBuilder.set_old_swapchain(m_swapchain).build();
    if (!swapRet) {
        throw std::runtime_error("Failed to create swapchain");
    }

    vkb::destroy_swapchain(m_swapchain);
    m_swapchain = swapRet.value();

    m_passData.images = m_swapchain.get_images().value();
    m_passData.imageViews = m_swapchain.get_image_views().value();
    m_globalData->numSwapchainImages = m_passData.images.size();

    if (!recreation) {
        m_cleanupQueue.push_front([this]() {
            m_swapchain.destroy_image_views(m_passData.imageViews);
            vkb::destroy_swapchain(m_swapchain);
        });
    }

    m_passData.depthImages.resize(m_passData.images.size());
    m_passData.depthImageViews.resize(m_passData.imageViews.size());

    VkExtent3D depthImageExtent = { m_swapchain.extent.width, m_swapchain.extent.height, 1 };

    VkImageCreateInfo depthCreateInfo = image_create_info(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

    VmaAllocationCreateInfo depthAllocInfo {};
    depthAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    depthAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    for (size_t i = 0; i < m_passData.images.size(); i++) {
        if (recreation) {
            vkDestroyImageView(m_globalData->device, m_passData.depthImageViews[i], nullptr);
            vmaDestroyImage(m_globalData->allocator, m_passData.depthImages[i].image, m_passData.depthImages[i].allocation);
        }

        if (vmaCreateImage(m_globalData->allocator,
                &depthCreateInfo,
                &depthAllocInfo,
                &m_passData.depthImages[i].image,
                &m_passData.depthImages[i].allocation,
                nullptr)
            != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate depth buffer");
        }

        m_passData.depthImages[i].inUse = true;
        m_passData.depthImages[i].allocator = std::make_shared<VmaAllocator>(m_globalData->allocator);

        VkImageViewCreateInfo depthViewInfo = image_view_create_info(VK_FORMAT_D32_SFLOAT, m_passData.depthImages[i].image, VK_IMAGE_ASPECT_DEPTH_BIT);
        if (vkCreateImageView(m_globalData->device, &depthViewInfo, nullptr, &m_passData.depthImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create depth image view");
        }

        if (!recreation) {
            m_cleanupQueue.push_front([this, i]() {
                vkDestroyImageView(m_globalData->device, m_passData.depthImageViews[i], nullptr);
                vmaDestroyImage(m_globalData->allocator, m_passData.depthImages[i].image, m_passData.depthImages[i].allocation);
            });
        }
    }
}

void PresentPass::create_render_pass()
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

    if (vkCreateRenderPass(m_globalData->device, &renderPassInfo, nullptr, &m_passData.renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
    m_cleanupQueue.push_front([this]() {
        vkDestroyRenderPass(m_globalData->device, m_passData.renderPass, nullptr);
    });
}

void PresentPass::create_framebuffers(bool recreation = false)
{
    m_passData.framebuffers.resize(m_passData.imageViews.size());

    for (size_t i = 0; i < m_passData.images.size(); ++i) {
        VkImageView attachments[2] = { m_passData.imageViews[i], m_passData.depthImageViews[i] };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_passData.renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchain.extent.width;
        framebufferInfo.height = m_swapchain.extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_globalData->device, &framebufferInfo, nullptr, &m_passData.framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffers");
        }

        if (!recreation) {
            m_cleanupQueue.push_front([this, i]() {
                vkDestroyFramebuffer(m_globalData->device, m_passData.framebuffers[i], nullptr);
            });
        }
    }
}

void PresentPass::create_pipelines()
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
        if (vkCreateShaderModule(m_globalData->device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
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

    VkDescriptorSetLayout layouts[] = { m_globalData->globalSetLayout };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = layouts;

    if (vkCreatePipelineLayout(m_globalData->device, &pipelineLayoutInfo, nullptr, &m_passData.pipelineLayout) != VK_SUCCESS) {
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
    pipelineInfo.layout = m_passData.pipelineLayout;
    pipelineInfo.renderPass = m_passData.renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(m_globalData->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_passData.pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    m_cleanupQueue.push_front([this]() {
        vkDestroyPipelineLayout(m_globalData->device, m_passData.pipelineLayout, nullptr);
        vkDestroyPipeline(m_globalData->device, m_passData.pipeline, nullptr);
    });

    vkDestroyShaderModule(m_globalData->device, fragModule, nullptr);
    vkDestroyShaderModule(m_globalData->device, vertModule, nullptr);
}

void PresentPass::create_sync_objects()
{
    m_passData.availableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_passData.finishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_passData.inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_passData.imageInFlight.resize(m_swapchain.image_count, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_globalData->device, &semaphoreInfo, nullptr, &m_passData.availableSemaphores[i]) != VK_SUCCESS
            || vkCreateSemaphore(m_globalData->device, &semaphoreInfo, nullptr, &m_passData.finishedSemaphores[i]) != VK_SUCCESS
            || vkCreateFence(m_globalData->device, &fenceInfo, nullptr, &m_passData.inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create sync objects");
        }

        m_cleanupQueue.push_front([this, i]() {
            vkDestroySemaphore(m_globalData->device, m_passData.availableSemaphores[i], nullptr);
            vkDestroySemaphore(m_globalData->device, m_passData.finishedSemaphores[i], nullptr);
            vkDestroyFence(m_globalData->device, m_passData.inFlightFences[i], nullptr);
        });
    }
}

void PresentPass::record_commands(VkCommandBuffer cmd)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_passData.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_passData.pipelineLayout,
        0, 1, &m_globalData->globalDescriptor[m_globalData->frameIndex], 0, nullptr);

    m_em.each_component<Mesh>([this, cmd](ECS::Entity& e, Mesh* m) {
        record_entity_commands(cmd, e);
    });
}

void PresentPass::record_entity_commands(VkCommandBuffer cmd, ECS::Entity& e)
{
    if (!e.get_component<Mesh>().has_value()) {
        throw std::runtime_error("Attempted to render entity with no renderable components");
    }

    Mesh* mesh = e.get_component<Mesh>().value();

    if (mesh->getVertices().size() == 0) {
        return;
    }

    glm::mat4 view = m_globalData->camera.second.getTransform();
    glm::mat4 projection = m_globalData->camera.first.getProjMatrix(m_globalData->windowSize.width, m_globalData->windowSize.height);

    glm::mat4 model(1.0f);
    Transform* t = e.get_component<Transform>().value();
    if (e.get_component<Transform>().has_value()) {
        model = e.get_component<Transform>().value()->getTransform();
    }

    GPUCameraData camData;
    camData.proj = projection;
    camData.view = view;

    void* data;
    vmaMapMemory(m_globalData->allocator, m_globalData->cameraData[m_globalData->frameIndex].allocation, &data);
    memcpy(data, &camData, sizeof(GPUCameraData));
    vmaUnmapMemory(m_globalData->allocator, m_globalData->cameraData[m_globalData->frameIndex].allocation);

    vmaMapMemory(m_globalData->allocator, m_globalData->sceneData[m_globalData->frameIndex].allocation, &data);
    data = (void*)((char*)data + e.get_eid() * sizeof(glm::mat4));
    memcpy(data, &model, sizeof(glm::mat4));
    vmaUnmapMemory(m_globalData->allocator, m_globalData->sceneData[m_globalData->frameIndex].allocation);

    MeshPushConstants constants;
    constants.index = e.get_eid();

    vkCmdPushConstants(cmd, m_passData.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh->m_buffer.buffer, &offset);
    vkCmdDraw(cmd, mesh->m_vertices.size(), 1, 0, 0);
}
