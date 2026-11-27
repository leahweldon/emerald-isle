#include "world/Cdlod.hpp"

#include <cmath>

namespace emerald {

namespace {

// Levels this coarse (131 km nodes and up) are kept loaded even outside the view.
constexpr int kPrefetchLevel = kMaxLevel - 2;
// Requests for tiles outside the view come after every visible one.
constexpr double kPrefetchPriority = 1e9;
// Bounds for nodes whose tile is not loaded yet, so they are not culled by mistake.
constexpr float kUnknownMinY = -200.0f;
constexpr float kUnknownMaxY = 5000.0f;

std::int64_t floorDiv(double value, double size)
{
    return static_cast<std::int64_t>(std::floor(value / size));
}

TileKey child(const TileKey& key, int i)
{
    return TileKey{key.level - 1, key.x * 2 + (i & 1), key.z * 2 + (i >> 1)};
}

class Selector {
public:
    Selector(TileCache& cache, const glm::dvec3& cameraPos, const CdlodSettings& settings, CdlodSelection& out)
        : m_cache(cache)
        , m_cameraPos(cameraPos)
        , m_settings(settings)
        , m_out(out)
    {
    }

    void select(const TileKey& key, const glm::vec4 planes[6])
    {
        const double size = nodeSize(key.level);
        const int slot = m_cache.use(key);
        const float minY = slot >= 0 ? m_cache.entry(slot).minY : kUnknownMinY;
        const float maxY = slot >= 0 ? m_cache.entry(slot).maxY : kUnknownMaxY;
        const glm::dvec3 lo(static_cast<double>(key.x) * size, minY, static_cast<double>(key.z) * size);
        const glm::dvec3 hi = lo + glm::dvec3(size, static_cast<double>(maxY - minY), size);

        if (!boxVisible(planes, glm::vec3(lo - m_cameraPos), glm::vec3(hi - m_cameraPos))) {
            if (key.level >= kPrefetchLevel) {
                prefetch(key);
            }
            return;
        }
        const double distance = distanceToBox(m_cameraPos, lo, hi);
        if (slot < 0) {
            // Children are only selected once all four are loaded, so this is a visible root: a hole.
            ++m_out.missingNodes;
            m_out.requests.push_back({key, priority(key.level, distance, true)});
            return;
        }

        if (key.level > 0 && distance < lodRange(key.level - 1, m_settings.rangeFactor)) {
            TileKey children[4];
            bool ready = true;
            for (int i = 0; i < 4; ++i) {
                children[i] = child(key, i);
                if (m_cache.use(children[i]) < 0) {
                    ready = false;
                    const double childSize = nodeSize(key.level - 1);
                    const glm::dvec3 clo(static_cast<double>(children[i].x) * childSize, kUnknownMinY,
                                         static_cast<double>(children[i].z) * childSize);
                    const glm::dvec3 chi = clo + glm::dvec3(childSize, kUnknownMaxY - kUnknownMinY, childSize);
                    const bool visible = boxVisible(planes, glm::vec3(clo - m_cameraPos), glm::vec3(chi - m_cameraPos));
                    m_out.requests.push_back(
                        {children[i], priority(children[i].level, distanceToBox(m_cameraPos, clo, chi), visible)});
                }
            }
            if (ready) {
                for (const TileKey& c : children) {
                    select(c, planes);
                }
                return;
            }
            // Children still streaming: draw this coarser node meanwhile.
        }
        m_out.nodes.push_back(CdlodNode{key, slot, distance});
    }

private:
    // Keeps coarse tiles loaded outside the view, so turning reveals terrain rather than holes.
    void prefetch(const TileKey& key)
    {
        const double size = nodeSize(key.level);
        const glm::dvec3 lo(static_cast<double>(key.x) * size, kUnknownMinY, static_cast<double>(key.z) * size);
        const glm::dvec3 hi = lo + glm::dvec3(size, kUnknownMaxY - kUnknownMinY, size);
        const double distance = distanceToBox(m_cameraPos, lo, hi);
        if (m_cache.use(key) < 0) {
            m_out.requests.push_back(
                {key, static_cast<float>(kPrefetchPriority + (kMaxLevel - key.level) * 1e8 + distance)});
            return;
        }
        if (key.level > kPrefetchLevel && distance < lodRange(key.level - 1, m_settings.rangeFactor)) {
            for (int i = 0; i < 4; ++i) {
                prefetch(child(key, i));
            }
        }
    }

    // Missing roots are holes, so always first. Then coarse levels before fine ones, so the whole
    // view is covered early; or, on slow devices, nearest in view first (then off screen), so the
    // starting view fills in before the horizon.
    float priority(int level, double distance, bool visible) const
    {
        if (m_settings.nearFirst) {
            return static_cast<float>((level == kMaxLevel ? 0.0 : visible ? 1e8 : 5e8) + distance);
        }
        return static_cast<float>((kMaxLevel - level) * 1e8 + distance);
    }

    TileCache& m_cache;
    glm::dvec3 m_cameraPos;
    const CdlodSettings& m_settings;
    CdlodSelection& m_out;
};

void selectLoaded(TileCache& cache, const glm::dvec3& cameraPos, const TileKey& key, const glm::vec4 planes[6],
                  double rangeFactor, double lodScale, int skipPlane, std::vector<CdlodNode>& out)
{
    const int slot = cache.use(key);
    if (slot < 0) {
        return;
    }
    const TileCache::Entry& s = cache.entry(slot);
    const double size = nodeSize(key.level);
    const glm::dvec3 lo(static_cast<double>(key.x) * size, s.minY, static_cast<double>(key.z) * size);
    const glm::dvec3 hi = lo + glm::dvec3(size, static_cast<double>(s.maxY - s.minY), size);
    if (!boxVisible(planes, glm::vec3(lo - cameraPos), glm::vec3(hi - cameraPos), skipPlane)) {
        return;
    }
    const double distance = distanceToBox(cameraPos, lo, hi);
    if (key.level > 0 && distance < lodRange(key.level - 1, rangeFactor) * lodScale) {
        TileKey children[4];
        bool ready = true;
        for (int i = 0; i < 4; ++i) {
            children[i] = child(key, i);
            ready = ready && cache.contains(children[i]);
        }
        if (ready) {
            for (const TileKey& c : children) {
                selectLoaded(cache, cameraPos, c, planes, rangeFactor, lodScale, skipPlane, out);
            }
            return;
        }
    }
    out.push_back(CdlodNode{key, slot, distance});
}

} // namespace

void extractFrustumPlanes(const glm::mat4& m, glm::vec4 planes[6])
{
    const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);
    planes[0] = row3 + row0;
    planes[1] = row3 - row0;
    planes[2] = row3 + row1;
    planes[3] = row3 - row1;
    planes[4] = row3 + row2;
    planes[5] = row3 - row2;
}

bool boxVisible(const glm::vec4 planes[6], const glm::vec3& lo, const glm::vec3& hi, int skipPlane)
{
    for (int i = 0; i < 6; ++i) {
        if (i == skipPlane) {
            continue;
        }
        const glm::vec4& p = planes[i];
        // The corner furthest along the plane normal.
        const glm::vec3 v(p.x >= 0.0f ? hi.x : lo.x, p.y >= 0.0f ? hi.y : lo.y, p.z >= 0.0f ? hi.z : lo.z);
        if (p.x * v.x + p.y * v.y + p.z * v.z + p.w < 0.0f) {
            return false;
        }
    }
    return true;
}

double distanceToBox(const glm::dvec3& p, const glm::dvec3& lo, const glm::dvec3& hi)
{
    const glm::dvec3 d = glm::max(glm::max(lo - p, p - hi), glm::dvec3(0.0));
    return glm::length(d);
}

double lodRange(int level, double rangeFactor)
{
    return rangeFactor * nodeSize(level);
}

CdlodSelection selectCdlod(TileCache& cache, const glm::dvec3& cameraPos, const glm::mat4& viewProjection,
                           const CdlodSettings& settings)
{
    CdlodSelection out;
    glm::vec4 planes[6];
    extractFrustumPlanes(viewProjection, planes);
    const double rootSize = nodeSize(kMaxLevel);
    out.rootX = floorDiv(cameraPos.x, rootSize);
    out.rootZ = floorDiv(cameraPos.z, rootSize);
    Selector selector(cache, cameraPos, settings, out);
    for (std::int64_t dz = -kRootRadius; dz <= kRootRadius; ++dz) {
        for (std::int64_t dx = -kRootRadius; dx <= kRootRadius; ++dx) {
            selector.select(TileKey{kMaxLevel, out.rootX + dx, out.rootZ + dz}, planes);
        }
    }
    return out;
}

std::vector<CdlodNode> selectCdlodLoaded(TileCache& cache, const glm::dvec3& cameraPos, std::int64_t rootX,
                                         std::int64_t rootZ, const glm::mat4& viewProj, double rangeFactor,
                                         double lodScale, int skipPlane)
{
    glm::vec4 planes[6];
    extractFrustumPlanes(viewProj, planes);
    std::vector<CdlodNode> out;
    for (std::int64_t dz = -kRootRadius; dz <= kRootRadius; ++dz) {
        for (std::int64_t dx = -kRootRadius; dx <= kRootRadius; ++dx) {
            selectLoaded(cache, cameraPos, TileKey{kMaxLevel, rootX + dx, rootZ + dz}, planes, rangeFactor,
                         lodScale, skipPlane, out);
        }
    }
    return out;
}

} // namespace emerald
