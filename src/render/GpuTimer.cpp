#include "render/GpuTimer.hpp"

namespace emerald {

GpuTimer::GpuTimer()
{
    glGenQueries(static_cast<GLsizei>(kQueries), m_queries.data());
}

GpuTimer::~GpuTimer()
{
    glDeleteQueries(static_cast<GLsizei>(kQueries), m_queries.data());
}

void GpuTimer::begin(int frame)
{
    // All queries busy: drop this frame's timing rather than wait for the GPU.
    m_active = !m_pending[m_next];
    if (!m_active) {
        return;
    }
    m_frames[m_next] = frame;
    glBeginQuery(GL_TIME_ELAPSED, m_queries[m_next]);
}

void GpuTimer::end()
{
    if (!m_active) {
        return;
    }
    m_active = false;
    glEndQuery(GL_TIME_ELAPSED);
    m_pending[m_next] = true;
    m_next = (m_next + 1) % kQueries;
}

std::vector<GpuTimer::Result> GpuTimer::collect()
{
    std::vector<Result> results;
    // Queries complete in submission order, so stop at the first one still in flight.
    while (m_pending[m_oldest]) {
        GLint available = 0;
        glGetQueryObjectiv(m_queries[m_oldest], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) {
            break;
        }
        GLuint64 ns = 0;
        glGetQueryObjectui64v(m_queries[m_oldest], GL_QUERY_RESULT, &ns);
        results.push_back({m_frames[m_oldest], static_cast<double>(ns) / 1e6});
        m_pending[m_oldest] = false;
        m_oldest = (m_oldest + 1) % kQueries;
    }
    return results;
}

} // namespace emerald
