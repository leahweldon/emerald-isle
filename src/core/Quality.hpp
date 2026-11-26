#pragma once

#include <array>

namespace emerald {

// Rendering cost against detail. High is the desktop renderer as designed; Medium is the web
// default; Low suits integrated GPUs and phones.
struct Quality {
    const char* name;
    float maxPixelRatio;      // device pixels per CSS pixel the scene is rendered at, at most
    int shadowResolution;     // texels per side of each shadow cascade
    int reflectionDivisor;    // the reflection is this many times smaller than the scene
    float rainDrops;          // fraction of the rain pool drawn
    double lodRange;          // CDLOD split distance in child-node sizes (lower is coarser)
    int msaa;                 // samples of the HDR scene target
    double tileBudgetMs;      // main-thread tile generation per frame, when there is no worker
};

inline constexpr std::array<Quality, 3> kQualities = {{
    {"Low", 1.0f, 1024, 4, 0.25f, 1.75, 0, 3.0},
    {"Medium", 1.5f, 1536, 3, 0.5f, 2.0, 2, 4.0},
    {"High", 16.0f, 2048, 2, 1.0f, 2.5, 4, 4.0},
}};

} // namespace emerald
