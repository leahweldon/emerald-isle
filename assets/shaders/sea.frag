#version 330 core

in vec3 vWorldPos;
out vec4 fragColour;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;     // towards the sun, normalised
uniform vec3 uSunColour;
uniform vec3 uAmbient;
uniform vec3 uHorizonColour;
uniform float uFogDensity;
uniform float uFogFalloff;

const vec3 kSea = vec3(0.04, 0.20, 0.23);

// Height fog: optical depth of exponentially thinning air along the view ray.
float fogAmount(vec3 cameraPos, vec3 worldPos)
{
    vec3 ray = worldPos - cameraPos;
    float dist = length(ray);
    float k = uFogFalloff;
    float base = exp(-k * cameraPos.y);
    float rise = k * ray.y;
    // Integral of exp(-k * height) over the ray, divided by its length.
    float avg = abs(rise) > 1e-4 ? (base - exp(-k * worldPos.y)) / rise : base;
    return 1.0 - exp(-uFogDensity * dist * avg);
}

void main()
{
    const vec3 n = vec3(0.0, 1.0, 0.0);
    vec3 toCamera = normalize(uCameraPos - vWorldPos);

    float diffuse = max(dot(n, uSunDir), 0.0);
    // Blinn-Phong glint from the sun.
    float specular = pow(max(dot(n, normalize(uSunDir + toCamera)), 0.0), 256.0);
    vec3 colour = kSea * (uAmbient + uSunColour * diffuse) + uSunColour * specular * 0.4;

    // Same fog as terrain.frag.
    colour = mix(colour, uHorizonColour, fogAmount(uCameraPos, vWorldPos));
    fragColour = vec4(colour, 1.0);
}
