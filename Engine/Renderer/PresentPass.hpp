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
#include <Renderer/RenderPass.hpp>
#include <Transform.hpp>

class GlobalRenderContext;

class PresentPass : RenderPass {
public:
    PresentPass() { }

    void init(std::shared_ptr<GlobalRenderContext> globalData) override;
    void update() override;
    void exit() override;

public:
    // Non-copyable and non-movable due to the lambdas in m_cleanupQueue capturing "this"
    PresentPass(const PresentPass&) = delete;
    PresentPass& operator=(const PresentPass&) = delete;
    PresentPass(PresentPass&&) = delete;
    PresentPass& operator=(PresentPass&&) = delete;

    void set_global_context(std::shared_ptr<GlobalRenderContext> globalData) { m_globalData = globalData; }

    void create_swapchain(bool recreation);
    void create_render_pass();
    void create_framebuffers(bool recreation);
    void create_pipelines();
    void create_sync_objects();

    void record_commands(VkCommandBuffer cmd);
    void record_entity_commands(VkCommandBuffer cmd, ECS::Entity& e);

private:
    ECS::EntityManager m_em;

    std::shared_ptr<GlobalRenderContext> m_globalData;

    PassData m_passData;
    vkb::Swapchain m_swapchain;

    std::deque<std::function<void()>> m_cleanupQueue;

    friend class Renderer;
};