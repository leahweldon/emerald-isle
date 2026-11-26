// The in-app panel: Application members that build the ImGui widgets each frame.

#include "core/Application.hpp"

#include "ui/Ui.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

namespace emerald {

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
    ImGui::SetNextWindowSize(ImVec2(340.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Emerald Isle", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::Text("%.0f FPS  %.2f ms", m_timer.framesPerSecond(), m_timer.frameTimeMs());
    ImGui::TextDisabled("F1 hides this panel");
    ImGui::End();
}

} // namespace emerald
