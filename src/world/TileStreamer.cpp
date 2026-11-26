#include "world/TileStreamer.hpp"

#include "world/HeightField.hpp"

#include <algorithm>
#include <chrono>
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

} // namespace

// Builds one tile in small steps (a row of heights, a row of the horizon context or one horizon
// direction), so generation on the main thread can stop when its frame budget is spent.
class TileJob {
public:
    TileJob(const HeightField& heightField, const TileKey& key)
        : m_heightField(heightField)
        , m_size(nodeSize(key.level))
        , m_spacing(m_size / kTileQuads)
        , m_originX(static_cast<double>(key.x) * m_size)
        , m_originZ(static_cast<double>(key.z) * m_size)
    {
        m_tile.key = key;
        m_tile.heights.resize(static_cast<std::size_t>(kTileSamples) * static_cast<std::size_t>(kTileSamples));
        m_tile.minY = 1e30f;
        m_tile.maxY = -1e30f;
    }

    const TileKey& key() const { return m_tile.key; }

    // Does one step; true once the tile is complete.
    bool step()
    {
        switch (m_phase) {
        case Phase::Heights:
            heightRow();
            if (++m_row == kTileSamples) {
                m_row = 0;
                m_phase = m_tile.key.level >= kHorizonMinLevel ? Phase::Context : Phase::Done;
            }
            break;
        case Phase::Context:
            contextRow();
            if (++m_row == kContextSamples) {
                m_tile.horizon.resize(kLayerSize * 2);
                m_phase = Phase::Horizon;
            }
            break;
        case Phase::Horizon:
            horizonDirection();
            if (++m_direction == kHorizonDirections) {
                m_phase = Phase::Done;
            }
            break;
        case Phase::Done:
            break;
        }
        return m_phase == Phase::Done;
    }

    TileData take() { return std::move(m_tile); }

private:
    enum class Phase { Heights, Context, Horizon, Done };
    static constexpr std::size_t kLayerSize = static_cast<std::size_t>(kHorizonSize) * kHorizonSize * 4;

    void heightRow()
    {
        const double x0 = m_originX - kTileBorder * m_spacing;
        const double z0 = m_originZ - kTileBorder * m_spacing;
        for (int i = 0; i < kTileSamples; ++i) {
            const auto h = static_cast<float>(m_heightField.heightAt(x0 + i * m_spacing, z0 + m_row * m_spacing));
            m_tile.heights[static_cast<std::size_t>(m_row) * kTileSamples + static_cast<std::size_t>(i)] = h;
            m_tile.minY = std::min(m_tile.minY, h);
            m_tile.maxY = std::max(m_tile.maxY, h);
        }
    }

    // Heights around the node for the horizon search: a coarse grid of kContextSamples at an
    // eighth of the node size, reaching two node widths past every edge.
    void contextRow()
    {
        if (m_context.empty()) {
            m_context.resize(static_cast<std::size_t>(kContextSamples) * kContextSamples);
        }
        for (int i = 0; i < kContextSamples; ++i) {
            m_context[static_cast<std::size_t>(m_row) * kContextSamples + static_cast<std::size_t>(i)] =
                static_cast<float>(m_heightField.heightAt(contextX() + i * contextSpacing(),
                                                          contextZ() + m_row * contextSpacing()));
        }
    }

    double contextSpacing() const { return m_size * kContextSpacing; }
    double contextX() const { return m_originX - kContextMargin * m_size; }
    double contextZ() const { return m_originZ - kContextMargin * m_size; }

    // Marches outwards from every other grid vertex in one direction, using the tile's own heights
    // inside the node and the coarse context grid beyond it, and keeps the steepest elevation angle.
    void horizonDirection()
    {
        const auto heightAt = [&](double x, double z) {
            const double gx = (x - m_originX) / m_spacing;
            const double gz = (z - m_originZ) / m_spacing;
            if (gx >= 0.0 && gx <= kTileQuads && gz >= 0.0 && gz <= kTileQuads) {
                return bilinear(m_tile.heights, kTileSamples, gx + kTileBorder, gz + kTileBorder);
            }
            return bilinear(m_context, kContextSamples, (x - contextX()) / contextSpacing(),
                            (z - contextZ()) / contextSpacing());
        };
        const double maxDistance = kContextMargin * m_size;
        const int dir = m_direction;
        const double bearing = dir * (2.0 * 3.14159265358979323846 / kHorizonDirections);
        const double dx = std::sin(bearing);
        const double dz = -std::cos(bearing); // north is -Z
        for (int v = 0; v < kHorizonSize; ++v) {
            for (int u = 0; u < kHorizonSize; ++u) {
                const double x = m_originX + 2 * u * m_spacing;
                const double z = m_originZ + 2 * v * m_spacing;
                const double h0 = m_tile.heights[static_cast<std::size_t>(2 * v + kTileBorder) * kTileSamples +
                                                 static_cast<std::size_t>(2 * u + kTileBorder)];
                double steepest = 0.0;
                for (double t = m_spacing; t <= maxDistance; t *= kStepGrowth) {
                    steepest = std::max(steepest, (heightAt(x + dx * t, z + dz * t) - h0) / t);
                }
                const double angle = std::atan(steepest) / (0.5 * 3.14159265358979323846);
                const std::size_t texel = (static_cast<std::size_t>(v) * kHorizonSize + static_cast<std::size_t>(u)) * 4;
                m_tile.horizon[static_cast<std::size_t>(dir / 4) * kLayerSize + texel + static_cast<std::size_t>(dir % 4)] =
                    static_cast<unsigned char>(std::lround(std::clamp(angle, 0.0, 1.0) * 255.0));
            }
        }
    }

    const HeightField& m_heightField;
    double m_size;
    double m_spacing;
    double m_originX;
    double m_originZ;
    TileData m_tile;
    std::vector<float> m_context;
    Phase m_phase = Phase::Heights;
    int m_row = 0;
    int m_direction = 0;
};

TileStreamer::TileStreamer(const HeightField& heightField)
    : m_heightField(heightField)
#if EMERALD_THREADS
    , m_thread([this] { run(); })
#endif
{
}

TileStreamer::~TileStreamer()
{
#if EMERALD_THREADS
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
#endif
}

TileData TileStreamer::generate(const HeightField& heightField, const TileKey& key)
{
    TileJob job(heightField, key);
    while (!job.step()) {
    }
    return job.take();
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

bool TileStreamer::generateNext(std::unique_lock<std::mutex>& lock)
{
    if (m_requests.empty()) {
        return false;
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
    return true;
}

void TileStreamer::run()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [this] { return m_stop || !m_requests.empty(); });
        if (m_stop) {
            return;
        }
        generateNext(lock);
    }
}

void TileStreamer::pump([[maybe_unused]] double budgetMs)
{
#if !EMERALD_THREADS
    // Works through the current job in steps, starting the most urgent request when it finishes,
    // until the budget is spent; an unfinished job carries over to the next frame.
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const std::lock_guard<std::mutex> lock(m_mutex);
    while (std::chrono::duration<double, std::milli>(Clock::now() - start).count() < budgetMs) {
        if (!m_job) {
            if (m_requests.empty()) {
                return;
            }
            const auto best = std::min_element(m_requests.begin(), m_requests.end(),
                                               [](const Request& a, const Request& b) {
                                                   return a.priority < b.priority;
                                               });
            m_job = std::make_unique<TileJob>(m_heightField, best->key);
            m_requests.erase(best);
            m_inProgress = m_job->key();
            m_busy = true;
        }
        if (m_job->step()) {
            m_completed.push_back(m_job->take());
            m_job.reset();
            m_busy = false;
            ++m_generated;
        }
    }
#endif
}

} // namespace emerald
