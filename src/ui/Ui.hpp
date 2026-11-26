#pragma once

#include <filesystem>

struct GLFWwindow;

namespace emerald {

// Dear ImGui on the app's window: context, font, HiDPI scale and drawing. While hidden it does no
// per-frame work at all. The UI is drawn after tone mapping into the final framebuffer, so its
// colours are not tone mapped.
class Ui {
public:
    // headless: the final image is an offscreen target of framebufferHeight rows, not the window;
    // the UI is then scaled as if that image were a 720-row window.
    Ui(GLFWwindow* window, bool headless, int framebufferHeight, const std::filesystem::path& fontPath);
    ~Ui();

    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    bool visible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }
    void toggle() { m_visible = !m_visible; }

    // Starts an ImGui frame when visible (widgets may be built until draw()); returns false and
    // drops queued input events when hidden. Sizes are the final framebuffer's, in pixels.
    bool beginFrame(int framebufferWidth, int framebufferHeight);
    // Renders the frame's widgets into the bound framebuffer.
    void draw();

    // Whether ImGui is using the mouse or keyboard, so the camera and shortcuts should not.
    bool wantsMouse() const;
    bool wantsKeyboard() const;

    // UI scale in display units (1 on a Retina Mac, the monitor's scale elsewhere), for sizes.
    float scale() const { return m_scale; }

private:
    void applyStyle();

    bool m_headless;
    bool m_visible = false;
    bool m_frameStarted = false;
    float m_scale = 1.0f;
};

} // namespace emerald
