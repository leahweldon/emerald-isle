#pragma once

#include "world/TileStreamer.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace emerald {

// Resident terrain tiles: a fixed pool of slots keyed by TileKey, with each tile's height range
// and the frame it was last used. When full, a new tile replaces the least recently used one,
// never one used in the current frame. Slot indices are stable, so the renderer can keep its GPU
// textures in a parallel array.
class TileCache {
public:
    struct Entry {
        TileKey key;
        float minY = 0.0f;
        float maxY = 0.0f;
        std::uint64_t lastUsed = 0;
        bool occupied = false;
    };

    explicit TileCache(int capacity);

    // Starts a frame: tiles used from now on are kept until the next one.
    void beginFrame() { ++m_frame; }
    std::uint64_t frame() const { return m_frame; }

    // Slot of a resident tile, marking it used this frame, or -1.
    int use(const TileKey& key);
    bool contains(const TileKey& key) const { return m_lookup.count(key) != 0; }

    // Stores a tile in the first free slot, or else in the least recently used slot not used this
    // frame (whose tile is then evicted and returned in evicted). Returns the slot, or -1 when the
    // tile is already resident or every slot was used this frame.
    int insert(const TileKey& key, float minY, float maxY, std::optional<TileKey>* evicted = nullptr);

    const Entry& entry(int slot) const { return m_slots[static_cast<std::size_t>(slot)]; }
    int capacity() const { return static_cast<int>(m_slots.size()); }
    std::size_t size() const { return m_lookup.size(); }

private:
    std::vector<Entry> m_slots;
    std::unordered_map<TileKey, int, TileKeyHash> m_lookup;
    std::uint64_t m_frame = 0;
};

} // namespace emerald
