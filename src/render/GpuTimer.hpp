#pragma once

#include <glad/gl.h>

#include <array>
#include <cstddef>
#include <vector>

namespace emerald {

// GPU time per frame from GL_TIME_ELAPSED queries. Results are read a few frames later, only once
// available, so timing never stalls the pipeline. On macOS the queries themselves add ~0.4 ms a
// frame (GL_TIMESTAMP reads back zero there), so timing is opt-in.
class GpuTimer {
public:
    struct Result {
        int frame;
        double ms;
    };

    GpuTimer();
    ~GpuTimer();

    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    void begin(int frame);
    void end();
    // Results that have become available since the last call, oldest first.
    std::vector<Result> collect();

private:
    static constexpr std::size_t kQueries = 16; // frames that may be in flight

    std::array<GLuint, kQueries> m_queries{};
    std::array<int, kQueries> m_frames{};
    std::array<bool, kQueries> m_pending{};
    bool m_active = false; // a query is open between begin() and end()
    std::size_t m_next = 0;
    std::size_t m_oldest = 0;
};

} // namespace emerald
