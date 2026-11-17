#include "render/ViewPresets.hpp"

#include <array>

namespace emerald {

namespace {

// Targets from ITM coordinates of each landmark, relative to the heightmap centre.
const std::array<ViewPreset, 4> kPresets = {{
    // Whole island from the south-east, side-lit by the south-west sun.
    {"overview", glm::vec3(0.0f, 0.0f, 0.0f), 0.8f, 0.2f, 400e3f},
    // MacGillycuddy's Reeks and Carrauntoohil from the east.
    {"kerry", glm::vec3(-116e3f, 600.0f, 156e3f), 1.57f, 0.25f, 45e3f},
    // Wicklow Mountains around Lugnaquilla from the south-east.
    {"wicklow", glm::vec3(107e3f, 500.0f, 49e3f), 0.6f, 0.3f, 60e3f},
    // Low over the Glenbeigh hills (~400 m above them), looking west-north-west across Dingle Bay
    // and the Dingle peninsula to the Atlantic; the south-west sun rakes in from the left.
    {"glenbeigh", glm::vec3(-154956.0f, 0.0f, 137293.0f), 1.047f, 0.045f, 30030.0f},
}};

} // namespace

const ViewPreset* findViewPreset(const std::string& name)
{
    for (const ViewPreset& preset : kPresets) {
        if (name == preset.name) {
            return &preset;
        }
    }
    return nullptr;
}

std::string viewPresetNames()
{
    std::string names;
    for (const ViewPreset& preset : kPresets) {
        if (!names.empty()) {
            names += ", ";
        }
        names += preset.name;
    }
    return names;
}

void applyViewPreset(const ViewPreset& preset, Camera& camera)
{
    camera.lookFrom(glm::dvec3(preset.target), preset.azimuth, preset.elevation, preset.distance);
    // Near plane scales with distance to keep depth precision at the coast.
    camera.nearPlane = preset.distance * 0.01f;
}

} // namespace emerald
