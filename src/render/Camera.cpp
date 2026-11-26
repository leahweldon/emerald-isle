#include "render/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace emerald {

glm::vec3 Camera::forward() const
{
    const float horizontal = std::cos(pitch);
    return glm::vec3(horizontal * std::sin(yaw), std::sin(pitch), -horizontal * std::cos(yaw));
}

glm::vec3 Camera::right() const
{
    return glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
}

void Camera::lookFrom(const glm::dvec3& target, float azimuth, float elevation, float distance)
{
    const double horizontal = static_cast<double>(distance) * std::cos(static_cast<double>(elevation));
    const glm::dvec3 offset(horizontal * std::sin(static_cast<double>(azimuth)),
                            static_cast<double>(distance) * std::sin(static_cast<double>(elevation)),
                            horizontal * std::cos(static_cast<double>(azimuth)));
    position = target + offset;
    const glm::vec3 dir = glm::normalize(glm::vec3(-offset));
    yaw = std::atan2(dir.x, -dir.z);
    pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));
    clampPitch();
}

void Camera::clampPitch()
{
    pitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
}

glm::mat4 Camera::view() const
{
    // Camera at the origin; callers pass camera-relative positions.
    return glm::lookAt(glm::vec3(0.0f), forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

float Camera::verticalFov(float aspect) const
{
    if (aspect >= 1.0f) {
        return fovY;
    }
    constexpr float kLandscape = 16.0f / 9.0f;
    constexpr float kMaxFovY = 1.40f; // 80 degrees
    return std::min(2.0f * std::atan(std::tan(0.5f * fovY) * kLandscape / aspect), kMaxFovY);
}

glm::mat4 Camera::projection(float aspect) const
{
    return glm::perspective(verticalFov(aspect), aspect, nearPlane, farPlane);
}

} // namespace emerald
