#include "render/Terrain.hpp"

#include "render/Shader.hpp"
#include "world/HeightField.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

namespace emerald {

namespace {

// A node splits when the camera is within kRangeFactor * child size of it; vertices morph to the
// coarser grid over the last part of that range so neighbouring levels meet smoothly.
constexpr double kRangeFactor = 2.5;
constexpr float kMorphStart = 0.75f; // fraction of the range where morphing begins
constexpr int kRootRadius = 2;       // roots drawn around the camera's root cell (5 x 5)
// Levels this coarse (131 km nodes and up) are kept loaded even outside the view.
constexpr int kPrefetchLevel = kMaxLevel - 2;
// Requests for tiles outside the view come after every visible one.
constexpr double kPrefetchPriority = 1e9;
constexpr int kUploadsPerFrame = 16;
constexpr int kCapacity = 1024; // resident tiles
// Bounds for nodes whose tile is not loaded yet, so they are not culled by mistake.
constexpr float kUnknownMinY = -200.0f;
constexpr float kUnknownMaxY = 5000.0f;
// Matches kMaterialPeriod in terrain.frag: material patterns repeat exactly every this many metres.
constexpr double kMaterialPeriod = 360000.0;

double lodRange(int level)
{
    return kRangeFactor * nodeSize(level);
}

// Frustum planes (a, b, c, d) with inward normals, from a camera-relative view-projection.
void extractPlanes(const glm::mat4& m, glm::vec4 planes[6])
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

bool boxVisible(const glm::vec4 planes[6], const glm::vec3& lo, const glm::vec3& hi, int skipPlane = -1)
{
    for (int i = 0; i < 6; ++i) {
        if (i == skipPlane) {
            continue;
        }
        const glm::vec4& p = planes[i];
        // The corner furthest along the plane normal.
        const glm::vec3 v(p.x >= 0.0f ? hi.x : lo.x, p.y >= 0.0f ? hi.y : lo.y,
                          p.z >= 0.0f ? hi.z : lo.z);
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

std::int64_t floorDiv(double value, double size)
{
    return static_cast<std::int64_t>(std::floor(value / size));
}

} // namespace

Terrain::Terrain(const HeightField& heightField)
{
    m_capacity = kCapacity;
    m_slots.resize(static_cast<std::size_t>(m_capacity));

    buildGridMesh();
    buildCoastMask(heightField);

    const unsigned char flat[8] = {};
    glGenTextures(1, &m_flatHorizon);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_flatHorizon);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, flat);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    m_streamer = std::make_unique<TileStreamer>(heightField);
    std::printf("Terrain: CDLOD, %d levels, %d x %d grid per node, %d tile slots\n", kMaxLevel + 1,
                kTileQuads, kTileQuads, m_capacity);
}

Terrain::~Terrain()
{
    m_streamer.reset(); // join the worker before GL objects go
    for (const Slot& slot : m_slots) {
        if (slot.texture != 0) {
            glDeleteTextures(1, &slot.texture);
        }
        if (slot.horizon != 0) {
            glDeleteTextures(1, &slot.horizon);
        }
    }
    glDeleteTextures(1, &m_flatHorizon);
    glDeleteTextures(1, &m_coastMask);
    glDeleteBuffers(1, &m_ebo);
    glDeleteBuffers(1, &m_vbo);
    glDeleteVertexArrays(1, &m_vao);
}

void Terrain::buildGridMesh()
{
    // Vertex: grid x, grid z, skirt flag. Skirt vertices duplicate the perimeter and are lowered in
    // the shader to hide any gap where a node is drawn coarser than its neighbour.
    constexpr int n = kTileQuads + 1;
    std::vector<float> vertices;
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            vertices.insert(vertices.end(), {static_cast<float>(x), static_cast<float>(z), 0.0f});
        }
    }
    const auto gridIndex = [](int x, int z) { return static_cast<GLushort>(z * n + x); };

    std::vector<GLushort> indices;
    for (int z = 0; z < kTileQuads; ++z) {
        for (int x = 0; x < kTileQuads; ++x) {
            const GLushort tl = gridIndex(x, z);
            const GLushort tr = gridIndex(x + 1, z);
            const GLushort bl = gridIndex(x, z + 1);
            const GLushort br = gridIndex(x + 1, z + 1);
            indices.insert(indices.end(), {tl, bl, tr, tr, bl, br}); // CCW seen from +Y
        }
    }
    m_gridIndexCount = static_cast<GLsizei>(indices.size());

    // Perimeter in order, then a lowered copy; both windings so culling never removes a skirt.
    std::vector<GLushort> ring;
    for (int x = 0; x < kTileQuads; ++x) ring.push_back(gridIndex(x, 0));
    for (int z = 0; z < kTileQuads; ++z) ring.push_back(gridIndex(kTileQuads, z));
    for (int x = kTileQuads; x > 0; --x) ring.push_back(gridIndex(x, kTileQuads));
    for (int z = kTileQuads; z > 0; --z) ring.push_back(gridIndex(0, z));
    const auto skirtBase = static_cast<GLushort>(vertices.size() / 3);
    for (GLushort i : ring) {
        vertices.insert(vertices.end(), {vertices[i * 3u], vertices[i * 3u + 1u], 1.0f});
    }
    const std::size_t count = ring.size();
    for (std::size_t i = 0; i < count; ++i) {
        const GLushort a = ring[i];
        const GLushort b = ring[(i + 1) % count];
        const auto sa = static_cast<GLushort>(skirtBase + i);
        const auto sb = static_cast<GLushort>(skirtBase + (i + 1) % count);
        indices.insert(indices.end(), {a, sa, b, b, sa, sb, a, b, sa, b, sb, sa});
    }
    m_totalIndexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(GLushort)),
                 indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void Terrain::buildCoastMask(const HeightField& heightField)
{
    // Fraction of land per 4 x 4 DEM block, then two box-blur passes: ~920 m texels, ~4-5 km blur.
    constexpr int factor = 4;
    constexpr int radius = 2;
    const int demW = heightField.demWidth();
    const int demH = heightField.demHeight();
    const std::vector<float>& dem = heightField.demHeights();
    const int w = (demW + factor - 1) / factor;
    const int h = (demH + factor - 1) / factor;
    const auto index = [w](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
    };
    std::vector<float> mask(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0f);
    for (int y = 0; y < demH; ++y) {
        for (int x = 0; x < demW; ++x) {
            if (dem[static_cast<std::size_t>(y) * static_cast<std::size_t>(demW) + static_cast<std::size_t>(x)] > 0.0f) {
                mask[index(x / factor, y / factor)] += 1.0f / static_cast<float>(factor * factor);
            }
        }
    }
    std::vector<float> temp(mask.size());
    const float norm = 1.0f / static_cast<float>(2 * radius + 1);
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) sum += mask[index(std::clamp(x + k, 0, w - 1), y)];
                temp[index(x, y)] = sum * norm;
            }
        }
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) sum += temp[index(x, std::clamp(y + k, 0, h - 1))];
                mask[index(x, y)] = sum * norm;
            }
        }
    }
    std::vector<unsigned char> bytes(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        bytes[i] = static_cast<unsigned char>(std::clamp(mask[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    }

    glGenTextures(1, &m_coastMask);
    glBindTexture(GL_TEXTURE_2D, m_coastMask);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, bytes.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Outside the DEM is open sea as far as the mask is concerned: border 0, not repeated edges.
    const GLfloat border[] = {0.0f, 0.0f, 0.0f, 0.0f};
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_demSize = glm::vec2(2.0 * heightField.demHalfWidth(), 2.0 * heightField.demHalfDepth());
}

void Terrain::bindCoastMask(GLuint unit) const
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_coastMask);
}

int Terrain::residentSlot(const TileKey& key)
{
    const auto it = m_lookup.find(key);
    if (it == m_lookup.end()) {
        return -1;
    }
    m_slots[static_cast<std::size_t>(it->second)].lastUsed = m_frame;
    return it->second;
}

void Terrain::uploadTile(const TileData& tile)
{
    if (m_lookup.count(tile.key) != 0) {
        return;
    }
    // A free slot, or the least recently used one not needed this frame.
    int chosen = -1;
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (int i = 0; i < m_capacity; ++i) {
        const Slot& slot = m_slots[static_cast<std::size_t>(i)];
        if (slot.texture == 0) {
            chosen = i;
            break;
        }
        if (slot.lastUsed < m_frame && slot.lastUsed < oldest) {
            oldest = slot.lastUsed;
            chosen = i;
        }
    }
    if (chosen < 0) {
        return; // every slot is in use this frame; the tile will be requested again
    }
    Slot& slot = m_slots[static_cast<std::size_t>(chosen)];
    if (slot.texture != 0) {
        m_lookup.erase(slot.key);
        glDeleteTextures(1, &slot.texture); // the driver frees it once in-flight frames finish
        if (slot.horizon != 0) {
            glDeleteTextures(1, &slot.horizon);
        }
    }

    // A new texture rather than updating one the GPU may still be reading: no pipeline stall.
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, kTileSamples, kTileSamples, 0, GL_RED, GL_FLOAT,
                 tile.heights.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint horizon = 0;
    if (!tile.horizon.empty()) {
        glGenTextures(1, &horizon);
        glBindTexture(GL_TEXTURE_2D_ARRAY, horizon);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, kHorizonSize, kHorizonSize, 2, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, tile.horizon.data());
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    slot = Slot{tile.key, tile.minY, tile.maxY, m_frame, texture, horizon};
    m_lookup[tile.key] = chosen;
}

void Terrain::update(const glm::dvec3& cameraPos, const glm::mat4& viewProjection)
{
    const auto start = std::chrono::steady_clock::now();
    ++m_frame;
    m_cameraPos = cameraPos;
    m_stats = Stats{};

    // Upload a bounded number of finished tiles so a burst never stalls the frame.
    const std::vector<TileData> done = m_streamer->takeCompleted(kUploadsPerFrame);
    for (const TileData& tile : done) {
        uploadTile(tile);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    m_stats.uploadsThisFrame = static_cast<int>(done.size());

    glm::vec4 planes[6];
    extractPlanes(viewProjection, planes);

    m_drawList.clear();
    std::vector<TileStreamer::Request> wanted;
    const double rootSize = nodeSize(kMaxLevel);
    const std::int64_t cx = floorDiv(cameraPos.x, rootSize);
    const std::int64_t cz = floorDiv(cameraPos.z, rootSize);
    m_rootX = cx;
    m_rootZ = cz;
    for (std::int64_t dz = -kRootRadius; dz <= kRootRadius; ++dz) {
        for (std::int64_t dx = -kRootRadius; dx <= kRootRadius; ++dx) {
            selectNode(TileKey{kMaxLevel, cx + dx, cz + dz}, cameraPos, planes, wanted);
        }
    }
    m_streamer->setRequests(wanted);
    // A loaded root means the ground under the camera can be drawn at some level.
    m_stats.cameraCovered = m_lookup.count(TileKey{kMaxLevel, cx, cz}) != 0;

    // Nearest first, so early depth testing rejects hidden fragments before the material shader.
    std::sort(m_drawList.begin(), m_drawList.end(),
              [](const DrawNode& a, const DrawNode& b) { return a.distance < b.distance; });

    m_stats.outstandingTiles = m_streamer->outstanding();
    m_stats.residentTiles = static_cast<int>(m_lookup.size());
    m_stats.tilesGenerated = m_streamer->generatedCount();
    m_settled = wanted.empty() && m_stats.outstandingTiles == 0;
    m_stats.updateMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void Terrain::prefetchNode(const TileKey& key, const glm::dvec3& cameraPos,
                           std::vector<TileStreamer::Request>& wanted)
{
    const double size = nodeSize(key.level);
    const glm::dvec3 lo(static_cast<double>(key.x) * size, kUnknownMinY, static_cast<double>(key.z) * size);
    const glm::dvec3 hi = lo + glm::dvec3(size, kUnknownMaxY - kUnknownMinY, size);
    const double distance = distanceToBox(cameraPos, lo, hi);
    if (residentSlot(key) < 0) {
        wanted.push_back({key, static_cast<float>(kPrefetchPriority + (kMaxLevel - key.level) * 1e8 + distance)});
        return;
    }
    if (key.level > kPrefetchLevel && distance < lodRange(key.level - 1)) {
        for (int i = 0; i < 4; ++i) {
            prefetchNode(TileKey{key.level - 1, key.x * 2 + (i & 1), key.z * 2 + (i >> 1)}, cameraPos, wanted);
        }
    }
}

void Terrain::selectNode(const TileKey& key, const glm::dvec3& cameraPos, const glm::vec4 planes[6],
                         std::vector<TileStreamer::Request>& wanted)
{
    const double size = nodeSize(key.level);
    const int slot = residentSlot(key);
    const float minY = slot >= 0 ? m_slots[static_cast<std::size_t>(slot)].minY : kUnknownMinY;
    const float maxY = slot >= 0 ? m_slots[static_cast<std::size_t>(slot)].maxY : kUnknownMaxY;
    const glm::dvec3 lo(static_cast<double>(key.x) * size, minY, static_cast<double>(key.z) * size);
    const glm::dvec3 hi = lo + glm::dvec3(size, static_cast<double>(maxY - minY), size);

    if (!boxVisible(planes, glm::vec3(lo - cameraPos), glm::vec3(hi - cameraPos))) {
        if (key.level >= kPrefetchLevel) {
            prefetchNode(key, cameraPos, wanted);
        }
        return;
    }
    const double distance = distanceToBox(cameraPos, lo, hi);
    if (slot < 0) {
        // Children are only selected once all four are loaded, so this is a visible root: a hole.
        ++m_stats.missingNodes;
        // Coarser levels first, then nearest.
        wanted.push_back({key, static_cast<float>((kMaxLevel - key.level) * 1e8 + distance)});
        return;
    }

    if (key.level > 0 && distance < lodRange(key.level - 1)) {
        TileKey children[4];
        bool ready = true;
        for (int i = 0; i < 4; ++i) {
            children[i] = TileKey{key.level - 1, key.x * 2 + (i & 1), key.z * 2 + (i >> 1)};
            if (residentSlot(children[i]) < 0) {
                ready = false;
                const double childSize = nodeSize(key.level - 1);
                const glm::dvec3 clo(static_cast<double>(children[i].x) * childSize, kUnknownMinY,
                                     static_cast<double>(children[i].z) * childSize);
                const glm::dvec3 chi = clo + glm::dvec3(childSize, kUnknownMaxY - kUnknownMinY, childSize);
                wanted.push_back({children[i], static_cast<float>((kMaxLevel - children[i].level) * 1e8 +
                                                                  distanceToBox(cameraPos, clo, chi))});
            }
        }
        if (ready) {
            for (const TileKey& child : children) {
                selectNode(child, cameraPos, planes, wanted);
            }
            return;
        }
        // Children still streaming: draw this coarser node meanwhile.
    }
    const Slot& chosen = m_slots[static_cast<std::size_t>(slot)];
    m_drawList.push_back(DrawNode{key, chosen.texture, chosen.horizon, distance});
}

void Terrain::selectCaster(const TileKey& key, const glm::vec4 planes[6], double lodScale, int skipPlane)
{
    const int slot = residentSlot(key);
    if (slot < 0) {
        return;
    }
    const Slot& s = m_slots[static_cast<std::size_t>(slot)];
    const double size = nodeSize(key.level);
    const glm::dvec3 lo(static_cast<double>(key.x) * size, s.minY, static_cast<double>(key.z) * size);
    const glm::dvec3 hi = lo + glm::dvec3(size, static_cast<double>(s.maxY - s.minY), size);
    if (!boxVisible(planes, glm::vec3(lo - m_cameraPos), glm::vec3(hi - m_cameraPos), skipPlane)) {
        return;
    }
    const double distance = distanceToBox(m_cameraPos, lo, hi);
    if (key.level > 0 && distance < lodRange(key.level - 1) * lodScale) {
        TileKey children[4];
        bool ready = true;
        for (int i = 0; i < 4; ++i) {
            children[i] = TileKey{key.level - 1, key.x * 2 + (i & 1), key.z * 2 + (i >> 1)};
            ready = ready && m_lookup.count(children[i]) != 0;
        }
        if (ready) {
            for (const TileKey& child : children) {
                selectCaster(child, planes, lodScale, skipPlane);
            }
            return;
        }
    }
    m_casterList.push_back(DrawNode{key, s.texture, s.horizon, distance});
}

void Terrain::selectCasters(const glm::mat4& viewProj, double lodScale, int skipPlane)
{
    glm::vec4 planes[6];
    extractPlanes(viewProj, planes);
    m_casterList.clear();
    for (std::int64_t dz = -kRootRadius; dz <= kRootRadius; ++dz) {
        for (std::int64_t dx = -kRootRadius; dx <= kRootRadius; ++dx) {
            selectCaster(TileKey{kMaxLevel, m_rootX + dx, m_rootZ + dz}, planes, lodScale, skipPlane);
        }
    }
}

void Terrain::drawReflection(Shader& shader, GLuint heightTileUnit, GLuint horizonUnit, const glm::mat4& viewProj,
                             float lodScale)
{
    selectCasters(viewProj, lodScale, -1);
    shader.set("uHorizon", static_cast<int>(horizonUnit));
    drawNodes(shader, heightTileUnit, static_cast<int>(horizonUnit), m_casterList, m_totalIndexCount);
    m_stats.reflectionDrawCalls += static_cast<int>(m_casterList.size());
    m_stats.reflectionTriangles += m_casterList.size() * static_cast<std::size_t>(m_totalIndexCount / 3);
}

void Terrain::drawShadowCasters(Shader& shader, GLuint heightTileUnit, const glm::mat4& lightViewProj,
                                float lodScale)
{
    // Plane 4 is the side facing the light: casters beyond it are clamped onto it, not culled.
    selectCasters(lightViewProj, lodScale, 4);
    drawNodes(shader, heightTileUnit, -1, m_casterList, m_gridIndexCount);
    m_stats.shadowDrawCalls += static_cast<int>(m_casterList.size());
    m_stats.shadowTriangles += m_casterList.size() * static_cast<std::size_t>(m_gridIndexCount / 3);
}

void Terrain::draw(Shader& shader, GLuint heightTileUnit, GLuint horizonUnit)
{
    shader.set("uHorizon", static_cast<int>(horizonUnit));
    drawNodes(shader, heightTileUnit, static_cast<int>(horizonUnit), m_drawList, m_totalIndexCount);
    m_stats.drawCalls = static_cast<int>(m_drawList.size());
    m_stats.triangles = m_drawList.size() * static_cast<std::size_t>(m_totalIndexCount / 3);
}

void Terrain::drawNodes(Shader& shader, GLuint heightTileUnit, int horizonUnit,
                        const std::vector<DrawNode>& nodes, GLsizei indexCount)
{
    glActiveTexture(GL_TEXTURE0 + heightTileUnit);
    shader.set("uHeightTile", static_cast<int>(heightTileUnit));
    shader.set("uTileSamples", static_cast<float>(kTileSamples));
    shader.set("uTileBorder", static_cast<float>(kTileBorder));
    shader.set("uTileQuads", static_cast<float>(kTileQuads));

    const GLint originLoc = shader.uniformLocation("uNodeOrigin");
    const GLint materialLoc = shader.uniformLocation("uNodeMaterialOrigin");
    const GLint sizeLoc = shader.uniformLocation("uNodeSize");
    const GLint morphLoc = shader.uniformLocation("uMorphRange");
    const GLint levelLoc = shader.uniformLocation("uNodeLevel"); // debug view only

    glBindVertexArray(m_vao);
    for (const DrawNode& node : nodes) {
        const double size = nodeSize(node.key.level);
        const double ox = static_cast<double>(node.key.x) * size;
        const double oz = static_cast<double>(node.key.z) * size;
        // Origin relative to the camera, computed in double: the floating origin.
        glUniform2f(originLoc, static_cast<float>(ox - m_cameraPos.x), static_cast<float>(oz - m_cameraPos.z));
        // Wrapped in double so material coordinates stay small however far out the node is.
        glUniform2f(materialLoc, static_cast<float>(ox - kMaterialPeriod * std::floor(ox / kMaterialPeriod)),
                    static_cast<float>(oz - kMaterialPeriod * std::floor(oz / kMaterialPeriod)));
        glUniform1f(sizeLoc, static_cast<float>(size));
        glUniform1f(levelLoc, static_cast<float>(node.key.level));
        if (horizonUnit >= 0) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLuint>(horizonUnit));
            glBindTexture(GL_TEXTURE_2D_ARRAY, node.horizon != 0 ? node.horizon : m_flatHorizon);
            glActiveTexture(GL_TEXTURE0 + heightTileUnit);
        }
        glBindTexture(GL_TEXTURE_2D, node.texture);
        if (node.key.level < kMaxLevel) {
            const auto range = static_cast<float>(lodRange(node.key.level));
            glUniform2f(morphLoc, kMorphStart * range, range);
        } else {
            glUniform2f(morphLoc, 1e30f, 2e30f); // no coarser level to morph to
        }
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT, nullptr);
    }
    glBindVertexArray(0);
}

} // namespace emerald
