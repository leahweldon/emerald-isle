#version 330 core

// Final pass: the linear HDR scene to the display. Exposure, then a tone curve, then sRGB
// encoding with a little dither against banding in sky gradients. Both curves roll highlights
// off smoothly, so a glow around a low sun stays a gradient instead of clipping:
//   0: Khronos PBR Neutral (default): linear up to 0.76 like the earlier display transform, then
//      compressed keeping hue, desaturating to white only gradually, so warm glows stay warm.
//   1: AgX (Troy Sobotka's, with the polynomial sigmoid fit used by Blender and three.js) with a
//      mild contrast and saturation look: filmic, bending bright colours towards white sooner.

in vec2 vUv;
out vec4 fragColour;

uniform sampler2D uScene;   // resolved, linear, Rec. 709 primaries
uniform float uExposure;    // linear scale
uniform int uToneMapper;    // 0 neutral, 1 AgX

const mat3 kSrgbToRec2020 = mat3(vec3(0.6274, 0.0691, 0.0164), vec3(0.3293, 0.9195, 0.0880),
                                 vec3(0.0433, 0.0113, 0.8956));
const mat3 kRec2020ToSrgb = mat3(vec3(1.6605, -0.1246, -0.0182), vec3(-0.5876, 1.1329, -0.1006),
                                 vec3(-0.0728, -0.0083, 1.1187));
// Columns, as in three.js.
const mat3 kAgxInset = mat3(vec3(0.856627153315983, 0.137318972929847, 0.11189821299995),
                            vec3(0.0951212405381588, 0.761241990602591, 0.0767994186031903),
                            vec3(0.0482516061458583, 0.101439036467562, 0.811302368396859));
const mat3 kAgxOutset = mat3(vec3(1.1271005818144368, -0.1413297634984383, -0.14132976349843826),
                             vec3(-0.11060664309660323, 1.157823702216272, -0.11060664309660294),
                             vec3(-0.016493938717834573, -0.016493938717834257, 1.2519364065950405));
const float kMinEv = -12.47393;
const float kMaxEv = 4.026069;
// Look applied in AgX's encoded space: a little more contrast and saturation than the base
// curve, which keeps blue skies and green grass close to the previous renders.
const float kLookPower = 1.1;
const float kLookSaturation = 1.2;

vec3 agxSigmoid(vec3 x)
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 colour)
{
    colour = kAgxInset * (kSrgbToRec2020 * colour);
    colour = clamp((log2(max(colour, 1e-10)) - kMinEv) / (kMaxEv - kMinEv), 0.0, 1.0);
    colour = pow(max(agxSigmoid(colour), 0.0), vec3(kLookPower));
    float luma = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    colour = luma + kLookSaturation * (colour - luma);
    colour = pow(max(kAgxOutset * colour, 0.0), vec3(2.2));
    return clamp(kRec2020ToSrgb * colour, 0.0, 1.0);
}

vec3 neutral(vec3 colour)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(colour.r, min(colour.g, colour.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    colour -= offset;
    float peak = max(colour.r, max(colour.g, colour.b));
    if (peak < startCompression) {
        return colour;
    }
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    colour *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(colour, vec3(newPeak), g);
}

vec3 linearToSrgb(vec3 c)
{
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// Triangular noise of about one 8-bit step, from the pixel position.
float dither()
{
    vec2 p = gl_FragCoord.xy;
    float a = fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
    float b = fract(sin(dot(p, vec2(39.3468, 11.1353))) * 24634.6345);
    return (a + b - 1.0) / 255.0;
}

void main()
{
    vec3 scene = texture(uScene, vUv).rgb * uExposure;
    vec3 colour = uToneMapper == 1 ? agx(scene) : neutral(scene);
    fragColour = vec4(linearToSrgb(colour) + dither(), 1.0);
}
