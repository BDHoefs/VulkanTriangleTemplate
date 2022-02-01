#pragma once
#include <memory>

struct PassData {
    VkRenderPass renderPass;

    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;

    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;

    std::vector<AllocatedImage> depthImages;
    std::vector<VkImageView> depthImageViews;

    std::vector<VkFramebuffer> framebuffers;

    std::vector<VkSemaphore> availableSemaphores;
    std::vector<VkSemaphore> finishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imageInFlight;
};

struct GlobalRenderContext;

class RenderPass {
    virtual void init(std::shared_ptr<GlobalRenderContext> globalData) = 0;
    virtual void update() = 0;
    virtual void exit() = 0;
};