#pragma once

#include <ThirdParty/glm/glm.hpp>
#include <ThirdParty/glm/gtx/transform.hpp>

struct Camera {
    Camera(float fov = 90.0f, float clipNear = 0.1f, float clipFar = 200.0f)
        : fov(fov)
        , clipNear(clipNear)
        , clipFar(clipFar)
    {
    }

    glm::mat4 getProjMatrix(float width, float height)
    {
        return glm::perspective(fov, (width / height), clipNear, clipFar);
    }

    float fov = 90.0f;
    float clipNear = 0.1f;
    float clipFar = 200.0f;
};