#version 330 core

in vec2 vUv;
out vec4 fragColour;

uniform float uTime;
uniform vec2 uResolution;

const vec3 kHorizon = vec3(0.86, 0.90, 0.93);
const vec3 kZenith  = vec3(0.24, 0.45, 0.72);
const vec3 kSun     = vec3(1.00, 0.88, 0.62);

void main()
{
    // Smooth vertical blend from a pale horizon to a deeper blue overhead.
    float height = clamp(vUv.y, 0.0, 1.0);
    vec3 colour = mix(kHorizon, kZenith, pow(height, 0.7));

    // A soft sun that drifts slowly across the sky, corrected for aspect ratio.
    vec2 sunPos = vec2(0.5 + 0.3 * sin(uTime * 0.1), 0.62);
    vec2 offset = (vUv - sunPos) * vec2(uResolution.x / uResolution.y, 1.0);
    float glow = exp(-dot(offset, offset) * 18.0);
    float disc = smoothstep(0.035, 0.03, length(offset));
    colour += kSun * (0.45 * glow + disc);

    fragColour = vec4(colour, 1.0);
}
