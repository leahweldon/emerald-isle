#pragma once

#include "world/TileStreamer.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace emerald {

class HeightField;
class Shader;

// Endless terrain as a CDLOD quadtree. Every node is drawn with one shared grid mesh; heights come
// from streamed per-node tile textures, sampled in the vertex shader, and vertices
// geomorph towards the next coarser grid as distance approaches the node's range so levels meet
// without popping or cracks. Rendering is camera-relative (floating origin).
class Terrain {
public:
    struct Stats {
        int drawCalls = 0;
        std::size_t triangles = 0;
        int residentTiles = 0;
        std::size_t outstandingTiles = 0;
        int uploadsThisFrame = 0;
        std::uint64_t tilesGenerated = 0; // since startup
        double updateMs = 0.0;            // main-thread cost of update(): uploads and selection
        int missingNodes = 0;             // visible nodes with no tile at any level: holes
        bool cameraCovered = false;       // a tile is loaded for the ground under the camera
        int shadowDrawCalls = 0;          // all cascades
        std::size_t shadowTriangles = 0;
    };

    explicit Terrain(const HeightField& heightField);
    ~Terrain();

    Terrain(const Terrain&) = delete;
    Terrain& operator=(const Terrain&) = delete;

    // Uploads finished tiles (bounded per frame), selects nodes for this camera and queues missing
    // tiles. Never waits for the worker.
    void update(const glm::dvec3& cameraPos, const glm::mat4& viewProjection);

    // Draws the nodes chosen by update(). The shader must be bound with scene uniforms set.
    // Each node's horizon map (see TileStreamer) is bound to horizonUnit.
    void draw(Shader& shader, GLuint heightTileUnit, GLuint horizonUnit);

    // Depth pass for one shadow cascade: loaded nodes inside its volume (clip space from
    // lightViewProj, camera-relative), at the view's level of detail scaled by lodScale (< 1 is
    // coarser). Skirts are left out. Only uses tiles already loaded; requests nothing.
    void drawShadowCasters(Shader& shader, GLuint heightTileUnit, const glm::mat4& lightViewProj,
                           float lodScale);

    // Blurred land mask over the DEM extent (R8, 1 = land), for shallow-water shading.
    // Row 0 is north (-Z); sample at uv = xz / demSize() + 0.5.
    void bindCoastMask(GLuint unit) const;
    glm::vec2 demSize() const { return m_demSize; }

    // True when everything the last update() wanted is loaded and nothing is queued.
    bool settled() const { return m_settled; }
    const Stats& stats() const { return m_stats; }

private:
    struct Slot {
        TileKey key;
        float minY = 0.0f;
        float maxY = 0.0f;
        std::uint64_t lastUsed = 0;
        GLuint texture = 0; // 0 when the slot is free
        GLuint horizon = 0; // 2-layer array, or 0 for fine nodes without one
    };
    struct DrawNode {
        TileKey key;
        GLuint texture;
        GLuint horizon;
        double distance; // camera to node bounds, for front-to-back order
    };

    void selectNode(const TileKey& key, const glm::dvec3& cameraPos, const glm::vec4 planes[6],
                    std::vector<TileStreamer::Request>& wanted);
    void selectCaster(const TileKey& key, const glm::vec4 planes[6], double lodScale);
    // horizonUnit < 0 skips the horizon maps (depth passes).
    void drawNodes(Shader& shader, GLuint heightTileUnit, int horizonUnit,
                   const std::vector<DrawNode>& nodes, GLsizei indexCount);
    // Keeps coarse tiles loaded outside the view, so turning reveals terrain rather than holes.
    void prefetchNode(const TileKey& key, const glm::dvec3& cameraPos,
                      std::vector<TileStreamer::Request>& wanted);
    // Slot index of a loaded tile (marking it used this frame), or -1.
    int residentSlot(const TileKey& key);
    void uploadTile(const TileData& tile);
    void buildGridMesh();
    void buildCoastMask(const HeightField& heightField);

    std::unique_ptr<TileStreamer> m_streamer;
    // One small R32F texture per resident tile. Each upload creates a fresh texture, so the driver
    // never has to wait for frames in flight that still read an older tile.
    int m_capacity = 0;
    std::vector<Slot> m_slots;
    std::unordered_map<TileKey, int, TileKeyHash> m_lookup;
    std::uint64_t m_frame = 0;

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLsizei m_gridIndexCount = 0;
    GLsizei m_totalIndexCount = 0;

    GLuint m_coastMask = 0;
    GLuint m_flatHorizon = 0; // horizon at 0 degrees everywhere, for nodes without a map
    glm::vec2 m_demSize{0.0f};

    std::vector<DrawNode> m_drawList;
    std::vector<DrawNode> m_casterList;
    glm::dvec3 m_cameraPos{0.0};
    std::int64_t m_rootX = 0; // root cell under the camera
    std::int64_t m_rootZ = 0;
    bool m_settled = false;
    Stats m_stats;
};

} // namespace emerald
