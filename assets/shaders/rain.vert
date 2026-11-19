#version 330 core

// One camera-facing streak per instance (a raindrop, or a splash droplet with SPLASH defined),
// from the particle's position back along its motion relative to the camera over uStreakTime.
// Streaks are at least kMinWidth pixels wide, with opacity scaled down by the coverage of the
// true width, so distant drops fade instead of aliasing. Each particle stands for many real drops
// (heavy rain has hundreds per cubic metre), so its width counts kCoverageBoost times over.

layout(location = 0) in vec4 aPosition; // box coordinates; diameter (mm), or life left for splashes
layout(location = 1) in vec4 aVelocity; // m/s; life at spawn for splashes

out float vAcross; // -1..1 across the streak
out float vAlong;  // 0 at the tail, 1 at the head
out float vAlpha;
out vec3 vColour;

uniform mat4 uView;          // rotation only
uniform mat4 uProjection;
uniform vec2 uViewportSize;  // pixels
uniform float uPixelAngle;   // radians per pixel
uniform vec3 uCameraVelocity;
uniform float uStreakTime;   // seconds of motion each streak shows
uniform float uOpacity;
uniform vec3 uLightDir;
uniform vec3 uLightColour;
uniform vec3 uAmbient;       // sky irradiance around the drops
uniform float uNight;
uniform float uOvercast;     // no forward glow once cloud hides the sun or moon

#include "rain_common.glsl"

const float kMinWidth = 1.0;   // pixels
const float kNearCull = 0.3;   // metres: closer drops would fill the screen
const float kMaxStreak = 4.0;  // metres, when flying fast
const float kSplashDiameter = 0.002; // each droplet stands for a little spray
const float kCoverageBoost = 25.0;
const float kFaceFade = 6.0;   // metres, at the top and bottom of the box

void main()
{
    vec3 rel = wrapToBox(aPosition.xyz);
#ifdef SPLASH
    float diameter = kSplashDiameter;
    float alpha = aPosition.w > 0.0 ? uOpacity * aPosition.w / max(aVelocity.w, 1e-3) : 0.0;
#else
    float diameter = aPosition.w * 0.001;
    float alpha = uOpacity;
#endif
    vec3 streak = (aVelocity.xyz - uCameraVelocity) * uStreakTime;
    float length_ = length(streak);
    if (length_ > kMaxStreak) {
        streak *= kMaxStreak / length_;
    }
    vec3 head = (uView * vec4(rel, 1.0)).xyz;
    vec3 tail = (uView * vec4(rel - streak, 1.0)).xyz;
    // The box reads as a cylinder: drops fade out before its sides, top and bottom.
    vec3 fromCentre = rel - uBoxCentre;
    alpha *= 1.0 - smoothstep(0.7 * uHalfSize.x, uHalfSize.x, length(rel.xz));
    alpha *= 1.0 - smoothstep(uHalfSize.y - kFaceFade, uHalfSize.y, abs(fromCentre.y));
    if (min(-head.z, -tail.z) < kNearCull || alpha <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // all corners at one point outside the view
        vAcross = vAlong = vAlpha = 0.0;
        vColour = vec3(0.0);
        return;
    }

    vec4 headClip = uProjection * vec4(head, 1.0);
    vec4 tailClip = uProjection * vec4(tail, 1.0);
    vec2 halfViewport = 0.5 * uViewportSize;
    vec2 dir = headClip.xy / headClip.w * halfViewport - tailClip.xy / tailClip.w * halfViewport;
    float pixels = length(dir);
    dir = pixels > 1e-3 ? dir / pixels : vec2(0.0, 1.0);
    vec2 side = vec2(-dir.y, dir.x);

    float width = diameter / (-head.z * uPixelAngle);
    float drawn = max(width, kMinWidth);
    alpha *= min(width * kCoverageBoost, drawn) / drawn;

    // Strip order: tail left, tail right, head left, head right.
    float s = (gl_VertexID & 1) == 0 ? -1.0 : 1.0;
    bool atHead = gl_VertexID >= 2;
    vec4 clip = atHead ? headClip : tailClip;
    vec2 offset = (side * s + dir * (atHead ? 1.0 : -1.0)) * 0.5 * drawn;
    gl_Position = clip + vec4(offset / halfViewport * clip.w, 0.0, 0.0);

    // Drops refract the sky around them, and glow when the light is behind them.
    float forward = pow(max(dot(normalize(rel), uLightDir), 0.0), 8.0);
    vec3 colour = 1.5 * uAmbient + uLightColour * (0.15 + 0.8 * forward * (1.0 - uOvercast));
    float luminance = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    vColour = mix(colour, luminance * vec3(0.70, 0.86, 1.25), 0.85 * uNight); // as terrain.frag
    vAcross = s;
    vAlong = atHead ? 1.0 : 0.0;
    vAlpha = alpha;
}
