#version 330 core

// Transform feedback: advances one raindrop per vertex. Gravity against quadratic air drag, with
// the drag scaled so a drop falls at its terminal velocity in still air and drifts with the wind.
// A drop that lands on the ground or the sea respawns at the top of the box; on land it also
// records the hit, for the splash pass.

layout(location = 0) in vec4 aPosition; // box coordinates (m), diameter (mm)
layout(location = 1) in vec4 aVelocity; // m/s

out vec4 oPosition;
out vec4 oVelocity;
out vec4 oHit; // box coordinates of a landing this step, w = 1; zero otherwise

uniform float uDt;
uniform vec3 uWind;       // m/s
uniform int uActiveDrops; // only drawn drops make splashes
uniform int uFrame;

#include "rain_common.glsl"

void main()
{
    float vt = terminalVelocity(aPosition.w);
    vec3 v = aVelocity.xyz;
    vec3 air = v - uWind;
    v += (vec3(0.0, -kGravity, 0.0) - kGravity / (vt * vt) * length(air) * air) * uDt;

    vec3 before = wrapToBox(aPosition.xyz);
    vec3 p = mod(aPosition.xyz + v * uDt, 2.0 * uHalfSize);
    vec3 after = wrapToBox(p);

    // A landing is a crossing of the ground or sea during this step; drops already below it
    // (inside a hillside in the box, or under coarse terrain still streaming) fall on and wrap.
    oHit = vec4(0.0);
    bool land;
    bool landBefore;
    float surface = surfaceHeight(after.xz, land);
    if (after.y < surface && before.y >= surfaceHeight(before.xz, landBefore) &&
        distance(before, after) < uHalfSize.y) {
        if (land && gl_VertexID < uActiveDrops) {
            oHit = vec4(toBox(vec3(after.x, surface, after.z)), 1.0);
        }
        uint seed = uint(gl_VertexID) * 9781u + uint(uFrame) * 6271u;
        vec3 spawn = vec3(random(seed) * 2.0 - 1.0, 1.0 - 0.02 * random(seed), random(seed) * 2.0 - 1.0);
        p = toBox(spawn * uHalfSize + uBoxCentre);
        v = uWind + vec3(0.0, -vt, 0.0);
    }
    oPosition = vec4(p, aPosition.w);
    oVelocity = vec4(v, 0.0);
}
