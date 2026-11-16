#pragma once

#include <glm/glm.hpp>

namespace emerald {

// Orbits a target point at a fixed distance and elevation.
class Camera {
public:
    glm::vec3 target{0.0f};
    float distance = 1000.0f;
    float azimuth = 0.0f;   // radians around +Y
    float elevation = 0.6f; // radians above the XZ plane
    float fovY = 0.785f;    // radians
    float nearPlane = 1.0f;
    float farPlane = 5000.0f;

    void orbit(float deltaRadians) { azimuth += deltaRadians; }

    glm::vec3 position() const;
    glm::mat4 view() const;
    glm::mat4 projection(float aspect) const;
};

} // namespace emerald
