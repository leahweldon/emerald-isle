#version 330 core

// Final pass: the linear HDR scene to the display. Exposure, then a tone curve, then sRGB
// encoding with a little dither against banding in sky gradients. Both curves roll highlights
// off smoothly, so a glow around a low sun stays a gradient instead of clipping:
//   0: Khronos PBR Neutral (default): linear up to 0.76 like the earlier display transform, then
//      compressed keeping hue, desaturating to white only gradually, so warm glows stay warm.
//   1: AgX (Troy Sobotka's, with the polynomial sigmoid fit used by Blender and three.js) with a
//      mild contrast and saturation look: filmic, bending bright colours towards white sooner.
// With uFxaa, FXAA (after Lottes' FXAA 2) on the display colours, in scene texels: luma at the four
// diagonal neighbours gives the edge direction, then two or four taps along it are blended.

in vec2 vUv;
out vec4 fragColour;

uniform sampler2D uScene;   // resolved, linear, Rec. 709 primaries
uniform float uExposure;    // linear scale
uniform int uToneMapper;    // 0 neutral, 1 AgX
uniform bool uFxaa;

#include "tonemap.glsl"

// sRGB-encoded display colour, without dither.
vec3 display(vec2 uv)
{
#ifdef TONEMAP_IN_SCENE
    return texture(uScene, uv).rgb; // already tone mapped by the scene shaders
#else
    vec3 scene = texture(uScene, uv).rgb * uExposure;
    return linearToSrgb(uToneMapper == 1 ? agx(scene) : neutral(scene));
#endif
}

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

vec3 fxaa(vec2 uv)
{
    const float kReduceMul = 1.0 / 8.0;
    const float kReduceMin = 1.0 / 128.0;
    const float kSpanMax = 8.0;
    vec2 texel = 1.0 / vec2(textureSize(uScene, 0));
    vec3 centre = display(uv);
    float m = luma(centre);
    float nw = luma(display(uv + vec2(-1.0, -1.0) * texel));
    float ne = luma(display(uv + vec2(1.0, -1.0) * texel));
    float sw = luma(display(uv + vec2(-1.0, 1.0) * texel));
    float se = luma(display(uv + vec2(1.0, 1.0) * texel));
    float lo = min(m, min(min(nw, ne), min(sw, se)));
    float hi = max(m, max(max(nw, ne), max(sw, se)));
    if (hi - lo < max(0.0312, hi * 0.0625)) {
        return centre; // no edge
    }
    vec2 dir = vec2((sw + se) - (nw + ne), (nw + sw) - (ne + se));
    float reduce = max((nw + ne + sw + se) * 0.25 * kReduceMul, kReduceMin);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -kSpanMax, kSpanMax) * texel;
    vec3 a = 0.5 * (display(uv + dir * (1.0 / 3.0 - 0.5)) + display(uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 b = 0.5 * a + 0.25 * (display(uv - dir * 0.5) + display(uv + dir * 0.5));
    float lb = luma(b);
    return lb < lo || lb > hi ? a : b;
}

void main()
{
    vec3 colour = uFxaa ? fxaa(vUv) : display(vUv);
#ifdef TONEMAP_IN_SCENE
    fragColour = vec4(colour, 1.0); // dithered by the scene shaders
#else
    fragColour = vec4(colour + dither(), 1.0);
#endif
}
