#version 330 core

// One camera-facing streak per instance (a raindrop, or a splash droplet with SPLASH defined),
// from the particle's position back along its motion relative to the camera over uStreakTime.
// Streaks are at least kMinWidth radians wide (a pixel at 720p, two at 1440p, so rain looks the
// same at any resolution), with opacity scaled down by the coverage of the true width, so
// distant drops fade instead of aliasing. Each particle stands for many real drops
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
uniform vec2 uTanHalfFov;    // tangents of half the horizontal and vertical field of view
uniform vec3 uCameraVelocity;
uniform float uStreakTime;   // seconds of motion each streak shows
uniform float uOpacity;
uniform vec3 uLightDir;
uniform vec3 uLightColour;
uniform vec3 uAmbient;       // sky irradiance around the drops
uniform float uNight;
uniform float uOvercast;     // no forward glow once cloud hides the sun or moon
uniform samplerCube uSkyCube; // linear sky radiance

#include "rain_common.glsl"

const float kMinWidth = 0.0011; // radians
const float kNearCull = 0.3;   // metres: closer drops would fill the screen
const float kMaxStreak = 4.0;  // metres, when flying fast
const float kSplashDiameter = 0.002; // each droplet stands for a little spray
const float kCoverageBoost = 25.0;
const float kRefracted = 0.5;  // share of the surrounding sky light a drop passes on
const float kForwardSky = 0.4; // share of the light from the sky behind it
const float kFaceFade = 6.0;
const float kNearSoft = 1.5;   // metres: drops nearer than about four times this are softened
const float kFarFaint = 8.0;   // metres: drops fade from here to the edge of the box   // metres, at the top and bottom of the box

// All four corners at one point outside the view: nothing is rasterised.
void cull()
{
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    vAcross = vAlong = vAlpha = 0.0;
    vColour = vec3(0.0);
}

// Whether a segment between two view-space points lies wholly outside one side of the view.
bool outsideView(vec3 a, vec3 b)
{
    vec2 limitA = -a.z * uTanHalfFov;
    vec2 limitB = -b.z * uTanHalfFov;
    return (a.x > limitA.x && b.x > limitB.x) || (a.x < -limitA.x && b.x < -limitB.x) ||
           (a.y > limitA.y && b.y > limitB.y) || (a.y < -limitA.y && b.y < -limitB.y);
}

void main()
{
    // Most particles are outside the view, so they are rejected first, before any shading work.
#ifdef SPLASH
    if (aPosition.w <= 0.0) {
        cull(); // a dead droplet
        return;
    }
#endif
    // Each particle's own width, opacity and streak length, so the rain is not a uniform overlay.
    uint seed = uint(gl_InstanceID) * 2654435761u;
    float widthScale = mix(0.7, 1.4, random(seed));
    float opacityScale = mix(0.55, 1.15, random(seed));
    float lengthScale = mix(0.6, 1.4, random(seed));

    vec3 rel = wrapToBox(aPosition.xyz);
    vec3 streak = (aVelocity.xyz - uCameraVelocity) * uStreakTime * lengthScale;
    float length_ = length(streak);
    if (length_ > kMaxStreak) {
        streak *= kMaxStreak / length_;
    }
    mat3 rotation = mat3(uView);
    vec3 head = rotation * rel;
    vec3 tail = rotation * (rel - streak);
    if (min(-head.z, -tail.z) < kNearCull || outsideView(head, tail)) {
        cull();
        return;
    }

#ifdef SPLASH
    float diameter = kSplashDiameter;
    float alpha = uOpacity * aPosition.w / max(aVelocity.w, 1e-3);
#else
    float diameter = aPosition.w * 0.001;
    float alpha = uOpacity;
#endif
    // Near drops are drawn a little wider and softer, far ones thinner and fainter.
    float distance_ = -head.z;
    float near = 1.0 - smoothstep(kNearSoft, kNearSoft * 4.0, distance_);
    float far = smoothstep(kFarFaint, uHalfSize.x, distance_);
    diameter *= widthScale * (1.0 + 0.6 * near);
    alpha *= opacityScale * (1.0 - 0.4 * near) * (1.0 - 0.55 * far);
    // The box reads as a cylinder: drops fade out before its sides, top and bottom.
    vec3 fromCentre = rel - uBoxCentre;
    alpha *= 1.0 - smoothstep(0.7 * uHalfSize.x, uHalfSize.x, length(rel.xz));
    alpha *= 1.0 - smoothstep(uHalfSize.y - kFaceFade, uHalfSize.y, abs(fromCentre.y));
    if (alpha <= 0.0) {
        cull();
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
    float drawn = max(width, max(kMinWidth * (1.0 - 0.25 * far) / uPixelAngle, 1.0));
    alpha *= min(width * kCoverageBoost, drawn) / drawn;

    // Strip order: tail left, tail right, head left, head right.
    float s = (gl_VertexID & 1) == 0 ? -1.0 : 1.0;
    bool atHead = gl_VertexID >= 2;
    vec4 clip = atHead ? headClip : tailClip;
    vec2 offset = (side * s + dir * (atHead ? 1.0 : -1.0)) * 0.5 * drawn;
    gl_Position = clip + vec4(offset / halfViewport * clip.w, 0.0, 0.0);

    // Light the drop passes on to the eye, added to the scene (streaks never darken it): the sky
    // refracted from all around, and forward scattering of whatever bright light is behind it,
    // the sky just above the line of sight and the sun or moon when they are behind the drop.
    vec3 view = normalize(rel);
    vec3 behind = texture(uSkyCube, normalize(view + vec3(0.0, 0.25, 0.0))).rgb;
    float forward = pow(max(dot(view, uLightDir), 0.0), 8.0);
    vec3 colour = kRefracted * uAmbient + kForwardSky * behind +
                  uLightColour * (0.05 + 0.8 * forward * (1.0 - uOvercast));
    float luminance = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    vColour = mix(colour, luminance * vec3(0.70, 0.86, 1.25), 0.85 * uNight); // as terrain.frag
    vAcross = s;
    vAlong = atHead ? 1.0 : 0.0;
    vAlpha = alpha;
}
