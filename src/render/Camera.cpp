#include "render/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace emerald {

glm::vec3 Camera::position() const
{
    const float horizontal = distance * std::cos(elevation);
    return target + glm::vec3(horizontal * std::sin(azimuth),
                              distance * std::sin(elevation),
                              horizontal * std::cos(azimuth));
}

glm::mat4 Camera::view() const
{
    return glm::lookAt(position(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projection(float aspect) const
{
    return glm::perspective(fovY, aspect, nearPlane, farPlane);
}

} // namespace emerald
