// The in-app panel: Application members that build the ImGui widgets each frame.

#include "core/Application.hpp"

#include "render/Terrain.hpp"
#include "ui/Ui.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace emerald {

namespace {

// Panel layout, in display units at UI scale 1.
constexpr float kPanelWidth = 360.0f;
constexpr float kLabelWidth = 110.0f; // right of sliders, for their labels
constexpr float kValueColumn = 130.0f; // where readout values start

} // namespace

void Application::updateUi()
{
    Input& input = m_window.input();
    input.setBlocked(false, false);
    if (input.wasPressed(GLFW_KEY_F1)) {
        m_ui->toggle();
    }
    if (m_ui->beginFrame(m_window.framebufferWidth(), m_window.framebufferHeight())) {
        buildPanel();
    }
    input.setBlocked(m_ui->wantsKeyboard(), m_ui->wantsMouse());
}

void Application::buildPanel()
{
    const float scale = m_ui->scale();
    ImGui::SetNextWindowPos(ImVec2(12.0f * scale, 12.0f * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(kPanelWidth * scale, 0.0f), ImVec2(kPanelWidth * scale, 1e6f));
    if (!ImGui::Begin("Emerald Isle", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::PushItemWidth(-kLabelWidth * scale);
    if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
        panelPerformance();
    }
    ImGui::PopItemWidth();
    ImGui::TextDisabled("F1 hides this panel");
    ImGui::End();
}

void Application::panelPerformance()
{
    const float scale = m_ui->scale();
    ImGui::Text("%.0f FPS", m_timer.framesPerSecond());
    ImGui::SameLine(kValueColumn * scale);
    ImGui::Text("%.2f ms a frame", m_timer.frameTimeMs());

    // Recent frame times, oldest on the left, on a fixed 0-20 ms scale so spikes stand out.
    char overlay[48];
    const float latest = m_frameHistory[(m_historyNext + m_frameHistory.size() - 1) % m_frameHistory.size()];
    std::snprintf(overlay, sizeof(overlay), "%.2f ms", static_cast<double>(latest));
    ImGui::PlotLines("##frames", m_frameHistory.data(), static_cast<int>(m_frameHistory.size()),
                     static_cast<int>(m_historyNext), overlay, 0.0f, 20.0f,
                     ImVec2(ImGui::GetContentRegionAvail().x, 48.0f * scale));
    ImGui::TextDisabled("Last %zu frames, 0-20 ms (120 Hz: 8.3 ms)", m_frameHistory.size());

    ImGui::SeparatorText("GPU time per pass");
    const float total = std::max(m_passTotalMs, 1e-3f);
    for (int i = 0; i < GpuPassTimer::kPassCount; ++i) {
        const float ms = m_passMs[static_cast<std::size_t>(i)];
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(GpuPassTimer::name(static_cast<GpuPassTimer::Pass>(i)));
        ImGui::SameLine(kValueColumn * scale);
        char label[16];
        std::snprintf(label, sizeof(label), "%.2f ms", static_cast<double>(ms));
        ImGui::ProgressBar(ms / total, ImVec2(-1.0f, 0.0f), label);
    }
    ImGui::Text("Total");
    ImGui::SameLine(kValueColumn * scale);
    ImGui::Text("%.2f ms", static_cast<double>(m_passTotalMs));

    ImGui::SeparatorText("Terrain");
    const Terrain::Stats& stats = m_terrain->stats();
    const auto row = [scale](const char* name, const char* format, auto... values) {
        ImGui::TextUnformatted(name);
        ImGui::SameLine(kValueColumn * scale);
        ImGui::Text(format, values...);
    };
    row("Triangles", "%.2fM (%d draws)", static_cast<double>(stats.triangles) / 1e6, stats.drawCalls);
    row("Shadow passes", "%.2fM (%d draws)", static_cast<double>(stats.shadowTriangles) / 1e6, stats.shadowDrawCalls);
    row("Reflection", "%.2fM (%d draws)", static_cast<double>(stats.reflectionTriangles) / 1e6,
        stats.reflectionDrawCalls);
    row("Tiles", "%d resident, %llu generated", stats.residentTiles,
        static_cast<unsigned long long>(stats.tilesGenerated));
    row("Streaming", "%zu queued, %d uploaded", stats.outstandingTiles, stats.uploadsThisFrame);
}

} // namespace emerald
