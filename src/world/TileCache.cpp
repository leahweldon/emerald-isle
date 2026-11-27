#include "world/TileCache.hpp"

#include <limits>

namespace emerald {

TileCache::TileCache(int capacity)
    : m_slots(static_cast<std::size_t>(capacity))
{
}

int TileCache::use(const TileKey& key)
{
    const auto it = m_lookup.find(key);
    if (it == m_lookup.end()) {
        return -1;
    }
    m_slots[static_cast<std::size_t>(it->second)].lastUsed = m_frame;
    return it->second;
}

int TileCache::insert(const TileKey& key, float minY, float maxY, std::optional<TileKey>* evicted)
{
    if (evicted) {
        evicted->reset();
    }
    if (contains(key)) {
        return -1;
    }
    int chosen = -1;
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (int i = 0; i < capacity(); ++i) {
        const Entry& slot = m_slots[static_cast<std::size_t>(i)];
        if (!slot.occupied) {
            chosen = i;
            break;
        }
        if (slot.lastUsed < m_frame && slot.lastUsed < oldest) {
            oldest = slot.lastUsed;
            chosen = i;
        }
    }
    if (chosen < 0) {
        return -1;
    }
    Entry& slot = m_slots[static_cast<std::size_t>(chosen)];
    if (slot.occupied) {
        m_lookup.erase(slot.key);
        if (evicted) {
            *evicted = slot.key;
        }
    }
    slot = Entry{key, minY, maxY, m_frame, true};
    m_lookup[key] = chosen;
    return chosen;
}

} // namespace emerald
