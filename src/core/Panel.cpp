// The in-app panel: Application members that build the ImGui widgets each frame.

#include "core/Application.hpp"

#include "core/CameraController.hpp"
#include "render/OffscreenTarget.hpp"
#include "render/Rain.hpp"
#include "render/ReflectionTarget.hpp"
#include "render/ShadowMaps.hpp"
#include "render/ViewPresets.hpp"
#include "world/HeightField.hpp"
#include "render/Terrain.hpp"
#include "world/SolarPosition.hpp"
#include "platform/Gl.hpp"
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

// Debug colours, matching debugPalette() in terrain.frag (linear; the panel shows them as sRGB).
constexpr float kPalette[9][3] = {{0.85f, 0.20f, 0.20f}, {0.95f, 0.55f, 0.15f}, {0.90f, 0.85f, 0.20f},
                                  {0.35f, 0.80f, 0.30f}, {0.20f, 0.70f, 0.80f}, {0.25f, 0.40f, 0.90f},
                                  {0.60f, 0.35f, 0.85f}, {0.85f, 0.40f, 0.70f}, {0.85f, 0.85f, 0.85f}};

// A colour swatch from the debug palette, then text on the same line.
void swatch(const float* rgb, float scale)
{
    const float size = ImGui::GetTextLineHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const auto srgb = [](float c) { return std::pow(c, 1.0f / 2.2f); };
    ImGui::GetWindowDrawList()->AddRectFilled(
        at, ImVec2(at.x + size, at.y + size),
        ImGui::GetColorU32(ImVec4(srgb(rgb[0]), srgb(rgb[1]), srgb(rgb[2]), 1.0f)), 3.0f * scale);
    ImGui::Dummy(ImVec2(size, size));
    ImGui::SameLine();
}

// A readout's name, then the cursor at the value column.
void label(const char* name, float scale)
{
    ImGui::TextUnformatted(name);
    ImGui::SameLine(kValueColumn * scale);
}

} // namespace

void Application::updateUi()
{
    Input& input = m_window.input();
    input.setBlocked(false, false);
    // F1, H or ` (F1 is a brightness key on Mac keyboards, and some browsers take it), but not
    // while typing into a panel field.
    const bool toggle = input.wasPressed(GLFW_KEY_F1) || input.wasPressed(GLFW_KEY_H) ||
                        input.wasPressed(GLFW_KEY_GRAVE_ACCENT);
    if (toggle && !m_ui->wantsTextInput()) {
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
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    m_ui->setPanelRect(position.x, position.y, size.x, size.y);
    ImGui::PushItemWidth(-kLabelWidth * scale);
    // Debug views asked for on the command line open their section instead of this one.
    const bool debugOn = m_options.debugView != 0 || m_options.debugTiles || m_options.debugWireframe;
    ImGui::SetNextItemOpen(!debugOn, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Performance")) {
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
    if (ImGui::CollapsingHeader("Camera")) {
        panelCamera();
    }
    ImGui::SetNextItemOpen(debugOn, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("Debug views")) {
        panelDebug();
    }
    if (ImGui::CollapsingHeader("Controls")) {
        panelHelp();
    }
    ImGui::PopItemWidth();
    ImGui::TextDisabled("H or F1 hides this panel");
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
    if (!gl::caps().timerQueries) {
        ImGui::TextDisabled("Not available: this browser has no GPU timer queries");
    } else {
        if (ImGui::Checkbox("Split the scene pass", &m_splitScenePass)) {
            m_passMs = {}; // the passes being timed change
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Times terrain, sea, sky and rain drops separately. Each extra timer query\n"
                              "splits the GPU's render pass, which costs about 0.5 ms a frame on macOS.");
        }
        const float total = std::max(m_passTotalMs, 1e-3f);
        for (int i = 0; i < GpuPassTimer::kPassCount; ++i) {
            const auto pass = static_cast<GpuPassTimer::Pass>(i);
            const bool scenePart = pass == GpuPassTimer::Terrain || pass == GpuPassTimer::Sea;
            if ((pass == GpuPassTimer::Scene && m_splitScenePass) || (scenePart && !m_splitScenePass)) {
                continue;
            }
            const float ms = m_passMs[static_cast<std::size_t>(i)];
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(GpuPassTimer::name(pass));
            ImGui::SameLine(kValueColumn * scale);
            char text[16];
            std::snprintf(text, sizeof(text), "%.2f ms", static_cast<double>(ms));
            ImGui::ProgressBar(ms / total, ImVec2(-1.0f, 0.0f), text);
        }
        ImGui::Text("Total");
        ImGui::SameLine(kValueColumn * scale);
        ImGui::Text("%.2f ms", static_cast<double>(m_passTotalMs));
    }

    ImGui::SeparatorText("Terrain");
    const Terrain::Stats& stats = m_terrain->stats();
    label("Triangles", scale);
    ImGui::Text("%.2fM (%d draws)", static_cast<double>(stats.triangles) / 1e6, stats.drawCalls);
    label("Shadow passes", scale);
    ImGui::Text("%.2fM (%d draws)", static_cast<double>(stats.shadowTriangles) / 1e6, stats.shadowDrawCalls);
    label("Reflection", scale);
    ImGui::Text("%.2fM (%d draws)", static_cast<double>(stats.reflectionTriangles) / 1e6,
        stats.reflectionDrawCalls);
    label("Tiles", scale);
    ImGui::Text("%d resident, %llu generated", stats.residentTiles,
        static_cast<unsigned long long>(stats.tilesGenerated));
    label("Streaming", scale);
    ImGui::Text("%zu queued, %d uploaded", stats.outstandingTiles, stats.uploadsThisFrame);
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
    const float scale = m_ui->scale();
    if (ImGui::BeginCombo("Quality", kQualities[static_cast<std::size_t>(m_quality)].name)) {
        for (int i = 0; i < static_cast<int>(kQualities.size()); ++i) {
            if (ImGui::Selectable(kQualities[static_cast<std::size_t>(i)].name, i == m_quality)) {
                applyQuality(i);
            }
        }
        ImGui::EndCombo();
    }
    label("Scene", scale);
    ImGui::Text("%d x %d, %dx MSAA", sceneWidth(), sceneHeight(), m_scene ? m_scene->samples() : 0);
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

void Application::panelCamera()
{
    const float scale = m_ui->scale();
    // Jump to a preset; free-fly from there.
    const char* current = m_options.view.empty() ? "(orbit)" : m_options.view.c_str();
    if (ImGui::BeginCombo("View", current)) {
        for (int i = 0; i < viewPresetCount(); ++i) {
            const ViewPreset& preset = viewPreset(i);
            if (ImGui::Selectable(preset.name, m_options.view == preset.name)) {
                m_options.view = preset.name;
                applyViewPreset(preset, m_camera);
                m_controller->startFreeFly();
            }
        }
        ImGui::EndCombo();
    }
    float multiplier = m_controller->speedMultiplier();
    if (ImGui::SliderFloat("Speed", &multiplier, 1.0f / 32.0f, 32.0f, "%.2fx", ImGuiSliderFlags_Logarithmic)) {
        m_controller->setSpeedMultiplier(multiplier);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        m_controller->setSpeedMultiplier(1.0f);
    }

    const glm::dvec3& p = m_camera.position;
    const double ground = m_heightField->groundHeight(p.x, p.z);
    label("Position", scale);
    ImGui::Text("%.0f, %.0f m", p.x, p.z);
    label("Height", scale);
    ImGui::Text("%.0f m, %.0f m above ground", p.y, p.y - ground);
    float heading = glm::degrees(m_camera.yaw);
    heading -= 360.0f * std::floor(heading / 360.0f);
    label("Heading", scale);
    ImGui::Text("%.0f\xc2\xb0, pitch %.0f\xc2\xb0", static_cast<double>(heading),
        static_cast<double>(glm::degrees(m_camera.pitch)));
    if (m_controller->mode() == CameraController::Mode::FreeFly) {
        const double v = static_cast<double>(m_controller->effectiveSpeed());
        label("Flying", scale);
    ImGui::Text(v >= 1000.0 ? "%.1f km/s" : "%.0f m/s", v >= 1000.0 ? v / 1000.0 : v);
    } else {
        label("Flying", scale);
    ImGui::Text("orbit (C for free-fly)");
    }

    // The pose in the form ViewPresets.cpp takes; also what P prints.
    if (ImGui::Button("Copy pose as a preset", ImVec2(-1.0f, 0.0f))) {
        ImGui::SetClipboardText(m_controller->presetLine().c_str());
        m_copiedAt = m_timer.elapsedSeconds();
    }
    if (m_timer.elapsedSeconds() - m_copiedAt < 2.0) {
        ImGui::TextDisabled("Copied to the clipboard");
    }
}

void Application::panelDebug()
{
    const float scale = m_ui->scale();
    ImGui::RadioButton("Shaded", &m_debug.view, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Level of detail", &m_debug.view, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Cascades", &m_debug.view, 2);
    const float column = ImGui::GetContentRegionAvail().x * 0.5f;
    ImGui::Checkbox("Tile borders", &m_debug.tileBorders);
    if (gl::caps().wireframe) { // not in WebGL
        ImGui::SameLine(column);
        ImGui::Checkbox("Wireframe", &m_debug.wireframe);
    }

    if (m_debug.view == 1) {
        // Node size per CDLOD level; colours blend to the next level where vertices geomorph.
        ImGui::TextDisabled("Node size by level; blends show geomorphing");
        for (int level = 0; level <= kMaxLevel; ++level) {
            swatch(kPalette[level], scale);
            const double km = nodeSize(level) / 1000.0;
            ImGui::Text("%d: %.0f km", level, km);
            if (level % 2 == 0 && level < kMaxLevel) {
                ImGui::SameLine(column);
            }
        }
    } else if (m_debug.view == 2) {
        ImGui::TextDisabled("Shadow cascades by view distance");
        float start = 0.0f;
        for (int cascade = 0; cascade < ShadowMaps::kCascades; ++cascade) {
            swatch(kPalette[cascade * 2], scale);
            const float end = m_shadows->cascadeEnd(cascade);
            ImGui::Text("%d: to %.1f km", cascade, static_cast<double>(end) / 1000.0);
            if (cascade % 2 == 0) {
                ImGui::SameLine(column);
            }
            start = end;
        }
        const float grey[3] = {0.55f, 0.55f, 0.55f};
        swatch(grey, scale);
        ImGui::Text("beyond %.0f km: horizon maps", static_cast<double>(start) / 1000.0);
    }

    ImGui::Checkbox("Reflection texture", &m_debug.reflectionPreview);
    if (m_debug.reflectionPreview && m_reflection->width() > 0) {
        // Linear HDR, half resolution, drawn upside down by GL convention: flipped here.
        const float width = ImGui::GetContentRegionAvail().x;
        const float height = width * static_cast<float>(m_reflection->height()) /
                             static_cast<float>(m_reflection->width());
        ImGui::Image(static_cast<ImTextureID>(m_reflection->texture()), ImVec2(width, height), ImVec2(0.0f, 1.0f),
                     ImVec2(1.0f, 0.0f));
        ImGui::TextDisabled("%d x %d, terrain mirrored in the sea", m_reflection->width(), m_reflection->height());
    }
}

void Application::panelHelp()
{
    struct Control {
        const char* action;
        const char* keys;
    };
    static constexpr Control kControls[] = {
        {"Fly", "W A S D, Q / E down / up"},
        {"Look", "two-finger scroll, or drag with the right button"},
        {"Free look", "Tab, then move; Tab again to release"},
        {"Turn / pitch", "arrow keys"},
        {"Faster", "hold Shift (5x)"},
        {"Speed", "[ / ] or - / ="},
        {"Free-fly / orbit", "C"},
        {"Time", "T pause, , / . 15 min (Shift: 1 hour)"},
        {"Rain", "R on / off, Shift+R light / heavy"},
        {"Print pose", "P"},
        {"Panel", "H, ` or F1"},
        {"Quit", "Esc"},
    };
    if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const Control& control : kControls) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", control.action);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", control.keys);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("While the pointer is over the panel, it gets the mouse.");
}

} // namespace emerald
