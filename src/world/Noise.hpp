#pragma once

#include <cstdint>

namespace emerald {

// Seeded 2D gradient noise and fractal sums, deterministic across platforms (integer hashing,
// no global state). Values are roughly in [-1, 1].
class Noise {
public:
    explicit Noise(std::uint32_t seed) : m_seed(seed) {}

    double gradient(double x, double y) const;

    // Fractal Brownian motion: octaves of gradient noise, frequency x2 and amplitude x0.5 each.
    double fbm(double x, double y, int octaves) const;

    std::uint32_t seed() const { return m_seed; }

private:
    std::uint32_t m_seed;
};

} // namespace emerald
