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

void Camera::lookFrom(const glm::vec3& target, float azimuth, float elevation, float distance)
{
    const float horizontal = distance * std::cos(elevation);
    position = target + glm::vec3(horizontal * std::sin(azimuth), distance * std::sin(elevation),
                                  horizontal * std::cos(azimuth));
    const glm::vec3 dir = glm::normalize(target - position);
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
    // Rotation from the direction alone, then translation: position + forward() would lose the
    // direction to float rounding when the camera is hundreds of kilometres from the origin.
    const glm::mat4 rotation = glm::lookAt(glm::vec3(0.0f), forward(), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::translate(rotation, -position);
}

glm::mat4 Camera::projection(float aspect) const
{
    return glm::perspective(fovY, aspect, nearPlane, farPlane);
}

} // namespace emerald
