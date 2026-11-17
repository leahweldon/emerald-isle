#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace emerald {

class HeightField;

// Terrain quadtree layout shared by the streamer and the renderer. Level 0 is the finest; node
// (level, x, z) covers [x, x + 1) * size by [z, z + 1) * size metres, size = kLeafSize * 2^level.
constexpr double kLeafSize = 4096.0;
constexpr int kMaxLevel = 7;            // roots are 524 km across
constexpr int kTileQuads = 64;          // grid quads per node side
constexpr int kTileBorder = 1;          // extra samples each side, for normals
constexpr int kTileSamples = kTileQuads + 1 + 2 * kTileBorder; // 67

inline double nodeSize(int level)
{
    return kLeafSize * static_cast<double>(1 << level);
}

struct TileKey {
    int level = 0;
    std::int64_t x = 0;
    std::int64_t z = 0;

    bool operator==(const TileKey& o) const { return level == o.level && x == o.x && z == o.z; }
};

struct TileKeyHash {
    std::size_t operator()(const TileKey& k) const
    {
        const auto h = static_cast<std::uint64_t>(k.x) * 0x9E3779B97F4A7C15ull ^
                       static_cast<std::uint64_t>(k.z) * 0xC2B2AE3D27D4EB4Full ^
                       static_cast<std::uint64_t>(k.level);
        return static_cast<std::size_t>(h ^ (h >> 29));
    }
};

struct TileData {
    TileKey key;
    std::vector<float> heights; // kTileSamples^2, row-major, first row at the node's -Z edge
    float minY = 0.0f;
    float maxY = 0.0f;
};

// Generates height tiles on one background thread. The main thread states which tiles it wants
// each frame (replacing the previous wish list, so stale requests are dropped) and collects
// finished tiles without ever blocking on generation.
class TileStreamer {
public:
    struct Request {
        TileKey key;
        float priority; // lower is generated sooner
    };

    explicit TileStreamer(const HeightField& heightField);
    ~TileStreamer();

    TileStreamer(const TileStreamer&) = delete;
    TileStreamer& operator=(const TileStreamer&) = delete;

    // Replaces the wish list. Tiles already being generated finish regardless.
    void setRequests(std::vector<Request> requests);
    // Up to maxCount finished tiles, oldest first. Never waits.
    std::vector<TileData> takeCompleted(std::size_t maxCount);

    // Tiles wanted but not finished, including the one in progress.
    std::size_t outstanding() const;
    std::uint64_t generatedCount() const;

    // Fills one tile synchronously (used by the worker; public for tests).
    static TileData generate(const HeightField& heightField, const TileKey& key);

private:
    void run();

    const HeightField& m_heightField;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::vector<Request> m_requests;
    std::deque<TileData> m_completed;
    bool m_busy = false;
    TileKey m_inProgress;
    bool m_stop = false;
    std::uint64_t m_generated = 0;
    std::thread m_thread;
};

} // namespace emerald
