#pragma once

#include <SDL.h>
#include <SDL_vulkan.h>

#include <Renderer/PresentPass.hpp>

struct MeshPushConstants {
    uint32_t index;
};

struct GPUCameraData {
    glm::mat4 view;
    glm::mat4 proj;
};

struct GlobalRenderContext {
    std::pair<Camera, Transform> camera;

    VmaAllocator allocator;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    vkb::Instance instance;
    vkb::Device device;

    VkDescriptorPool descriptorPool;

    VkDescriptorSetLayout globalSetLayout;
    std::vector<VkDescriptorSet> globalDescriptor;

    std::vector<AllocatedBuffer> cameraData;
    std::vector<AllocatedBuffer> sceneData;

    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    VkExtent2D windowSize;

    int frameIndex = 0;
    size_t frameNumber = 0;

    size_t numSwapchainImages = 0;
    uint32_t swapchainIndex = 0;
};

struct UploadContext {
    VkFence uploadFence;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
};

const int MAX_FRAMES_IN_FLIGHT = 2;

class Renderer {
public:
    Renderer(SDL_Window* window)
        : m_window(window)
    {
        m_globalData = std::make_shared<GlobalRenderContext>();
        m_uploadData = std::make_shared<UploadContext>();
    }

    void init();
    void update();
    void exit();

    void recreate_swapchain();

    std::shared_ptr<GlobalRenderContext> get_global_data();

    void init_instance();
    void init_render_passes();
    void init_global_descriptor();
    void prepare_resources();
    void create_command_buffers();
    void bind_global_descriptor();

private:
    // Non-copyable and non-movable due to the lambdas in m_cleanupQueue capturing "this"
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    void immediate_submit(std::function<void(VkCommandBuffer cmd)> f);

private:
    SDL_Window* m_window;

    std::shared_ptr<GlobalRenderContext> m_globalData;
    std::shared_ptr<UploadContext> m_uploadData;

    bool m_initialized = false;

    VkSurfaceKHR m_surface;

    std::vector<PresentPass> m_renderPasses;
    PresentPass m_presentPass;

    std::deque<std::function<void()>> m_cleanupQueue;
};