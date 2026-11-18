#include "world/TileStreamer.hpp"

#include "world/HeightField.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace emerald {

namespace {

// Heights around the node for the horizon search: a coarse grid of kContextSamples at an eighth
// of the node size, reaching two node widths past every edge.
constexpr int kContextSamples = 41;
constexpr double kContextSpacing = 1.0 / 8.0; // in node widths
constexpr double kContextMargin = 2.0;        // node widths
constexpr double kStepGrowth = 1.2;           // each march step 20% longer than the last

double bilinear(const std::vector<float>& grid, int size, double x, double z)
{
    const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, size - 2);
    const int z0 = std::clamp(static_cast<int>(std::floor(z)), 0, size - 2);
    const double fx = x - x0;
    const double fz = z - z0;
    const auto at = [&](int i, int j) {
        return static_cast<double>(grid[static_cast<std::size_t>(j) * static_cast<std::size_t>(size) +
                                        static_cast<std::size_t>(i)]);
    };
    return (at(x0, z0) * (1 - fx) + at(x0 + 1, z0) * fx) * (1 - fz) +
           (at(x0, z0 + 1) * (1 - fx) + at(x0 + 1, z0 + 1) * fx) * fz;
}

// Marches outwards from every other grid vertex in each direction, using the tile's own heights
// inside the node and the coarse context grid beyond it, and keeps the steepest elevation angle.
std::vector<unsigned char> computeHorizon(const HeightField& heightField, const TileKey& key,
                                          const std::vector<float>& heights)
{
    const double size = nodeSize(key.level);
    const double spacing = size / kTileQuads;
    const double originX = static_cast<double>(key.x) * size;
    const double originZ = static_cast<double>(key.z) * size;

    std::vector<float> context(static_cast<std::size_t>(kContextSamples) * kContextSamples);
    const double contextSpacing = size * kContextSpacing;
    const double contextX = originX - kContextMargin * size;
    const double contextZ = originZ - kContextMargin * size;
    float minY = 0.0f;
    float maxY = 0.0f;
    heightField.fillTile(contextX, contextZ, contextSpacing, kContextSamples, context.data(), minY, maxY);

    const auto heightAt = [&](double x, double z) {
        const double gx = (x - originX) / spacing;
        const double gz = (z - originZ) / spacing;
        if (gx >= 0.0 && gx <= kTileQuads && gz >= 0.0 && gz <= kTileQuads) {
            return bilinear(heights, kTileSamples, gx + kTileBorder, gz + kTileBorder);
        }
        return bilinear(context, kContextSamples, (x - contextX) / contextSpacing,
                        (z - contextZ) / contextSpacing);
    };

    const double maxDistance = kContextMargin * size;
    const std::size_t layerSize = static_cast<std::size_t>(kHorizonSize) * kHorizonSize * 4;
    std::vector<unsigned char> horizon(layerSize * 2);
    for (int dir = 0; dir < kHorizonDirections; ++dir) {
        const double bearing = dir * (2.0 * 3.14159265358979323846 / kHorizonDirections);
        const double dx = std::sin(bearing);
        const double dz = -std::cos(bearing); // north is -Z
        for (int v = 0; v < kHorizonSize; ++v) {
            for (int u = 0; u < kHorizonSize; ++u) {
                const double x = originX + 2 * u * spacing;
                const double z = originZ + 2 * v * spacing;
                const double h0 = heights[static_cast<std::size_t>(2 * v + kTileBorder) * kTileSamples +
                                          static_cast<std::size_t>(2 * u + kTileBorder)];
                double steepest = 0.0;
                for (double t = spacing; t <= maxDistance; t *= kStepGrowth) {
                    steepest = std::max(steepest, (heightAt(x + dx * t, z + dz * t) - h0) / t);
                }
                const double angle = std::atan(steepest) / (0.5 * 3.14159265358979323846);
                const std::size_t texel = (static_cast<std::size_t>(v) * kHorizonSize + static_cast<std::size_t>(u)) * 4;
                horizon[static_cast<std::size_t>(dir / 4) * layerSize + texel + static_cast<std::size_t>(dir % 4)] =
                    static_cast<unsigned char>(std::lround(std::clamp(angle, 0.0, 1.0) * 255.0));
            }
        }
    }
    return horizon;
}

} // namespace

TileStreamer::TileStreamer(const HeightField& heightField)
    : m_heightField(heightField)
    , m_thread([this] { run(); })
{
}

TileStreamer::~TileStreamer()
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

TileData TileStreamer::generate(const HeightField& heightField, const TileKey& key)
{
    const double size = nodeSize(key.level);
    const double spacing = size / kTileQuads;
    TileData tile;
    tile.key = key;
    tile.heights.resize(static_cast<std::size_t>(kTileSamples) * static_cast<std::size_t>(kTileSamples));
    heightField.fillTile(static_cast<double>(key.x) * size - kTileBorder * spacing,
                         static_cast<double>(key.z) * size - kTileBorder * spacing, spacing,
                         kTileSamples, tile.heights.data(), tile.minY, tile.maxY);
    if (key.level >= kHorizonMinLevel) {
        tile.horizon = computeHorizon(heightField, key, tile.heights);
    }
    return tile;
}

void TileStreamer::setRequests(std::vector<Request> requests)
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        // Skip tiles already being made or finished but not yet collected, so none is built twice.
        requests.erase(std::remove_if(requests.begin(), requests.end(),
                                      [this](const Request& r) {
                                          if (m_busy && r.key == m_inProgress) {
                                              return true;
                                          }
                                          return std::any_of(m_completed.begin(), m_completed.end(),
                                                             [&r](const TileData& t) { return t.key == r.key; });
                                      }),
                       requests.end());
        m_requests = std::move(requests);
    }
    m_wake.notify_one();
}

std::vector<TileData> TileStreamer::takeCompleted(std::size_t maxCount)
{
    std::vector<TileData> out;
    const std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_completed.empty() && out.size() < maxCount) {
        out.push_back(std::move(m_completed.front()));
        m_completed.pop_front();
    }
    return out;
}

std::size_t TileStreamer::outstanding() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_requests.size() + (m_busy ? 1u : 0u) + m_completed.size();
}

std::uint64_t TileStreamer::generatedCount() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_generated;
}

void TileStreamer::run()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [this] { return m_stop || !m_requests.empty(); });
        if (m_stop) {
            return;
        }
        // Most urgent request first; the list is rewritten every frame, so a linear scan is fine.
        const auto best = std::min_element(m_requests.begin(), m_requests.end(),
                                           [](const Request& a, const Request& b) {
                                               return a.priority < b.priority;
                                           });
        const TileKey key = best->key;
        m_requests.erase(best);
        m_inProgress = key;
        m_busy = true;

        lock.unlock();
        TileData tile = generate(m_heightField, key);
        lock.lock();

        m_busy = false;
        ++m_generated;
        m_completed.push_back(std::move(tile));
    }
}

} // namespace emerald
