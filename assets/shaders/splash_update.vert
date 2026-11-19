#version 330 core

// Transform feedback: advances one splash droplet per vertex. A dead droplet looks for a drop
// that landed this step among the drops assigned to its group (every uGroups-th drop), and if
// it finds one, leaves its landing point outwards and upwards. Droplets fall, bounce once off
// the ground and fade over their life.

layout(location = 0) in vec4 aPosition; // box coordinates (m), life left (s)
layout(location = 1) in vec4 aVelocity; // m/s, life at spawn (s)

out vec4 oPosition;
out vec4 oVelocity;

uniform samplerBuffer uDrops; // the drops just updated: position, velocity, hit per drop
uniform int uActiveDrops;
uniform int uGroups;
uniform float uDt;
uniform int uFrame;

#include "rain_common.glsl"

void main()
{
    float life = aPosition.w - uDt;
    vec3 p = aPosition.xyz;
    vec4 v = aVelocity;
    if (life > 0.0) {
        v.y -= kGravity * uDt;
        p = mod(p + v.xyz * uDt, 2.0 * uHalfSize);
        vec3 rel = wrapToBox(p);
        bool land;
        float surface = surfaceHeight(rel.xz, land);
        if (rel.y < surface && v.y < 0.0) {
            v.xyz *= vec3(0.5, -0.3, 0.5);
            p = toBox(vec3(rel.x, surface, rel.z));
        }
    } else {
        life = 0.0;
        for (int d = gl_VertexID % uGroups; d < uActiveDrops; d += uGroups) {
            vec4 hit = texelFetch(uDrops, d * 3 + 2);
            if (hit.w > 0.5) {
                uint seed = uint(gl_VertexID) * 7919u + uint(uFrame) * 104729u;
                float angle = 6.2831853 * random(seed);
                // Bigger drops throw their droplets further.
                float size = 0.6 + 0.2 * texelFetch(uDrops, d * 3).w;
                float outward = mix(0.4, 1.4, random(seed)) * size;
                life = mix(0.2, 0.45, random(seed));
                v = vec4(cos(angle) * outward, mix(0.8, 2.0, random(seed)) * size, sin(angle) * outward, life);
                p = toBox(wrapToBox(hit.xyz) + vec3(0.0, 0.02, 0.0));
                break;
            }
        }
    }
    oPosition = vec4(p, life);
    oVelocity = v;
}
