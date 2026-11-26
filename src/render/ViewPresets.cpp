#include "render/ViewPresets.hpp"

#include <array>

namespace emerald {

namespace {

// Targets from ITM coordinates of each landmark, relative to the heightmap centre.
const std::array<ViewPreset, 8> kPresets = {{
    // Whole island from the south-east, side-lit by the south-west sun.
    {"overview", glm::vec3(0.0f, 0.0f, 0.0f), 0.8f, 0.2f, 400e3f},
    // MacGillycuddy's Reeks and Carrauntoohil from the east.
    {"kerry", glm::vec3(-116e3f, 600.0f, 156e3f), 1.57f, 0.25f, 45e3f},
    // Wicklow Mountains around Lugnaquilla from the south-east.
    {"wicklow", glm::vec3(107e3f, 500.0f, 49e3f), 0.6f, 0.3f, 60e3f},
    // Low over the Glenbeigh hills (~400 m above them), looking west-north-west across Dingle Bay
    // and the Dingle peninsula to the Atlantic; the south-west sun rakes in from the left.
    {"glenbeigh", glm::vec3(-154956.0f, 0.0f, 137293.0f), 1.047f, 0.045f, 30030.0f},
    // Procedural archipelago ~1040 km west-north-west of Ireland (default seed): low over a bay,
    // looking north-west at a ridged massif with the south-west sun to the left.
    {"archipelago", glm::vec3(-1005e3f, 900.0f, -265e3f), 0.5f, 0.03f, 60e3f},
    // The same archipelago closer in, looking north-north-east along ridges and valleys: an evening
    // sun from the west-north-west throws long shadows across the valley floors.
    {"ridges", glm::vec3(-1012e3f, 300.0f, -258e3f), -0.3f, 0.3f, 22e3f},
    // Kerry from the north, looking south-south-east over the Reeks to the Kenmare River. At
    // midsummer midnight the full moon stands due south, 30 degrees right of the view, so slopes
    // and the bay catch it from the side.
    {"kerry-south", glm::vec3(-116e3f, 600.0f, 170e3f), 3.66f, 0.10f, 45e3f},
    // Low on the shore west of Glenbeigh, ~32 m above the ground, looking north-north-west
    // across Castlemaine Harbour to the Dingle peninsula: close enough for rain to land nearby.
    {"shore", glm::vec3(-161956.0f, 0.0f, 126293.0f), 0.291f, 0.0056f, 10440.0f},
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

int viewPresetCount()
{
    return static_cast<int>(kPresets.size());
}

const ViewPreset& viewPreset(int index)
{
    return kPresets[static_cast<std::size_t>(index)];
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
