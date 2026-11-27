#pragma once

#include "render/Camera.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <string>

namespace emerald {

class InputSource;

// Drives a Camera from input. Free-fly: WASD move, Q/E down/up, arrows turn and pitch, Shift
// faster, [ ] or - = scale speed (which otherwise follows height above the ground), two-finger scroll (or wheel) turns, mouse look with the right
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

    void update(InputSource& window, float deltaSeconds);

    // Touch gestures, as drags in CSS pixels and pinches as a scale (> 1 is fingers apart).
    // Orbiting: a drag rotates around the target, a pinch moves closer or further, a two-finger
    // drag moves the target; the automatic rotation stops. Free-fly: a drag looks around, a pinch
    // moves forward or back, a two-finger drag moves sideways and up or down.
    void touchDrag(const glm::vec2& pixels);
    void touchPinch(float scale);
    void touchPan(const glm::vec2& pixels);

    Mode mode() const { return m_mode; }
    // Current free-fly speed in m/s: proportional to height above the ground, times the [ ] multiplier
    // and the Shift boost while held.
    float effectiveSpeed() const { return m_effectiveSpeed; }
    // The [ ] multiplier on free-fly speed, clamped to 1/32-32.
    float speedMultiplier() const { return m_speedMultiplier; }
    void setSpeedMultiplier(float multiplier);
    // Pitches an orbit this far below its target (radians), so land rather than sky fills a
    // portrait screen.
    void setOrbitTilt(float tilt);

    // The current pose as a ViewPreset initialiser line, for src/render/ViewPresets.cpp.
    std::string presetLine() const;

private:
    void updateFreeFly(InputSource& window, float dt);
    void updateOrbit(float dt);
    void applyOrbit();
    void applyOrbitAboveGround();
    void keepAboveGround();
    float heightAboveGround() const;
    void updateNearPlane();
    // First point where the view ray meets the ground or sea, or a fixed distance ahead.
    glm::dvec3 lookTarget() const;

    Camera& m_camera;
    std::function<double(double, double)> m_groundHeight;
    Mode m_mode = Mode::Orbit;

    // Free-fly state.
    glm::vec3 m_velocity{0.0f};
    float m_speedMultiplier = 1.0f; // [ ] or - =
    float m_altitude = 0.0f;        // smoothed height above ground or sea, metres
    float m_effectiveSpeed = 0.0f;
    glm::vec2 m_scrollTurn{0.0f}; // smoothed scroll look rate, radians per second
    float m_autoflySpeed = 0.0f;  // > 0 while autoflying

    // Orbit state.
    glm::dvec3 m_orbitTarget{0.0};
    float m_orbitAzimuth = 0.0f;
    float m_orbitElevation = 0.0f;
    float m_orbitDistance = 1.0f;
    bool m_orbitAnimate = false;
    float m_orbitTilt = 0.0f;
};

} // namespace emerald
