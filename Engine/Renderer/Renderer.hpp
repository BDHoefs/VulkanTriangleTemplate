#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>

#include <SDL.h>
#include <ThirdParty/vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <vulkan/vulkan.h>

#include <Camera.hpp>
#include <ECS/ECS.hpp>
#include <Mesh.hpp>
#include <Transform.hpp>

struct MeshPushConstants {
    uint32_t index;
};

struct GPUCameraData {
    glm::mat4 view;
    glm::mat4 proj;
};

struct RenderData {
    VmaAllocator allocator;

    VkRenderPass renderPass;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    VkDescriptorSetLayout globalSetLayout;

    VkDescriptorPool descriptorPool;

    std::vector<VkDescriptorSet> globalDescriptor;

    std::vector<AllocatedBuffer> cameraData;
    std::vector<AllocatedBuffer> sceneData;

    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<AllocatedImage> depthImages;
    std::vector<VkImageView> depthImageViews;

    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;

    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> availableSemaphores;
    std::vector<VkSemaphore> finishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imageInFlight;

    VkExtent2D windowSize;

    int frameIndex = 0;
    size_t frameNumber = 0;
};

const int MAX_FRAMES_IN_FLIGHT = 2;

class Renderer {
public:
    Renderer(SDL_Window* window)
        : m_window(window)
    {
    }
    void init();
    void update();
    void exit();

public:
    // Non-copyable and non-movable due to the lambdas in m_cleanupQueue capturing "this"
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

private:
    void create_instance();
    void create_swapchain();
    void create_render_pass();
    void prepare_resources();
    void create_framebuffers();
    void create_descriptors();
    void create_pipelines();
    void create_command_buffers();
    void create_sync_objects();

    void recreate_swapchain();

    void upload_mesh(Mesh& m);

    void record_commands(VkCommandBuffer cmd);
    void record_entity_commands(VkCommandBuffer cmd, ECS::Entity& e);

private:
    bool m_initialized = false;

    ECS::EntityManager m_em;

    std::tuple<Camera, Transform> m_camera;

    RenderData m_renderData;

    SDL_Window* m_window;

    vkb::Instance m_instance;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    vkb::Device m_device;
    vkb::Swapchain m_swapchain;

    std::deque<std::function<void()>> m_cleanupQueue;
};