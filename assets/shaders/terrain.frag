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
uniform float uExaggeration;

const vec3 kSand     = vec3(0.76, 0.70, 0.52);
const vec3 kLowland  = vec3(0.30, 0.52, 0.22);
const vec3 kHighland = vec3(0.24, 0.32, 0.17);
const vec3 kPeak     = vec3(0.78, 0.78, 0.76);

// Elevation in metres. Land median is ~84 m, 95th percentile ~300 m, peak ~945 m.
vec3 heightColour(float metres)
{
    vec3 c = mix(kSand, kLowland, smoothstep(1.0, 5.0, metres));
    c = mix(c, kHighland, smoothstep(200.0, 400.0, metres));
    return mix(c, kPeak, smoothstep(550.0, 800.0, metres));
}

void main()
{
    vec3 n = normalize(vNormal);
    float metres = vWorldPos.y / uExaggeration;

    float diffuse = max(dot(n, uSunDir), 0.0);
    vec3 colour = heightColour(metres) * (uAmbient + uSunColour * diffuse);

    // Fog is alpha: blending over the sky pass fades terrain into the sky behind it.
    float fog = smoothstep(uFogStart, uFogEnd, length(vWorldPos - uCameraPos));
    fragColour = vec4(colour, 1.0 - fog);
}
