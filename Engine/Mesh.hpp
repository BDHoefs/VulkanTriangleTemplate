#pragma once

#include <vector>

#include <ThirdParty/glm/glm.hpp>
#include <ThirdParty/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <Renderer/VulkanTypes.hpp>

struct VertexInputDescription {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;

    VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 norm;
    glm::vec3 color;

    static VertexInputDescription get_vertex_description();
};

class GlobalRenderContext;

class Mesh {
public:
    Mesh(std::shared_ptr<GlobalRenderContext> globalData)
        : m_globalData(globalData)
    {
    }

    const std::vector<Vertex>& getVertices();
    void set_vertices(std::vector<Vertex> vertices);

    void cleanup();

private:
    void upload();

    std::shared_ptr<GlobalRenderContext> m_globalData;

    std::vector<Vertex> m_vertices;

    AllocatedBuffer m_buffer;
    friend class PresentPass;
    friend struct RenderObject;
};