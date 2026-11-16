#include "render/Atmosphere.hpp"

#include "render/Shader.hpp"

#include <cmath>

namespace emerald {

glm::vec3 sunDirection(float bearingDegrees, float elevationDegrees)
{
    const float bearing = glm::radians(bearingDegrees);
    const float elevation = glm::radians(elevationDegrees);
    const float horizontal = std::cos(elevation);
    return glm::normalize(glm::vec3(horizontal * std::sin(bearing), std::sin(elevation),
                                    -horizontal * std::cos(bearing)));
}

void Atmosphere::apply(Shader& shader) const
{
    shader.set("uSunDir", sunDir);
    shader.set("uSunColour", sunColour);
    shader.set("uAmbient", ambient);
    shader.set("uHorizonColour", horizonColour);
    shader.set("uZenithColour", zenithColour);
}

} // namespace emerald
