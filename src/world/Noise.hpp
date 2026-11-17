#pragma once

#include <cstdint>

namespace emerald {

// Seeded 2D gradient noise and fractal sums, deterministic across platforms (integer hashing,
// no global state). Values are roughly in [-1, 1].
class Noise {
public:
    // Value and analytic partial derivatives.
    struct Sample {
        double value;
        double dx;
        double dy;
    };

    explicit Noise(std::uint32_t seed) : m_seed(seed) {}

    double gradient(double x, double y) const;
    Sample gradientWithDerivatives(double x, double y) const;

    // Fractal Brownian motion: octaves of gradient noise, frequency x2 and amplitude x0.5 each.
    double fbm(double x, double y, int octaves) const;

    // fBm whose octaves are damped by the slope accumulated so far: steep ground gets gullies,
    // flat ground stays smooth, like erosion. Roughly [-1, 1].
    double erodedFbm(double x, double y, int octaves) const;

    // Ridged multifractal: octaves folded into sharp crests (1 - |n|)^2, each weighted by the
    // previous one so ridges branch. Roughly [0, 1].
    double ridged(double x, double y, int octaves) const;

    std::uint32_t seed() const { return m_seed; }

private:
    std::uint32_t m_seed;
};

} // namespace emerald
