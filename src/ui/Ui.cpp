#include "ui/Ui.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace emerald {

namespace {

constexpr float kFontSize = 15.0f;        // points, at UI scale 1
constexpr float kReferenceHeight = 720.0f; // headless: rows of a window at UI scale 1

ImVec4 rgb(int r, int g, int b, float a = 1.0f)
{
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f, a);
}

} // namespace

Ui::Ui(GLFWwindow* window, bool headless, int framebufferHeight, const std::filesystem::path& fontPath)
    : m_headless(headless)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // no imgui.ini written next to the app
    // No keyboard navigation: the panel only takes keys while a widget is active, so WASD and
    // the shortcuts keep working after clicking it.

    // Pixels per display unit: 2 on a Retina Mac, where windows are sized in points.
    int windowWidth = 0;
    int windowHeight = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &pixelWidth, &pixelHeight);
    const float pixelRatio = headless || windowWidth <= 0
                                 ? 1.0f
                                 : static_cast<float>(pixelWidth) / static_cast<float>(windowWidth);
    float contentScale = 1.0f;
    glfwGetWindowContentScale(window, &contentScale, nullptr);
    if (headless) {
        m_scale = std::max(static_cast<float>(framebufferHeight) / kReferenceHeight, 1.0f);
    } else {
        m_scale = pixelRatio > 1.01f ? 1.0f : std::max(contentScale, 1.0f);
    }

    // The font is rasterised at full pixel resolution and drawn back down to display units, so
    // text stays sharp on HiDPI screens.
    std::error_code ec;
    ImFontConfig config;
    config.OversampleH = 2;
    const float pixels = kFontSize * m_scale * pixelRatio;
    if (std::filesystem::exists(fontPath, ec) &&
        io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), pixels, &config) != nullptr) {
        io.FontGlobalScale = 1.0f / pixelRatio;
    } else {
        std::fprintf(stderr, "UI font not found (%s); using the built-in font\n", fontPath.string().c_str());
        config.SizePixels = pixels;
        io.Fonts->AddFontDefault(&config);
        io.FontGlobalScale = 1.0f / pixelRatio;
    }
    applyStyle();
    ImGui::GetStyle().ScaleAllSizes(m_scale);

    ImGui_ImplGlfw_InitForOpenGL(window, true); // chains to the window's own input callbacks
    ImGui_ImplOpenGL3_Init("#version 330 core");
}

Ui::~Ui()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Ui::applyStyle()
{
    // Compact dark panel with an emerald accent.
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(8.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 12.0f;
    style.GrabMinSize = 9.0f;
    style.SeparatorTextBorderSize = 1.0f;

    ImVec4* c = style.Colors;
    const ImVec4 accent = rgb(46, 160, 103);
    const ImVec4 accentHover = rgb(62, 184, 122);
    const ImVec4 accentActive = rgb(34, 130, 84);
    c[ImGuiCol_WindowBg] = rgb(20, 24, 26, 0.92f);
    c[ImGuiCol_ChildBg] = rgb(28, 33, 36, 0.6f);
    c[ImGuiCol_PopupBg] = rgb(24, 28, 31, 0.97f);
    c[ImGuiCol_Border] = rgb(60, 68, 72, 0.6f);
    c[ImGuiCol_Text] = rgb(226, 232, 230);
    c[ImGuiCol_TextDisabled] = rgb(130, 140, 138);
    c[ImGuiCol_FrameBg] = rgb(40, 47, 51);
    c[ImGuiCol_FrameBgHovered] = rgb(52, 61, 66);
    c[ImGuiCol_FrameBgActive] = rgb(60, 71, 76);
    c[ImGuiCol_TitleBg] = rgb(20, 24, 26);
    c[ImGuiCol_TitleBgActive] = rgb(24, 32, 30);
    c[ImGuiCol_Header] = rgb(36, 58, 48);
    c[ImGuiCol_HeaderHovered] = rgb(44, 78, 62);
    c[ImGuiCol_HeaderActive] = rgb(50, 94, 72);
    c[ImGuiCol_Button] = rgb(40, 52, 50);
    c[ImGuiCol_ButtonHovered] = accentActive;
    c[ImGuiCol_ButtonActive] = accent;
    c[ImGuiCol_CheckMark] = accentHover;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;
    c[ImGuiCol_Separator] = rgb(60, 68, 72, 0.8f);
    c[ImGuiCol_PlotLines] = accentHover;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_TextSelectedBg] = rgb(46, 160, 103, 0.35f);
    c[ImGuiCol_NavCursor] = accentHover;
}

bool Ui::beginFrame(int framebufferWidth, int framebufferHeight)
{
    m_frameStarted = false;
    if (!m_visible) {
        // Events keep arriving through the callbacks; do not let them pile up while hidden.
        ImGui::GetIO().ClearEventsQueue();
        return false;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    if (m_headless) {
        // Drawn into the offscreen image, not the (hidden) window.
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }
    ImGui::NewFrame();
    m_frameStarted = true;
    return true;
}

void Ui::draw()
{
    if (!m_frameStarted) {
        return;
    }
    m_frameStarted = false;
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool Ui::wantsMouse() const
{
    return m_visible && ImGui::GetIO().WantCaptureMouse;
}

bool Ui::wantsKeyboard() const
{
    return m_visible && ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace emerald
