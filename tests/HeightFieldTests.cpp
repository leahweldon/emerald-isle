#include "world/HeightField.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace emerald;

namespace {

const std::filesystem::path kDem = std::filesystem::path(EMERALD_TEST_ASSETS) / "textures" / "ireland-heightmap.png";

std::unique_ptr<HeightField> makeField(std::uint32_t seed)
{
    HeightFieldConfig config;
    config.seed = seed;
    return std::make_unique<HeightField>(kDem, config);
}

// Procedural world well outside the DEM and its clear ring.
double proceduralX(const HeightField& field, int i)
{
    return field.demHalfWidth() + 400e3 + i * 1730.0;
}

} // namespace

TEST_CASE("Procedural terrain: the same seed gives identical heights")
{
    const auto a = makeField(7);
    const auto b = makeField(7);
    for (int i = 0; i < 200; ++i) {
        const double x = proceduralX(*a, i);
        const double z = -300e3 + i * 2910.0;
        CHECK(a->heightAt(x, z) == b->heightAt(x, z));
    }
}

TEST_CASE("Procedural terrain: different seeds give different islands")
{
    const auto a = makeField(1);
    const auto b = makeField(2);
    int different = 0;
    for (int i = 0; i < 200; ++i) {
        const double x = proceduralX(*a, i);
        const double z = -300e3 + i * 2910.0;
        different += std::abs(a->heightAt(x, z) - b->heightAt(x, z)) > 1.0 ? 1 : 0;
    }
    CHECK(different > 150);
}

TEST_CASE("Procedural terrain: the seed does not change the DEM")
{
    const auto a = makeField(1);
    const auto b = makeField(2);
    // Well inside the DEM, away from its blend band.
    for (int i = -10; i <= 10; ++i) {
        CHECK(a->heightAt(i * 5000.0, i * 7000.0) == b->heightAt(i * 5000.0, i * 7000.0));
    }
}

TEST_CASE("DEM border: no step where the DEM meets the procedural world")
{
    // Walks across each edge of the DEM rectangle in 50 m steps, along the whole edge, and checks
    // the largest height change between neighbouring samples. The edges lie in the sea with
    // procedural land kept away from them, so the seabed should be smooth there; a seam between
    // the two sources would show as a step.
    const auto field = makeField(1);
    const double w = field->demHalfWidth();
    const double d = field->demHalfDepth();
    constexpr double kStep = 50.0;
    constexpr double kMaxStep = 5.0; // metres (exaggerated) per 50 m
    double worst = 0.0;
    for (double t = -1.0; t <= 1.0; t += 0.01) {
        for (double s = -2000.0; s < 2000.0; s += kStep) {
            const double ex = t * w;
            const double ez = t * d;
            worst = std::max(worst, std::abs(field->heightAt(w + s + kStep, ez) - field->heightAt(w + s, ez)));
            worst = std::max(worst, std::abs(field->heightAt(-w - s - kStep, ez) - field->heightAt(-w - s, ez)));
            worst = std::max(worst, std::abs(field->heightAt(ex, d + s + kStep) - field->heightAt(ex, d + s)));
            worst = std::max(worst, std::abs(field->heightAt(ex, -d - s - kStep) - field->heightAt(ex, -d - s)));
        }
    }
    MESSAGE("largest step across the DEM border: " << worst << " m per " << kStep << " m");
    CHECK(worst < kMaxStep);
}

TEST_CASE("Ground height never goes below the sea")
{
    const auto field = makeField(1);
    for (int i = 0; i < 100; ++i) {
        const double x = -800e3 + i * 16e3;
        CHECK(field->groundHeight(x, 0.0) >= 0.0);
        CHECK(field->groundHeight(x, 0.0) >= field->heightAt(x, 0.0));
    }
}
