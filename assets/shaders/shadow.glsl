// Cascaded shadow lookups for the key light, shared by terrain.frag and sea.frag (see ShadowMaps).

uniform sampler2DArrayShadow uShadowMap;
uniform mat4 uShadowMatrices[4]; // camera-relative position to shadow texture coordinates and depth
uniform vec4 uCascadeStart;      // view depth where each cascade starts and ends
uniform vec4 uCascadeEnd;
uniform vec4 uShadowTexel;       // world size of one shadow texel in each cascade
uniform vec3 uViewForward;
uniform bool uShadowsOff; // panel toggle: everything lit

const float kShadowResolution = 2048.0;
const float kCascadeBlend = 0.15;  // last fraction of each cascade, cross-faded into the next
const float kNormalOffset = 1.5;   // texels, at grazing light
const float kDepthBias = 1.0;      // texels' worth of depth

// Visibility in one cascade: 3 x 3 bilinear comparisons (a smooth 4 x 4 texel filter). The
// receiver is pushed along its normal by up to kNormalOffset texels as the light grazes it, which
// removes acne on slopes without detaching shadows from their casters.
float cascadeVisibility(int cascade, vec3 relPos, vec3 normal, float nDotL)
{
    float texel = uShadowTexel[cascade];
    float offset = kNormalOffset * texel * sqrt(1.0 - nDotL * nDotL);
    vec3 p = (uShadowMatrices[cascade] * vec4(relPos + normal * offset, 1.0)).xyz;
    float reference = p.z - kDepthBias / kShadowResolution;
    float step_ = 1.0 / kShadowResolution;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            sum += texture(uShadowMap, vec4(p.xy + vec2(x, y) * step_, float(cascade), reference));
        }
    }
    return sum / 9.0;
}

// Direct-light visibility from the cascades, cross-faded at every boundary. Past the last
// cascade it fades into farVisibility (1 where nothing farther is known).
float shadowVisibility(vec3 relPos, vec3 normal, float nDotL, float farVisibility)
{
    if (uShadowsOff) {
        return 1.0;
    }
    float depth = dot(relPos, uViewForward);
    if (depth >= uCascadeEnd.w) {
        return farVisibility;
    }
    int cascade = depth < uCascadeEnd.x ? 0 : depth < uCascadeEnd.y ? 1 : depth < uCascadeEnd.z ? 2 : 3;
    float start = uCascadeStart[cascade];
    float end = uCascadeEnd[cascade];
    float blend = smoothstep(end - kCascadeBlend * (end - start), end, depth);
    float visibility = cascadeVisibility(cascade, relPos, normal, nDotL);
    if (blend > 0.0) {
        float next = cascade < 3 ? cascadeVisibility(cascade + 1, relPos, normal, nDotL) : farVisibility;
        visibility = mix(visibility, next, blend);
    }
    return visibility;
}
