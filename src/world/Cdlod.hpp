#pragma once

#include "world/TileCache.hpp"
#include "world/TileStreamer.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace emerald {

// CDLOD node selection over the terrain quadtree, without any GPU state: which resident tiles to
// draw for a camera, and which tiles to request. A node splits when the camera is within
// rangeFactor times the child size of it, and only once all four children are resident; until
// then the coarser node is drawn.

constexpr int kRootRadius = 2; // roots selected around the camera's root cell (5 x 5)

struct CdlodSettings {
    double rangeFactor = 2.5; // split distance in child-node sizes
    bool nearFirst = false;   // request nearest tiles in view first, not coarsest first
};

struct CdlodNode {
    TileKey key;
    int slot;        // in the TileCache
    double distance; // camera to node bounds
};

struct CdlodSelection {
    std::vector<CdlodNode> nodes;                  // to draw, in traversal order
    std::vector<TileStreamer::Request> requests;   // missing tiles, with priorities
    int missingNodes = 0;                          // visible roots with no tile: holes
    std::int64_t rootX = 0;                        // root cell under the camera
    std::int64_t rootZ = 0;
};

// Frustum planes (a, b, c, d) with inward normals, from a camera-relative view-projection.
void extractFrustumPlanes(const glm::mat4& m, glm::vec4 planes[6]);
bool boxVisible(const glm::vec4 planes[6], const glm::vec3& lo, const glm::vec3& hi, int skipPlane = -1);
double distanceToBox(const glm::dvec3& p, const glm::dvec3& lo, const glm::dvec3& hi);
double lodRange(int level, double rangeFactor);

// Selects nodes for the camera (marking the tiles it reads as used in the cache), requests missing
// tiles in view, and keeps coarse levels requested outside it.
CdlodSelection selectCdlod(TileCache& cache, const glm::dvec3& cameraPos, const glm::mat4& viewProjection,
                           const CdlodSettings& settings);

// Resident nodes only (no requests) inside viewProj, around the given root cell, splitting at
// lodScale times the usual range; plane skipPlane (or none if -1) is ignored. For depth passes.
std::vector<CdlodNode> selectCdlodLoaded(TileCache& cache, const glm::dvec3& cameraPos, std::int64_t rootX,
                                         std::int64_t rootZ, const glm::mat4& viewProj, double rangeFactor,
                                         double lodScale, int skipPlane);

} // namespace emerald
