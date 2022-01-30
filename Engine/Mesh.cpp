#include "Mesh.hpp"

VertexInputDescription Vertex::get_vertex_description()
{
    VertexInputDescription description;

    VkVertexInputBindingDescription mainBinding {};
    mainBinding.binding = 0;
    mainBinding.stride = sizeof(Vertex);
    mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    description.bindings.push_back(mainBinding);

    VkVertexInputAttributeDescription positionAttribute {};
    positionAttribute.binding = 0;
    positionAttribute.location = 0;
    positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    positionAttribute.offset = offsetof(Vertex, pos);

    VkVertexInputAttributeDescription normalAttribute = {};
    normalAttribute.binding = 0;
    normalAttribute.location = 1;
    normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    normalAttribute.offset = offsetof(Vertex, norm);

    VkVertexInputAttributeDescription colorAttribute = {};
    colorAttribute.binding = 0;
    colorAttribute.location = 2;
    colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    colorAttribute.offset = offsetof(Vertex, color);

    description.attributes.push_back(positionAttribute);
    description.attributes.push_back(normalAttribute);
    description.attributes.push_back(colorAttribute);

    return description;
}

const std::vector<Vertex>& Mesh::getVertices()
{
    return m_vertices;
}

void Mesh::set_vertices(std::vector<Vertex> vertices)
{
    m_vertices = vertices;
    m_needsUpload = true;
}

void Mesh::cleanup()
{
    if (m_buffer.inUse) {
        vmaDestroyBuffer(*m_buffer.allocator, m_buffer.buffer, m_buffer.allocation);
    }
}