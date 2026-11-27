#include "render/Camera.hpp"
#include "world/Cdlod.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace emerald;

namespace {

struct View {
    glm::dvec3 position;
    glm::mat4 viewProjection;
};

// 300 m up, looking north and slightly down, 16:9.
View lowView(const glm::dvec3& at)
{
    Camera camera;
    camera.position = at + glm::dvec3(0.0, 300.0, 0.0);
    camera.pitch = -0.1f;
    camera.farPlane = 2.0e6f;
    return {camera.position, camera.projection(16.0f / 9.0f) * camera.view()};
}

// Selects repeatedly, loading every requested tile at once (flat, 0 to 100 m), until nothing more
// is requested: what the renderer draws once streaming has caught up.
CdlodSelection settle(TileCache& cache, const View& view, const CdlodSettings& settings = {})
{
    CdlodSelection selection;
    for (int pass = 0; pass < 64; ++pass) {
        cache.beginFrame();
        selection = selectCdlod(cache, view.position, view.viewProjection, settings);
        if (selection.requests.empty()) {
            break;
        }
        for (const TileStreamer::Request& request : selection.requests) {
            cache.insert(request.key, 0.0f, 100.0f);
        }
    }
    return selection;
}

} // namespace

TEST_CASE("CDLOD: streaming settles with every visible node drawn")
{
    TileCache cache(1024);
    const CdlodSelection selection = settle(cache, lowView({12e3, 0.0, -7e3}));
    CHECK(selection.requests.empty());
    CHECK(selection.missingNodes == 0);
    CHECK_FALSE(selection.nodes.empty());
}

TEST_CASE("CDLOD: nodes near the camera are finer than far ones")
{
    TileCache cache(1024);
    const CdlodSettings settings;
    const CdlodSelection selection = settle(cache, lowView({12e3, 0.0, -7e3}), settings);
    REQUIRE_FALSE(selection.nodes.empty());

    const auto nearest = std::min_element(selection.nodes.begin(), selection.nodes.end(),
                                          [](const CdlodNode& a, const CdlodNode& b) { return a.distance < b.distance; });
    const auto farthest = std::max_element(selection.nodes.begin(), selection.nodes.end(),
                                           [](const CdlodNode& a, const CdlodNode& b) { return a.distance < b.distance; });
    CHECK(nearest->key.level == 0);
    CHECK(farthest->key.level >= 4);

    // A node is only drawn instead of its children when the camera is outside their split range,
    // and the coarser a level, the further away its nearest node.
    int previousLevel = -1;
    double previousNearest = -1.0;
    for (int level = 0; level <= kMaxLevel; ++level) {
        double nearestOfLevel = -1.0;
        for (const CdlodNode& node : selection.nodes) {
            if (node.key.level != level) {
                continue;
            }
            if (level > 0) {
                CHECK(node.distance >= lodRange(level - 1, settings.rangeFactor));
            }
            if (nearestOfLevel < 0.0 || node.distance < nearestOfLevel) {
                nearestOfLevel = node.distance;
            }
        }
        if (nearestOfLevel >= 0.0) {
            if (previousLevel >= 0) {
                CHECK(nearestOfLevel > previousNearest);
            }
            previousLevel = level;
            previousNearest = nearestOfLevel;
        }
    }
}

TEST_CASE("CDLOD: a node waits for all four children before splitting")
{
    TileCache cache(1024);
    const View view = lowView({12e3, 0.0, -7e3});
    settle(cache, view);
    // Drop one leaf under the camera: its parent is drawn again, not a hole.
    TileCache partial(1024);
    cache.beginFrame();
    const CdlodSelection full = selectCdlod(cache, view.position, view.viewProjection, {});
    // The nearest finest node.
    auto leaf = full.nodes.end();
    for (auto it = full.nodes.begin(); it != full.nodes.end(); ++it) {
        if (it->key.level == 0 && (leaf == full.nodes.end() || it->distance < leaf->distance)) {
            leaf = it;
        }
    }
    REQUIRE(leaf != full.nodes.end());
    for (int slot = 0; slot < cache.capacity(); ++slot) {
        const TileCache::Entry& e = cache.entry(slot);
        if (e.occupied && !(e.key == leaf->key)) {
            partial.insert(e.key, e.minY, e.maxY);
        }
    }
    partial.beginFrame();
    const CdlodSelection selection = selectCdlod(partial, view.position, view.viewProjection, {});
    const TileKey parent{1, leaf->key.x >> 1, leaf->key.z >> 1};
    CHECK(std::any_of(selection.nodes.begin(), selection.nodes.end(), [&](const CdlodNode& n) { return n.key == parent; }));
    CHECK(std::none_of(selection.nodes.begin(), selection.nodes.end(), [&](const CdlodNode& n) {
        return n.key.level == 0 && (n.key.x >> 1) == parent.x && (n.key.z >> 1) == parent.z;
    }));
    CHECK(std::any_of(selection.requests.begin(), selection.requests.end(),
                      [&](const TileStreamer::Request& r) { return r.key == leaf->key; }));
    CHECK(selection.missingNodes == 0);
}

TEST_CASE("CDLOD: selection is deterministic")
{
    const View view = lowView({-150e3, 0.0, 220e3});
    TileCache a(1024);
    TileCache b(1024);
    const CdlodSelection first = settle(a, view);
    const CdlodSelection second = settle(b, view);
    REQUIRE(first.nodes.size() == second.nodes.size());
    for (std::size_t i = 0; i < first.nodes.size(); ++i) {
        CHECK(first.nodes[i].key == second.nodes[i].key);
        CHECK(first.nodes[i].slot == second.nodes[i].slot);
        CHECK(first.nodes[i].distance == second.nodes[i].distance);
    }
    // And again from the same cache.
    a.beginFrame();
    const CdlodSelection again = selectCdlod(a, view.position, view.viewProjection, {});
    REQUIRE(again.nodes.size() == first.nodes.size());
    for (std::size_t i = 0; i < first.nodes.size(); ++i) {
        CHECK(again.nodes[i].key == first.nodes[i].key);
    }
}

TEST_CASE("CDLOD: nothing behind the camera is selected")
{
    TileCache cache(1024);
    const View view = lowView({0.0, 0.0, 0.0});
    const CdlodSelection selection = settle(cache, view);
    for (const CdlodNode& node : selection.nodes) {
        const double size = nodeSize(node.key.level);
        // Looking north (-Z): every drawn node reaches north of the camera's position (with some
        // slack for the frustum's width near the camera).
        CHECK(static_cast<double>(node.key.z) * size < view.position.z + 1.0);
    }
}
