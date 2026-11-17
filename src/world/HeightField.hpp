#pragma once

#include "world/Noise.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace emerald {

// Heightmap encoding written by tools/make_heightmap.py.
constexpr float kHeightmapMetresPerPixel = 230.0f;
constexpr float kHeightmapMetresPerUnit = 0.02f;

// Relief is hard to read at true scale from orbit.
constexpr float kVerticalExaggeration = 2.5f;

struct HeightFieldConfig {
    float metresPerPixel = kHeightmapMetresPerPixel;
    float metresPerUnit = kHeightmapMetresPerUnit;
    float verticalExaggeration = kVerticalExaggeration;
    // Sea is lowered to this depth so coasts slope under the sea plane.
    float seaFloorDepth = 30.0f;
    std::uint32_t seed = 1;
};

// World surface height, in world units (metres, vertically exaggerated). The Ireland DEM fills its
// own rectangle, centred on the origin; everywhere else is seeded procedural islands, cross-faded
// into the DEM near its border. Read-only after construction, so safe to share between threads.
class HeightField {
public:
    HeightField(const std::filesystem::path& demPath, const HeightFieldConfig& config);

    // Exaggerated height at (x, z); below 0 is under the sea.
    double heightAt(double x, double z) const;
    // Never below sea level: for keeping the camera above ground or water.
    double groundHeight(double x, double z) const;

    // Fills size x size samples at origin + (i, j) * spacing (row-major, j along +Z) and returns
    // their range in minY / maxY.
    void fillTile(double originX, double originZ, double spacing, int size, float* out,
                  float& minY, float& maxY) const;

    // DEM extent and data, for passes that still read the DEM directly.
    double demHalfWidth() const { return m_halfWidth; }
    double demHalfDepth() const { return m_halfDepth; }
    int demWidth() const { return m_demWidth; }
    int demHeight() const { return m_demHeight; }
    const std::vector<float>& demHeights() const { return m_dem; } // row 0 north
    float metresPerPixel() const { return m_config.metresPerPixel; }
    float verticalExaggeration() const { return m_config.verticalExaggeration; }
    std::uint32_t seed() const { return m_config.seed; }

private:
    double demAt(double x, double z) const;
    double proceduralAt(double x, double z) const; // metres, not exaggerated

    HeightFieldConfig m_config;
    Noise m_noise;
    std::vector<float> m_dem; // exaggerated world heights, sea already lowered
    int m_demWidth = 0;
    int m_demHeight = 0;
    double m_halfWidth = 0.0;
    double m_halfDepth = 0.0;
};

} // namespace emerald
