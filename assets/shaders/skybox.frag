#version 330 core

in vec2 vNdc;
out vec4 fragColour;

uniform samplerCube uSkyCube;
uniform mat4 uInvViewProjRotation; // inverse of projection * view without translation
uniform vec3 uSunDir;
uniform vec3 uSunDiscColour;       // linear, already attenuated by the atmosphere and exposed
uniform vec3 uMoonDir;
uniform vec3 uMoonDiscColour;      // linear and exposed; 0 when the moon is down
uniform float uStarBrightness;     // 0 by day, 1 in full darkness
uniform mat3 uStarRotation;        // world directions to the rotating celestial sphere
uniform float uPixelAngle;         // radians per pixel, so stars stay about a pixel wide

const float kSunAngularRadius = 0.0047;  // matches sky.frag
const float kMoonAngularRadius = 0.008; // drawn larger than the real 0.0045, as the eye sees it
const float kStarCells = 150.0;          // star grid cells per unit of direction
const float kStarDensity = 0.035;        // fraction of cells holding a star

vec3 hash3(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

// One star at most per cell of a grid around the celestial sphere, kept away from the cell edges
// so it is never cut off. Magnitudes follow a steep power law: few bright stars, many faint.
vec3 stars(vec3 dir)
{
    vec3 p = dir * kStarCells;
    vec3 cell = floor(p);
    vec3 h = hash3(cell);
    if (h.x > kStarDensity) {
        return vec3(0.0);
    }
    vec3 centre = normalize(cell + 0.25 + 0.5 * hash3(cell + 17.0));
    float angle = length(centre - dir);
    float width = max(uPixelAngle * 0.6, 0.0002);
    float brightness = 0.02 + 0.6 * pow(h.y, 12.0);
    vec3 tint = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.85, 0.65), h.z);
    return tint * brightness * exp(-angle * angle / (width * width));
}

void main()
{
    vec4 far = uInvViewProjRotation * vec4(vNdc, 1.0, 1.0);
    vec3 ray = normalize(far.xyz / far.w);
    vec3 colour = texture(uSkyCube, ray).rgb;

    // Stars dim towards the horizon, where the air is thickest.
    if (uStarBrightness > 0.0 && ray.y > 0.0) {
        colour += stars(uStarRotation * ray) * uStarBrightness * smoothstep(0.0, 0.25, ray.y);
    }

    // Sun disc with limb darkening, anti-aliased over about one pixel.
    float r = acos(clamp(dot(ray, uSunDir), -1.0, 1.0)) / kSunAngularRadius;
    float edge = fwidth(r);
    float limb = 1.0 - 0.6 * (1.0 - sqrt(max(1.0 - r * r, 0.0)));
    colour += uSunDiscColour * limb * (1.0 - smoothstep(1.0 - edge, 1.0 + edge, r));

    // Full moon: a grey disc with darker patches standing in for the maria, and a faint halo.
    float m = acos(clamp(dot(ray, uMoonDir), -1.0, 1.0)) / kMoonAngularRadius;
    if (m < 12.0) {
        vec3 side = normalize(cross(uMoonDir, vec3(0.0, 1.0, 0.0)));
        vec3 upward = cross(side, uMoonDir);
        vec2 local = vec2(dot(ray, side), dot(ray, upward)) / kMoonAngularRadius;
        float maria = smoothstep(0.35, 0.0, length(local - vec2(-0.25, 0.3))) * 0.35 +
                      smoothstep(0.3, 0.0, length(local - vec2(0.2, 0.05))) * 0.3 +
                      smoothstep(0.25, 0.0, length(local - vec2(-0.05, -0.35))) * 0.25;
        float medge = fwidth(m);
        float disc = 1.0 - smoothstep(1.0 - medge, 1.0 + medge, m);
        float halo = 0.04 * exp(-m * m / 8.0) + 0.008 * exp(-m * m / 40.0);
        colour += uMoonDiscColour * ((1.0 - maria) * disc + halo);
    }

    fragColour = vec4(colour, 1.0); // linear HDR, tone mapped in tonemap.frag
}
