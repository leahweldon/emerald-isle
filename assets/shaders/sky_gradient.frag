#version 330 core

in vec2 vUv;
out vec4 fragColour;

uniform mat4 uInvViewProjRotation; // inverse of projection * rotation-only view
uniform vec3 uSunDir;
uniform vec3 uSunColour;
uniform vec3 uHorizonColour;
uniform vec3 uZenithColour;

// Lighting is linear; the default framebuffer expects sRGB.
vec3 linearToSrgb(vec3 c)
{
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main()
{
    // World-space view ray for this pixel.
    vec4 far = uInvViewProjRotation * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 ray = normalize(far.xyz / far.w);

    float up = max(ray.y, 0.0);
    // Gentle start so the haze above the horizon mirrors the sea fading into it below.
    vec3 colour = mix(uHorizonColour, uZenithColour, 1.0 - exp(-2.5 * up));

    // Sun disc (~1 degree radius) and glow, in the same direction used for lighting.
    float cosAngle = dot(ray, uSunDir);
    float disc = smoothstep(0.99980, 0.99986, cosAngle);
    float glow = 0.35 * pow(max(cosAngle, 0.0), 300.0) + 0.12 * pow(max(cosAngle, 0.0), 12.0);
    colour += uSunColour * (glow + disc);

    fragColour = vec4(linearToSrgb(colour), 1.0);
}
