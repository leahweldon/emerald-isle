// The in-app panel: Application members that build the ImGui widgets each frame.

#include "core/Application.hpp"

#include "render/Rain.hpp"
#include "render/Terrain.hpp"
#include "world/SolarPosition.hpp"
#include "ui/Ui.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
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
    if (ImGui::CollapsingHeader("Time & sky")) {
        panelTime();
    }
    if (ImGui::CollapsingHeader("Weather")) {
        panelWeather();
    }
    if (ImGui::CollapsingHeader("Rendering")) {
        panelRendering();
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

void Application::panelTime()
{
    const float scale = m_ui->scale();
    if (m_options.fixedSun) {
        ImGui::TextWrapped("The sun is fixed by --sun-bearing / --sun-elevation.");
        if (ImGui::Button("Follow the clock")) {
            m_options.fixedSun = false;
        }
    } else {
        // Sliders show the clock and date as text; the values underneath are minutes and days.
        const int minutes = static_cast<int>(m_time.minutes());
        char clock[16];
        std::snprintf(clock, sizeof(clock), "%02d:%02d", minutes / 60, minutes % 60);
        float time = static_cast<float>(m_time.minutes());
        if (ImGui::SliderFloat("Time", &time, 0.0f, 1439.0f, clock)) {
            m_time.setMinutes(static_cast<double>(time));
        }
        int month = 0;
        int day = 0;
        monthAndDay(m_time.dayOfYear(), month, day);
        static constexpr const char* kMonths[] = {"January", "February", "March", "April", "May", "June", "July",
                                                  "August", "September", "October", "November", "December"};
        char date[24];
        std::snprintf(date, sizeof(date), "%d %s", day, kMonths[month - 1]);
        int dayOfYear = m_time.dayOfYear();
        if (ImGui::SliderInt("Date", &dayOfYear, 1, 365, date)) {
            m_time.setDayOfYear(dayOfYear);
        }
        float speed = static_cast<float>(m_time.speed());
        if (ImGui::SliderFloat("Speed", &speed, 0.1f, 60.0f, "%.1fx", ImGuiSliderFlags_Logarithmic)) {
            m_time.setSpeed(static_cast<double>(speed));
        }
        bool running = m_time.running();
        if (ImGui::Checkbox("Running", &running)) {
            m_time.setRunning(running);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f min a second", TimeOfDay::kRate * m_time.speed() / 60.0);
    }
    const glm::vec3& sun = m_atmosphere.sunDir;
    const float elevation = glm::degrees(std::asin(std::clamp(sun.y, -1.0f, 1.0f)));
    float bearing = glm::degrees(std::atan2(sun.x, -sun.z));
    bearing += bearing < 0.0f ? 360.0f : 0.0f;
    ImGui::TextUnformatted("Sun");
    ImGui::SameLine(kValueColumn * scale);
    ImGui::Text("%.1f\xc2\xb0 up, bearing %.0f\xc2\xb0", static_cast<double>(elevation), static_cast<double>(bearing));
    ImGui::TextUnformatted("Light");
    ImGui::SameLine(kValueColumn * scale);
    ImGui::TextUnformatted(m_atmosphere.night() > 0.5f ? "moon" : m_atmosphere.haze() > 0.5f ? "low sun, evening haze" : "sun");
}

void Application::panelWeather()
{
    const float scale = m_ui->scale();
    // The slider sets where the rain is heading; it eases there over a few seconds.
    if (ImGui::SliderFloat("Rain", &m_rainTarget, 0.0f, 1.0f, "%.2f")) {
        m_rainLevel = m_rainTarget > 0.0f ? m_rainTarget : m_rainLevel;
    }
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    if (ImGui::Button("Dry", ImVec2(buttonWidth, 0.0f))) {
        m_rainTarget = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Light", ImVec2(buttonWidth, 0.0f))) {
        m_rainTarget = m_rainLevel = kLightRain;
    }
    ImGui::SameLine();
    if (ImGui::Button("Heavy", ImVec2(buttonWidth, 0.0f))) {
        m_rainTarget = m_rainLevel = kHeavyRain;
    }
    ImGui::TextUnformatted("Now");
    ImGui::SameLine(kValueColumn * scale);
    ImGui::Text("rain %.0f%%, cloud %.0f%%, ground %.0f%% wet", static_cast<double>(m_rainIntensity * 100.0f),
                static_cast<double>(m_atmosphere.overcast() * 100.0f), static_cast<double>(m_wetness * 100.0f));
    ImGui::TextUnformatted("Drops");
    ImGui::SameLine(kValueColumn * scale);
    ImGui::Text("%d of %d", m_rain->activeDrops(), Rain::kMaxDrops);
}

void Application::panelRendering()
{
    int mapper = m_options.agx ? 1 : 0;
    if (ImGui::Combo("Tone mapper", &mapper, "PBR Neutral\0AgX\0")) {
        m_options.agx = mapper == 1;
    }
    ImGui::SliderFloat("Exposure", &m_options.exposureEv, -3.0f, 3.0f, "%+.1f EV");
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        m_options.exposureEv = 0.0f;
    }
    // Two columns of switches.
    const float column = ImGui::GetContentRegionAvail().x * 0.5f;
    ImGui::Checkbox("Shadows", &m_features.shadows);
    ImGui::SameLine(column);
    ImGui::BeginDisabled(!m_features.shadows);
    ImGui::Checkbox("Horizon shadows", &m_features.horizonShadows);
    ImGui::EndDisabled();
    ImGui::Checkbox("Reflections", &m_features.reflections);
    ImGui::SameLine(column);
    ImGui::Checkbox("Fog", &m_atmosphere.fog);
    ImGui::Checkbox("Rain particles", &m_features.rainParticles);
    ImGui::TextDisabled("Right-click exposure to reset it");
}

} // namespace emerald
