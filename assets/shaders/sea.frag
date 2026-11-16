#version 330 core

in vec3 vWorldPos;
out vec4 fragColour;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;     // towards the sun, normalised
uniform vec3 uSunColour;
uniform vec3 uAmbient;
uniform float uFogStart;
uniform float uFogEnd;

const vec3 kSea = vec3(0.04, 0.20, 0.23);

void main()
{
    const vec3 n = vec3(0.0, 1.0, 0.0);
    vec3 toCamera = normalize(uCameraPos - vWorldPos);

    float diffuse = max(dot(n, uSunDir), 0.0);
    // Blinn-Phong glint from the sun.
    float specular = pow(max(dot(n, normalize(uSunDir + toCamera)), 0.0), 256.0);
    vec3 colour = kSea * (uAmbient + uSunColour * diffuse) + uSunColour * specular * 0.4;

    // Same fog as terrain.frag: alpha blends over the sky pass.
    float fog = smoothstep(uFogStart, uFogEnd, length(vWorldPos - uCameraPos));
    fragColour = vec4(colour, 1.0 - fog);
}
