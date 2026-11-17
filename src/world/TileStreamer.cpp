#include "world/TileStreamer.hpp"

#include "world/HeightField.hpp"

#include <algorithm>
#include <utility>

namespace emerald {

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
