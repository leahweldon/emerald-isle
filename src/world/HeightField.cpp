#include "world/HeightField.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace emerald {

namespace {

// Procedural world shape, in metres.
constexpr double kWarpScale = 80e3;       // domain warp wavelength
constexpr double kWarpAmplitude = 30e3;   // how far the warp bends coastlines
constexpr double kContinentScale = 110e3; // island size
constexpr double kSeaThreshold = 0.12;    // continent value where land starts
constexpr double kRegionScale = 600e3;    // dense archipelagos versus open ocean
constexpr double kRegionStrength = 0.35;  // how far regions shift the land threshold
constexpr double kBaseRelief = 1100.0;    // height per unit of continent above the threshold
constexpr double kHillScale = 6e3;
constexpr double kHillRelief = 120.0;
constexpr double kRidgeScale = 12e3;
constexpr double kRidgeRelief = 550.0;

// Near the DEM: blend band inside its border, and a ring outside kept clear of procedural land.
constexpr double kBlendMargin = 20e3;
constexpr double kClearStart = 60e3;
constexpr double kClearEnd = 180e3;

double smoothstep(double edge0, double edge1, double x)
{
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

HeightField::HeightField(const std::filesystem::path& demPath, const HeightFieldConfig& config)
    : m_config(config)
    , m_noise(config.seed)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_us, void (*)(void*)> pixels(
        stbi_load_16(demPath.string().c_str(), &width, &height, &channels, 1), stbi_image_free);
    if (!pixels) {
        throw std::runtime_error("Could not load heightmap " + demPath.string() + ": " +
                                 stbi_failure_reason());
    }
    if (width < 2 || height < 2) {
        throw std::runtime_error("Heightmap is too small: " + demPath.string());
    }

    m_demWidth = width;
    m_demHeight = height;
    m_dem.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < m_dem.size(); ++i) {
        const stbi_us raw = pixels.get()[i];
        const float metres = raw == 0 ? -config.seaFloorDepth
                                      : static_cast<float>(raw) * config.metresPerUnit;
        m_dem[i] = metres * config.verticalExaggeration;
    }
    // Texel centres sit on the grid: column c at x = (c - (width - 1) / 2) * spacing.
    m_halfWidth = 0.5 * static_cast<double>(width - 1) * config.metresPerPixel;
    m_halfDepth = 0.5 * static_cast<double>(height - 1) * config.metresPerPixel;
}

double HeightField::demAt(double x, double z) const
{
    const double spacing = m_config.metresPerPixel;
    const double fx = std::clamp((x + m_halfWidth) / spacing, 0.0, static_cast<double>(m_demWidth - 1));
    const double fz = std::clamp((z + m_halfDepth) / spacing, 0.0, static_cast<double>(m_demHeight - 1));
    const int x0 = std::min(static_cast<int>(fx), m_demWidth - 2);
    const int z0 = std::min(static_cast<int>(fz), m_demHeight - 2);
    const double tx = fx - x0;
    const double tz = fz - z0;
    const auto at = [this](int c, int r) {
        return static_cast<double>(
            m_dem[static_cast<std::size_t>(r) * static_cast<std::size_t>(m_demWidth) +
                  static_cast<std::size_t>(c)]);
    };
    const double top = at(x0, z0) + (at(x0 + 1, z0) - at(x0, z0)) * tx;
    const double bottom = at(x0, z0 + 1) + (at(x0 + 1, z0 + 1) - at(x0, z0 + 1)) * tx;
    return top + (bottom - top) * tz;
}

double HeightField::proceduralAt(double x, double z) const
{
    // Domain warp bends the continent field so coastlines are irregular rather than blobby.
    const double wx = m_noise.fbm(x / kWarpScale + 31.7, z / kWarpScale - 12.9, 3);
    const double wz = m_noise.fbm(x / kWarpScale - 54.1, z / kWarpScale + 7.3, 3);
    const double qx = x + kWarpAmplitude * wx;
    const double qz = z + kWarpAmplitude * wz;

    const double region = m_noise.fbm(x / kRegionScale + 3.3, z / kRegionScale - 8.1, 2);
    double land = m_noise.fbm(qx / kContinentScale, qz / kContinentScale, 5) - kSeaThreshold -
                  kRegionStrength * region;

    // Keep a ring of open sea around Ireland so the DEM border never meets procedural land.
    const double outX = std::max(std::abs(x) - m_halfWidth, 0.0);
    const double outZ = std::max(std::abs(z) - m_halfDepth, 0.0);
    const double distanceOutside = std::sqrt(outX * outX + outZ * outZ);
    land = -0.2 + (land + 0.2) * smoothstep(kClearStart, kClearEnd, distanceOutside);

    const double hills = m_noise.fbm(qx / kHillScale, qz / kHillScale, 6) * kHillRelief *
                         smoothstep(-0.03, 0.06, land);
    const double ridgeNoise = 1.0 - std::abs(m_noise.fbm(qx / kRidgeScale + 101.0, qz / kRidgeScale, 4));
    const double ridges = ridgeNoise * ridgeNoise * kRidgeRelief * smoothstep(0.03, 0.2, land);
    return land * kBaseRelief + hills + ridges;
}

double HeightField::heightAt(double x, double z) const
{
    const double insideX = m_halfWidth - std::abs(x);
    const double insideZ = m_halfDepth - std::abs(z);
    const double inside = std::min(insideX, insideZ); // distance to the DEM border, > 0 inside

    const double seaFloor = -m_config.seaFloorDepth * m_config.verticalExaggeration;
    if (inside >= kBlendMargin) {
        return demAt(x, z);
    }
    const double procedural =
        std::max(proceduralAt(x, z) * m_config.verticalExaggeration, seaFloor);
    if (inside <= 0.0) {
        return procedural;
    }
    // Cross-fade so the DEM edge (mostly sea, Kintyre at the top) runs into the procedural sea.
    const double w = smoothstep(0.0, kBlendMargin, inside);
    return procedural + (demAt(x, z) - procedural) * w;
}

double HeightField::groundHeight(double x, double z) const
{
    return std::max(heightAt(x, z), 0.0);
}

void HeightField::fillTile(double originX, double originZ, double spacing, int size, float* out,
                           float& minY, float& maxY) const
{
    minY = 1e30f;
    maxY = -1e30f;
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            const auto h = static_cast<float>(heightAt(originX + i * spacing, originZ + j * spacing));
            out[static_cast<std::size_t>(j) * static_cast<std::size_t>(size) +
                static_cast<std::size_t>(i)] = h;
            minY = std::min(minY, h);
            maxY = std::max(maxY, h);
        }
    }
}

} // namespace emerald
