#pragma once

#include "render/Camera.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <string>

namespace emerald {

class Window;

// Drives a Camera from input. Free-fly: WASD move, Q/E down/up, arrows turn and pitch, Shift
// faster, [ ] or - = change speed, two-finger scroll (or wheel) turns, mouse look with the right
// button held or after Tab captures the cursor. Orbit: slow rotation around a target.
// C switches mode, P prints a --view preset line.
class CameraController {
public:
    enum class Mode { Orbit, FreeFly };

    // groundHeight returns the world-space surface height at (x, z), never below sea level.
    CameraController(Camera& camera, std::function<double(double, double)> groundHeight);

    // Orbit around target from the preset-style pose; rotates slowly when animate is set.
    void startOrbit(const glm::dvec3& target, float azimuth, float elevation, float distance,
                    bool animate);
    // Free-fly from the camera's current pose.
    void startFreeFly();
    // Free-fly straight and level along the current heading at speed (m/s), for demos and tests.
    void startAutofly(float speed);
    bool autoflying() const { return m_autoflySpeed > 0.0f; }

    void update(Window& window, float deltaSeconds);

    Mode mode() const { return m_mode; }
    float baseSpeed() const { return m_baseSpeed; } // metres per second, before Shift

    // The current pose as a ViewPreset initialiser line, for src/render/ViewPresets.cpp.
    std::string presetLine() const;

private:
    void updateFreeFly(Window& window, float dt);
    void updateOrbit(float dt);
    void applyOrbit();
    void keepAboveGround();
    void updateNearPlane();
    // First point where the view ray meets the ground or sea, or a fixed distance ahead.
    glm::dvec3 lookTarget() const;

    Camera& m_camera;
    std::function<double(double, double)> m_groundHeight;
    Mode m_mode = Mode::Orbit;

    // Free-fly state.
    glm::vec3 m_velocity{0.0f};
    float m_baseSpeed = 300.0f; // metres per second
    glm::vec2 m_scrollTurn{0.0f}; // smoothed scroll look rate, radians per second
    float m_autoflySpeed = 0.0f;  // > 0 while autoflying

    // Orbit state.
    glm::dvec3 m_orbitTarget{0.0};
    float m_orbitAzimuth = 0.0f;
    float m_orbitElevation = 0.0f;
    float m_orbitDistance = 1.0f;
    bool m_orbitAnimate = false;
};

} // namespace emerald
