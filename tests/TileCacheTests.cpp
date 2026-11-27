#include "world/TileCache.hpp"

#include <doctest/doctest.h>

#include <optional>

using namespace emerald;

namespace {

TileKey key(std::int64_t x)
{
    return TileKey{0, x, 0};
}

} // namespace

TEST_CASE("Tile cache: free slots first, then the least recently used")
{
    TileCache cache(3);
    std::optional<TileKey> evicted;

    cache.beginFrame(); // 1
    CHECK(cache.insert(key(1), 0.0f, 1.0f, &evicted) == 0);
    CHECK_FALSE(evicted.has_value());
    cache.beginFrame(); // 2
    CHECK(cache.insert(key(2), 0.0f, 1.0f) == 1);
    cache.beginFrame(); // 3
    CHECK(cache.insert(key(3), 0.0f, 1.0f) == 2);
    CHECK(cache.size() == 3);

    // Tile 1 is used again, so tile 2 is now the oldest.
    cache.beginFrame(); // 4
    CHECK(cache.use(key(1)) == 0);
    CHECK(cache.insert(key(4), 0.0f, 1.0f, &evicted) == 1);
    REQUIRE(evicted.has_value());
    CHECK(*evicted == key(2));
    CHECK_FALSE(cache.contains(key(2)));

    // Then tile 3; tiles 1 and 4 were used this frame and are kept.
    CHECK(cache.insert(key(5), 0.0f, 1.0f, &evicted) == 2);
    REQUIRE(evicted.has_value());
    CHECK(*evicted == key(3));

    // Every slot used this frame: nothing to evict.
    CHECK(cache.insert(key(6), 0.0f, 1.0f, &evicted) == -1);
    CHECK_FALSE(evicted.has_value());
    CHECK(cache.contains(key(1)));
    CHECK(cache.contains(key(4)));
    CHECK(cache.contains(key(5)));

    // Next frame: tiles 1 and 4 were both last used in frame 4; the tie goes to the lower slot.
    cache.beginFrame(); // 5
    CHECK(cache.use(key(5)) == 2);
    CHECK(cache.insert(key(6), 0.0f, 1.0f, &evicted) == 0);
    REQUIRE(evicted.has_value());
    CHECK(*evicted == key(1));
}

TEST_CASE("Tile cache: a resident tile is not inserted twice, and keeps its height range")
{
    TileCache cache(2);
    cache.beginFrame();
    const int slot = cache.insert(key(7), -5.0f, 250.0f);
    REQUIRE(slot >= 0);
    CHECK(cache.insert(key(7), 0.0f, 1.0f) == -1);
    CHECK(cache.size() == 1);
    CHECK(cache.entry(slot).minY == -5.0f);
    CHECK(cache.entry(slot).maxY == 250.0f);
    CHECK(cache.use(key(8)) == -1);
}
