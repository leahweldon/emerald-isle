#version 330 core

in vec2 vNdc;
out vec4 fragColour;

uniform samplerCube uSkyCube;
uniform mat4 uInvViewProjRotation; // inverse of projection * view without translation
uniform vec3 uSunDir;
uniform vec3 uSunDiscColour;       // linear, already attenuated by the atmosphere and exposed

const float kSunAngularRadius = 0.0047; // matches sky.frag

// Display transform shared by every pass that writes to the screen: linear below 0.8, then a
// smooth shoulder to 1 so the bright sky around the sun compresses instead of clipping.
vec3 toneMap(vec3 c)
{
    const float knee = 0.8;
    vec3 over = max(c - knee, 0.0);
    return min(c, vec3(knee)) + (1.0 - knee) * (1.0 - exp(-over / (1.0 - knee)));
}

vec3 linearToSrgb(vec3 c)
{
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

void main()
{
    vec4 far = uInvViewProjRotation * vec4(vNdc, 1.0, 1.0);
    vec3 ray = normalize(far.xyz / far.w);
    vec3 colour = texture(uSkyCube, ray).rgb;

    // Sun disc with limb darkening, anti-aliased over about one pixel.
    float r = acos(clamp(dot(ray, uSunDir), -1.0, 1.0)) / kSunAngularRadius;
    float edge = fwidth(r);
    float limb = 1.0 - 0.6 * (1.0 - sqrt(max(1.0 - r * r, 0.0)));
    colour += uSunDiscColour * limb * (1.0 - smoothstep(1.0 - edge, 1.0 + edge, r));

    fragColour = vec4(linearToSrgb(toneMap(colour)), 1.0);
}
