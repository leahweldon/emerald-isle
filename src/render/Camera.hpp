#pragma once

#include <glm/glm.hpp>

namespace emerald {

// Camera pose and projection. Yaw is a compass bearing (0 = north, -Z; pi/2 = east, +X);
// pitch is the angle above the horizontal.
class Camera {
public:
    static constexpr float kMaxPitch = 1.553f; // 89 degrees, avoids flipping at the poles

    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float fovY = 0.785f; // radians
    float nearPlane = 1.0f;
    float farPlane = 5000.0f;

    glm::vec3 forward() const;
    glm::vec3 right() const;

    // Places the camera at target + (sin az, ., cos az) * distance horizontally, elevation above it,
    // looking at target. This is the --view preset convention.
    void lookFrom(const glm::vec3& target, float azimuth, float elevation, float distance);

    void clampPitch();

    glm::mat4 view() const;
    glm::mat4 projection(float aspect) const;
};

} // namespace emerald
