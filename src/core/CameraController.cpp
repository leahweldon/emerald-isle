#include "core/CameraController.hpp"

#include "core/Window.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace emerald {

namespace {

constexpr float kMinClearance = 30.0f;     // metres above ground or sea
constexpr float kFastMultiplier = 5.0f;    // with Shift
constexpr float kSpeedStep = 1.25f;        // per [ ] or - = press
constexpr float kMinMultiplier = 1.0f / 32.0f;
constexpr float kMaxMultiplier = 32.0f;
// Free-fly speed follows height above the ground (like a map viewer): slow and precise near the
// terrain, fast high up. ~2.4 km/s at a 1.5 km cruising height crosses 30 km in about 12 s.
constexpr float kSpeedPerMetre = 1.6f;     // m/s of speed per metre of height
constexpr float kMinSpeed = 20.0f;
constexpr float kMaxSpeed = 100000.0f;
constexpr float kAltitudeSmoothing = 2.0f; // 1/s; speed does not jump at cliffs and coastlines
constexpr float kResponse = 6.0f;          // 1/s; velocity reaches ~95% of target in 0.5 s
constexpr float kMouseSensitivity = 0.0025f; // radians per screen pixel
constexpr float kTurnRate = 1.2f;          // radians per second, arrow keys
// Two-finger scroll look. macOS trackpads send many small fractional deltas (and inertia after
// the fingers lift); a per-frame cap stops bursts jumping the view and a short low-pass evens out
// the uneven event rate.
constexpr float kScrollSensitivity = 0.02f; // radians per scroll unit
constexpr float kMaxScrollPerFrame = 15.0f; // scroll units
constexpr float kScrollSmoothing = 25.0f;   // 1/s
constexpr float kOrbitSpeed = 0.05f;       // radians per second
constexpr float kMaxStep = 0.1f;           // seconds; larger frame gaps are clamped
constexpr float kTargetFallback = 20000.0f; // metres, when the view ray misses the ground

} // namespace

CameraController::CameraController(Camera& camera, std::function<double(double, double)> groundHeight)
    : m_camera(camera)
    , m_groundHeight(std::move(groundHeight))
{
}

void CameraController::startOrbit(const glm::dvec3& target, float azimuth, float elevation,
                                  float distance, bool animate)
{
    m_mode = Mode::Orbit;
    m_orbitTarget = target;
    m_orbitAzimuth = azimuth;
    m_orbitElevation = elevation;
    m_orbitDistance = distance;
    m_orbitAnimate = animate;
    applyOrbit();
}

void CameraController::startFreeFly()
{
    m_mode = Mode::FreeFly;
    m_velocity = glm::vec3(0.0f);
    m_altitude = heightAboveGround();
    m_scrollTurn = glm::vec2(0.0f);
    keepAboveGround();
    updateNearPlane();
}

void CameraController::startAutofly(float speed)
{
    startFreeFly();
    m_autoflySpeed = speed;
}

void CameraController::update(Window& window, float deltaSeconds)
{
    const Input& input = window.input();
    const float dt = std::min(deltaSeconds, kMaxStep);

    if (input.wasPressed(GLFW_KEY_C)) {
        if (m_mode == Mode::FreeFly) {
            // Orbit around what the camera is looking at, starting from the current view.
            const glm::dvec3 target = lookTarget();
            const glm::dvec3 offset = m_camera.position - target;
            const double distance = glm::length(offset);
            startOrbit(target, static_cast<float>(std::atan2(offset.x, offset.z)),
                       static_cast<float>(std::asin(offset.y / distance)), static_cast<float>(distance),
                       true);
            window.setCursorCaptured(false);
        } else {
            startFreeFly();
        }
        std::printf("Camera: %s\n", m_mode == Mode::FreeFly ? "free-fly" : "orbit");
    }
    if (input.wasPressed(GLFW_KEY_P)) {
        std::printf("%s\n", presetLine().c_str());
        std::fflush(stdout);
    }

    if (m_autoflySpeed > 0.0f) {
        // Level flight along the heading; ground clearance still lifts the camera over hills.
        const glm::vec3 heading(std::sin(m_camera.yaw), 0.0f, -std::cos(m_camera.yaw));
        m_camera.position += glm::dvec3(heading * (m_autoflySpeed * dt));
        keepAboveGround();
        updateNearPlane();
    } else if (m_mode == Mode::FreeFly) {
        updateFreeFly(window, dt);
    } else {
        updateOrbit(dt);
    }
}

void CameraController::updateFreeFly(Window& window, float dt)
{
    const Input& input = window.input();

    if (input.wasPressed(GLFW_KEY_TAB)) {
        window.setCursorCaptured(!window.cursorCaptured());
    }
    const int faster = (input.wasPressed(GLFW_KEY_RIGHT_BRACKET) ? 1 : 0) +
                       (input.wasPressed(GLFW_KEY_EQUAL) ? 1 : 0);
    const int slower = (input.wasPressed(GLFW_KEY_LEFT_BRACKET) ? 1 : 0) +
                       (input.wasPressed(GLFW_KEY_MINUS) ? 1 : 0);
    if (faster != slower) {
        m_speedMultiplier = std::clamp(m_speedMultiplier * std::pow(kSpeedStep, static_cast<float>(faster - slower)),
                                       kMinMultiplier, kMaxMultiplier);
    }

    // Look: mouse while captured or with the right button held, plus arrow keys.
    if (window.cursorCaptured() || input.isMouseDown(GLFW_MOUSE_BUTTON_RIGHT)) {
        m_camera.yaw += input.mouseDelta().x * kMouseSensitivity;
        m_camera.pitch -= input.mouseDelta().y * kMouseSensitivity;
    }
    // Two-finger scroll drags the scene: fingers right turn the view left, fingers up tilt it down
    // (with macOS natural scrolling). Converted to a rate so the smoothing is frame-rate independent.
    if (dt > 0.0f) {
        const glm::vec2 scroll = glm::clamp(input.scrollDelta(), glm::vec2(-kMaxScrollPerFrame),
                                            glm::vec2(kMaxScrollPerFrame));
        const glm::vec2 targetTurn = scroll * kScrollSensitivity / dt;
        m_scrollTurn += (targetTurn - m_scrollTurn) * (1.0f - std::exp(-kScrollSmoothing * dt));
        m_camera.yaw -= m_scrollTurn.x * dt;
        m_camera.pitch += m_scrollTurn.y * dt;
    }

    const auto axis = [&input](int positive, int negative) {
        return (input.isDown(positive) ? 1.0f : 0.0f) - (input.isDown(negative) ? 1.0f : 0.0f);
    };
    m_camera.yaw += axis(GLFW_KEY_RIGHT, GLFW_KEY_LEFT) * kTurnRate * dt;
    m_camera.pitch += axis(GLFW_KEY_UP, GLFW_KEY_DOWN) * kTurnRate * dt;
    m_camera.clampPitch();

    // Move: target velocity from keys, approached exponentially so acceleration and damping
    // are independent of frame rate.
    glm::vec3 wish = m_camera.forward() * axis(GLFW_KEY_W, GLFW_KEY_S) +
                     m_camera.right() * axis(GLFW_KEY_D, GLFW_KEY_A) +
                     glm::vec3(0.0f, 1.0f, 0.0f) * axis(GLFW_KEY_E, GLFW_KEY_Q);
    if (glm::dot(wish, wish) > 1.0f) {
        wish = glm::normalize(wish);
    }
    const bool fast = input.isDown(GLFW_KEY_LEFT_SHIFT) || input.isDown(GLFW_KEY_RIGHT_SHIFT);
    m_altitude += (heightAboveGround() - m_altitude) * (1.0f - std::exp(-kAltitudeSmoothing * dt));
    const float cruise = std::clamp(kSpeedPerMetre * m_altitude, kMinSpeed, kMaxSpeed) * m_speedMultiplier;
    m_effectiveSpeed = cruise * (fast ? kFastMultiplier : 1.0f);
    const glm::vec3 targetVelocity = wish * m_effectiveSpeed;
    m_velocity += (targetVelocity - m_velocity) * (1.0f - std::exp(-kResponse * dt));
    m_camera.position += glm::dvec3(m_velocity * dt);

    keepAboveGround();
    updateNearPlane();
}

void CameraController::updateOrbit(float dt)
{
    if (m_orbitAnimate) {
        m_orbitAzimuth += kOrbitSpeed * dt;
        applyOrbit();
    }
}

void CameraController::applyOrbit()
{
    m_camera.lookFrom(m_orbitTarget, m_orbitAzimuth, m_orbitElevation, m_orbitDistance);
    // Matches the preset convention: precision scales with how far away the subject is.
    m_camera.nearPlane = m_orbitDistance * 0.01f;
}

void CameraController::keepAboveGround()
{
    const double ground = m_groundHeight(m_camera.position.x, m_camera.position.z);
    if (m_camera.position.y < ground + kMinClearance) {
        m_camera.position.y = ground + kMinClearance;
        m_velocity.y = std::max(m_velocity.y, 0.0f);
    }
}

float CameraController::heightAboveGround() const
{
    return static_cast<float>(
        std::max(m_camera.position.y - m_groundHeight(m_camera.position.x, m_camera.position.z), 0.0));
}

void CameraController::updateNearPlane()
{
    // Large enough for depth precision at the coast, small enough not to clip nearby slopes.
    const auto altitude = static_cast<float>(
        m_camera.position.y - m_groundHeight(m_camera.position.x, m_camera.position.z));
    m_camera.nearPlane = std::clamp(altitude * 0.3f, 2.0f, 5000.0f);
}

void CameraController::setSpeedMultiplier(float multiplier)
{
    m_speedMultiplier = std::clamp(multiplier, kMinMultiplier, kMaxMultiplier);
}

glm::dvec3 CameraController::lookTarget() const
{
    const glm::dvec3 origin = m_camera.position;
    const glm::dvec3 dir(m_camera.forward());
    double t = 0.0;
    double step = std::max(50.0, origin.y * 0.01);
    const auto below = [&](double distance) {
        const glm::dvec3 p = origin + dir * distance;
        return p.y <= m_groundHeight(p.x, p.z);
    };
    while (t < 500000.0) {
        if (below(t + step)) {
            // Refine the crossing between t and t + step.
            double lo = t;
            double hi = t + step;
            for (int i = 0; i < 16; ++i) {
                const double mid = 0.5 * (lo + hi);
                (below(mid) ? hi : lo) = mid;
            }
            return origin + dir * hi;
        }
        t += step;
        step *= 1.02;
    }
    return origin + dir * static_cast<double>(kTargetFallback);
}

std::string CameraController::presetLine() const
{
    const glm::dvec3 target = lookTarget();
    const glm::dvec3 offset = m_camera.position - target;
    const double distance = glm::length(offset);
    const double azimuth = std::atan2(offset.x, offset.z);
    const double elevation = std::asin(offset.y / distance);
    char line[192];
    std::snprintf(line, sizeof(line),
                  "    {\"name\", glm::vec3(%.1ff, %.1ff, %.1ff), %.4ff, %.4ff, %.1ff},",
                  target.x, target.y, target.z, azimuth, elevation, distance);
    return line;
}

} // namespace emerald
