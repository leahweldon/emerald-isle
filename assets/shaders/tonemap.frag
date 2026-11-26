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

#include "tonemap.glsl"

void main()
{
#ifdef TONEMAP_IN_SCENE
    fragColour = vec4(texture(uScene, vUv).rgb, 1.0); // already tone mapped by the scene shaders
#else
    vec3 scene = texture(uScene, vUv).rgb * uExposure;
    vec3 colour = uToneMapper == 1 ? agx(scene) : neutral(scene);
    fragColour = vec4(linearToSrgb(colour) + dither(), 1.0);
#endif
}
