#include "world/Noise.hpp"

#include <cmath>

namespace emerald {

namespace {

// Integer hash of a lattice point (PCG-style output mixing).
std::uint32_t hash(std::int64_t x, std::int64_t y, std::uint32_t seed)
{
    std::uint64_t h = static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ull ^
                      static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4Full ^
                      static_cast<std::uint64_t>(seed) * 0x165667B19E3779F9ull;
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return static_cast<std::uint32_t>(h);
}

// Dot product of the offset with one of 16 evenly spaced unit gradients.
double gradientDot(std::uint32_t h, double dx, double dy)
{
    constexpr double kStep = 6.283185307179586 / 16.0;
    const double angle = static_cast<double>(h & 15u) * kStep;
    return std::cos(angle) * dx + std::sin(angle) * dy;
}

double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

} // namespace

double Noise::gradient(double x, double y) const
{
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const auto ix = static_cast<std::int64_t>(fx);
    const auto iy = static_cast<std::int64_t>(fy);
    const double dx = x - fx;
    const double dy = y - fy;

    const double n00 = gradientDot(hash(ix, iy, m_seed), dx, dy);
    const double n10 = gradientDot(hash(ix + 1, iy, m_seed), dx - 1.0, dy);
    const double n01 = gradientDot(hash(ix, iy + 1, m_seed), dx, dy - 1.0);
    const double n11 = gradientDot(hash(ix + 1, iy + 1, m_seed), dx - 1.0, dy - 1.0);

    const double u = fade(dx);
    const double v = fade(dy);
    const double nx0 = n00 + (n10 - n00) * u;
    const double nx1 = n01 + (n11 - n01) * u;
    // Gradient noise peaks near +-0.7; scale towards [-1, 1].
    return 1.4 * (nx0 + (nx1 - nx0) * v);
}

double Noise::fbm(double x, double y, int octaves) const
{
    double sum = 0.0;
    double amplitude = 1.0;
    double norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * gradient(x, y);
        norm += amplitude;
        // Offset each octave so lattice points do not line up.
        x = x * 2.0 + 17.31;
        y = y * 2.0 - 9.73;
        amplitude *= 0.5;
    }
    return sum / norm;
}

} // namespace emerald
