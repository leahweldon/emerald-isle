#include "core/CameraController.hpp"
#include "core/Input.hpp"
#include "render/Camera.hpp"

#include <doctest/doctest.h>

#include <GLFW/glfw3.h>

#include <cmath>

using namespace emerald;

namespace {

constexpr double kClearance = 30.0; // kMinClearance in CameraController.cpp

// Rolling hills, 100 to 700 m.
double hills(double x, double z)
{
    return 400.0 + 300.0 * std::sin(x / 900.0) * std::cos(z / 1300.0);
}

// Input with no window: keys are pressed through Input's callbacks.
class FakeWindow final : public InputSource {
public:
    Input keys;
    bool captured = false;
    const Input& input() const override { return keys; }
    bool cursorCaptured() const override { return captured; }
    void setCursorCaptured(bool c) override { captured = c; }
};

} // namespace

TEST_CASE("Camera: pitch is clamped short of straight up and down")
{
    Camera camera;
    camera.pitch = 3.0f;
    camera.clampPitch();
    CHECK(camera.pitch == Camera::kMaxPitch);
    camera.pitch = -3.0f;
    camera.clampPitch();
    CHECK(camera.pitch == -Camera::kMaxPitch);
    CHECK(Camera::kMaxPitch < 1.5708f);
}

TEST_CASE("Camera: looking around never passes the vertical")
{
    Camera camera;
    camera.position = {0.0, 1000.0, 0.0};
    CameraController controller(camera, hills);
    controller.startFreeFly();
    for (int i = 0; i < 50; ++i) {
        controller.touchDrag({0.0f, 400.0f});
    }
    CHECK(camera.pitch == Camera::kMaxPitch);
    for (int i = 0; i < 100; ++i) {
        controller.touchDrag({0.0f, -400.0f});
    }
    CHECK(camera.pitch == -Camera::kMaxPitch);
    CHECK(std::abs(glm::length(camera.forward()) - 1.0f) < 1e-5f);
}

TEST_CASE("Camera: flying down stops above the ground")
{
    Camera camera;
    camera.position = {0.0, 2000.0, 0.0};
    CameraController controller(camera, hills);
    controller.startFreeFly();
    FakeWindow window;
    window.keys.onKey(GLFW_KEY_Q, GLFW_PRESS); // down
    const auto clearance = [&] { return camera.position.y - hills(camera.position.x, camera.position.z); };
    for (int frame = 0; frame < 600; ++frame) {
        controller.update(window, 1.0f / 60.0f);
        window.keys.endFrame();
        REQUIRE(clearance() >= kClearance - 1e-6);
    }
    CHECK(clearance() == doctest::Approx(kClearance)); // it got down to the limit

    // Then forward as well, over rising and falling ground.
    window.keys.onKey(GLFW_KEY_W, GLFW_PRESS);
    for (int frame = 0; frame < 600; ++frame) {
        controller.update(window, 1.0f / 60.0f);
        window.keys.endFrame();
        REQUIRE(clearance() >= kClearance - 1e-6);
    }
}

TEST_CASE("Camera: autofly holds its height but climbs over hills")
{
    Camera camera;
    camera.position = {0.0, 150.0, 0.0};
    CameraController controller(camera, hills);
    controller.startFreeFly();
    controller.startAutofly(300.0f);
    FakeWindow window;
    for (int frame = 0; frame < 600; ++frame) {
        controller.update(window, 1.0f / 60.0f);
        REQUIRE(camera.position.y >= hills(camera.position.x, camera.position.z) + kClearance - 1e-6);
    }
}

TEST_CASE("Camera: an orbit steepens rather than pass under the ground")
{
    Camera camera;
    CameraController controller(camera, hills);
    controller.startOrbit({0.0, hills(0.0, 0.0), 0.0}, 0.0f, 0.3f, 3000.0f, false);
    // Drag the orbit down towards the horizon and pinch in: the camera stays above the hills.
    for (int i = 0; i < 40; ++i) {
        controller.touchDrag({25.0f, -60.0f});
        controller.touchPinch(1.1f);
        REQUIRE(camera.position.y >= hills(camera.position.x, camera.position.z) + kClearance - 1e-6);
    }
}

TEST_CASE("Camera: touch panning and pinching in free flight keep clear of the ground")
{
    Camera camera;
    camera.position = {0.0, 800.0, 0.0};
    camera.pitch = -0.6f;
    CameraController controller(camera, hills);
    controller.startFreeFly();
    for (int i = 0; i < 60; ++i) {
        controller.touchPinch(1.5f);         // forward and down
        controller.touchPan({0.0f, -80.0f}); // fingers down: the camera goes down
        REQUIRE(camera.position.y >= hills(camera.position.x, camera.position.z) + kClearance - 1e-6);
    }
}
