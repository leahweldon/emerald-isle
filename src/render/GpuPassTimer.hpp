#pragma once

#include <glad/gl.h>

#include <array>
#include <cstddef>
#include <vector>

namespace emerald {

// GPU time per render pass from GL_TIME_ELAPSED queries. Such queries cannot nest (and
// GL_TIMESTAMP reads back zero on macOS), so a frame is split into consecutive segments, each
// timed by its own query and credited to a pass; passes that recur in a frame are summed, and the
// segments together cover the frame's GPU work (not idle gaps between them). Results are read
// only once available, a few frames later, so timing never stalls the pipeline. On macOS each
// boundary inside a render pass splits it, which costs more than the boundaries between passes.
class GpuPassTimer {
public:
    // Scene is the main HDR pass as a whole; Terrain, Sea, Sky and Rain split it when asked.
    enum Pass { Shadows, Reflection, Rain, Scene, Terrain, Sea, Sky, ToneMap, Ui, Other, kPassCount };
    using Times = std::array<float, kPassCount>; // milliseconds

    struct Result {
        int frame;
        Times ms;
        float total;
    };

    GpuPassTimer();
    ~GpuPassTimer();

    GpuPassTimer(const GpuPassTimer&) = delete;
    GpuPassTimer& operator=(const GpuPassTimer&) = delete;

    static const char* name(Pass pass);

    void beginFrame(int frame);
    // Ends the running segment and starts one for pass.
    void mark(Pass pass);
    void endFrame();
    // Frames whose results have become available since the last call, oldest first.
    std::vector<Result> collect();

private:
    static constexpr std::size_t kFrames = 6;    // frames that may be in flight
    static constexpr std::size_t kSegments = 24; // per frame

    struct Slot {
        std::array<GLuint, kSegments> queries{};
        std::array<Pass, kSegments> passes{};
        std::size_t used = 0;
        int frame = 0;
        bool pending = false;
    };

    std::array<Slot, kFrames> m_slots{};
    std::size_t m_next = 0;
    std::size_t m_oldest = 0;
    Slot* m_current = nullptr; // the slot being recorded, or null when this frame is not timed
    bool m_open = false;       // a query is running
};

} // namespace emerald
