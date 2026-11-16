#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
out vec4 fragColour;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;     // towards the sun, normalised
uniform vec3 uSunColour;
uniform vec3 uAmbient;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uHeightScale;
uniform vec2 uEdgeFade;    // radial fade start/end from the origin

const vec3 kSea      = vec3(0.16, 0.27, 0.36);
const vec3 kSand     = vec3(0.76, 0.70, 0.52);
const vec3 kLowland  = vec3(0.30, 0.52, 0.22);
const vec3 kHighland = vec3(0.24, 0.32, 0.17);
const vec3 kPeak     = vec3(0.78, 0.78, 0.76);

// h is height normalised to [0, 1]. Land median is ~0.18, 95th percentile ~0.43.
vec3 heightColour(float h)
{
    vec3 c = mix(kSand, kLowland, smoothstep(0.01, 0.06, h));
    c = mix(c, kHighland, smoothstep(0.25, 0.45, h));
    c = mix(c, kPeak, smoothstep(0.55, 0.75, h));
    // Placeholder sea until the water pass.
    return mix(kSea, c, step(0.002, h));
}

void main()
{
    vec3 n = normalize(vNormal);
    float h = vWorldPos.y / uHeightScale;

    float diffuse = max(dot(n, uSunDir), 0.0);
    vec3 colour = heightColour(h) * (uAmbient + uSunColour * diffuse);

    float dist = length(vWorldPos - uCameraPos);
    float fog = smoothstep(uFogStart, uFogEnd, dist);

    // Dissolve the outer sea so the mesh has no visible edge.
    fog = max(fog, smoothstep(uEdgeFade.x, uEdgeFade.y, length(vWorldPos.xz)));

    // Fog is alpha: blending over the sky pass fades terrain into the sky behind it.
    fragColour = vec4(colour, 1.0 - fog);
}
