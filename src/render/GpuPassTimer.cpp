#include "render/GpuPassTimer.hpp"

namespace emerald {

GpuPassTimer::GpuPassTimer()
{
    for (Slot& slot : m_slots) {
        glGenQueries(static_cast<GLsizei>(kSegments), slot.queries.data());
    }
}

GpuPassTimer::~GpuPassTimer()
{
    for (Slot& slot : m_slots) {
        glDeleteQueries(static_cast<GLsizei>(kSegments), slot.queries.data());
    }
}

const char* GpuPassTimer::name(Pass pass)
{
    switch (pass) {
    case Shadows: return "Shadows";
    case Reflection: return "Reflection & seabed";
    case Rain: return "Rain";
    case Scene: return "Scene";
    case Terrain: return "Terrain";
    case Sea: return "Sea";
    case Sky: return "Sky";
    case ToneMap: return "Tone map";
    case Ui: return "Panel";
    default: return "Other";
    }
}

void GpuPassTimer::beginFrame(int frame)
{
    // Every slot still waiting for the GPU: skip timing this frame rather than wait.
    Slot& slot = m_slots[m_next];
    m_current = slot.pending ? nullptr : &slot;
    if (m_current) {
        m_current->used = 0;
        m_current->frame = frame;
    }
}

void GpuPassTimer::mark(Pass pass)
{
    if (!m_current) {
        return;
    }
    if (m_open) {
        glEndQuery(GL_TIME_ELAPSED);
        m_open = false;
    }
    if (m_current->used == kSegments) {
        return; // out of queries: the rest of the frame goes untimed
    }
    m_current->passes[m_current->used] = pass;
    glBeginQuery(GL_TIME_ELAPSED, m_current->queries[m_current->used++]);
    m_open = true;
}

void GpuPassTimer::endFrame()
{
    if (!m_current) {
        return;
    }
    if (m_open) {
        glEndQuery(GL_TIME_ELAPSED);
        m_open = false;
    }
    m_current->pending = m_current->used > 0;
    m_current = nullptr;
    m_next = (m_next + 1) % kFrames;
}

std::vector<GpuPassTimer::Result> GpuPassTimer::collect()
{
    std::vector<Result> results;
    // Frames complete in submission order; a frame is done once its last query is.
    for (std::size_t n = 0; n < kFrames; ++n) {
        Slot& slot = m_slots[m_oldest];
        if (!slot.pending) {
            if (m_oldest == m_next) {
                break; // nothing in flight
            }
            m_oldest = (m_oldest + 1) % kFrames;
            continue;
        }
        GLint available = 0;
        glGetQueryObjectiv(slot.queries[slot.used - 1], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) {
            break;
        }
        Result result{slot.frame, {}, 0.0f};
        for (std::size_t i = 0; i < slot.used; ++i) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(slot.queries[i], GL_QUERY_RESULT, &ns);
            const auto ms = static_cast<float>(static_cast<double>(ns) / 1e6);
            result.ms[slot.passes[i]] += ms;
            result.total += ms;
        }
        results.push_back(result);
        slot.pending = false;
        m_oldest = (m_oldest + 1) % kFrames;
    }
    return results;
}

} // namespace emerald
