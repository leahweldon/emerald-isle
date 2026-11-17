#include "world/Noise.hpp"

#include <algorithm>
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

// Unit gradient for a hash, one of 16 evenly spaced directions.
void gradientVector(std::uint32_t h, double& gx, double& gy)
{
    constexpr double kStep = 6.283185307179586 / 16.0;
    const double angle = static_cast<double>(h & 15u) * kStep;
    gx = std::cos(angle);
    gy = std::sin(angle);
}

double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double fadeDerivative(double t)
{
    return 30.0 * t * t * (t - 1.0) * (t - 1.0);
}

// Moves to the next octave's coordinates: double frequency, offset so lattices do not line up.
void nextOctave(double& x, double& y)
{
    x = x * 2.0 + 17.31;
    y = y * 2.0 - 9.73;
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

Noise::Sample Noise::gradientWithDerivatives(double x, double y) const
{
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const auto ix = static_cast<std::int64_t>(fx);
    const auto iy = static_cast<std::int64_t>(fy);
    const double dx = x - fx;
    const double dy = y - fy;

    double g00x, g00y, g10x, g10y, g01x, g01y, g11x, g11y;
    gradientVector(hash(ix, iy, m_seed), g00x, g00y);
    gradientVector(hash(ix + 1, iy, m_seed), g10x, g10y);
    gradientVector(hash(ix, iy + 1, m_seed), g01x, g01y);
    gradientVector(hash(ix + 1, iy + 1, m_seed), g11x, g11y);
    const double n00 = g00x * dx + g00y * dy;
    const double n10 = g10x * (dx - 1.0) + g10y * dy;
    const double n01 = g01x * dx + g01y * (dy - 1.0);
    const double n11 = g11x * (dx - 1.0) + g11y * (dy - 1.0);

    const double u = fade(dx);
    const double v = fade(dy);
    const double du = fadeDerivative(dx);
    const double dv = fadeDerivative(dy);
    const double k = n00 - n10 - n01 + n11;

    // n = n00 + u(n10 - n00) + v(n01 - n00) + uv k, differentiated with the corner gradients.
    const double value = n00 + u * (n10 - n00) + v * (n01 - n00) + u * v * k;
    const double ddx = g00x + u * (g10x - g00x) + v * (g01x - g00x) + u * v * (g00x - g10x - g01x + g11x) +
                       du * ((n10 - n00) + v * k);
    const double ddy = g00y + u * (g10y - g00y) + v * (g01y - g00y) + u * v * (g00y - g10y - g01y + g11y) +
                       dv * ((n01 - n00) + u * k);
    return {1.4 * value, 1.4 * ddx, 1.4 * ddy};
}

double Noise::erodedFbm(double x, double y, int octaves) const
{
    double sum = 0.0;
    double amplitude = 1.0;
    double norm = 0.0;
    double slopeX = 0.0;
    double slopeY = 0.0;
    for (int i = 0; i < octaves; ++i) {
        const Sample s = gradientWithDerivatives(x, y);
        slopeX += s.dx;
        slopeY += s.dy;
        sum += amplitude * s.value / (1.0 + slopeX * slopeX + slopeY * slopeY);
        norm += amplitude;
        nextOctave(x, y);
        amplitude *= 0.5;
    }
    return sum / norm;
}

double Noise::ridged(double x, double y, int octaves) const
{
    double sum = 0.0;
    double amplitude = 1.0;
    double norm = 0.0;
    double weight = 1.0;
    for (int i = 0; i < octaves; ++i) {
        double crest = 1.0 - std::abs(gradient(x, y));
        crest *= crest * weight;
        weight = std::clamp(crest * 2.0, 0.0, 1.0);
        sum += amplitude * crest;
        norm += amplitude;
        nextOctave(x, y);
        amplitude *= 0.5;
    }
    return sum / norm;
}

double Noise::fbm(double x, double y, int octaves) const
{
    double sum = 0.0;
    double amplitude = 1.0;
    double norm = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * gradient(x, y);
        norm += amplitude;
        nextOctave(x, y);
        amplitude *= 0.5;
    }
    return sum / norm;
}

} // namespace emerald
