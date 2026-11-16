#pragma once

#include "render/Camera.hpp"

#include <glm/glm.hpp>

#include <string>

namespace emerald {

// Fixed camera for reproducible screenshots. World units are metres, origin at the heightmap centre.
struct ViewPreset {
    const char* name;
    glm::vec3 target;
    float azimuth;   // radians, see Camera
    float elevation; // radians
    float distance;  // metres
};

// Returns nullptr if no preset has this name.
const ViewPreset* findViewPreset(const std::string& name);

// Comma-separated preset names, for usage text.
std::string viewPresetNames();

// Places the camera at the preset pose and sets its near plane.
void applyViewPreset(const ViewPreset& preset, Camera& camera);

} // namespace emerald
