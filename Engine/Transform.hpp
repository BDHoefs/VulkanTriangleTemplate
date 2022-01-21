#pragma once
#include <ThirdParty/glm/glm.hpp>

struct Transform {
    Transform(glm::vec3 pos = {}, glm::vec3 rot = {}, glm::vec3 scale = glm::vec3(1.0f))
        : pos(pos)
        , rot(rot)
        , scale(scale)
    {
    }

    glm::mat4 getTransform()
    {
        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, pos);

        transform = glm::rotate(transform, rot.x, glm::vec3(1.f, 0.f, 0.f));
        transform = glm::rotate(transform, rot.y, glm::vec3(0.f, 1.f, 0.f));
        transform = glm::rotate(transform, rot.z, glm::vec3(0.f, 0.f, 1.f));

        transform = glm::scale(transform, scale);

        return transform;
    }

    glm::vec3 pos {};
    glm::vec3 rot {};
    glm::vec3 scale = glm::vec3(1.0f);
};