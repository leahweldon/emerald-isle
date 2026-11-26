// Shared by the rain update and draw shaders (see Rain). Particles are stored world-fixed modulo
// the box size, so they stay precise anywhere; the box near the camera is that space wrapped
// about the box centre, and a particle leaving one side comes back on the other.

uniform vec3 uBoxOrigin;  // box centre in world space, modulo the box size
uniform vec3 uBoxCentre;  // box centre relative to the camera (below it)
uniform vec3 uHalfSize;   // box half extents, metres

uniform sampler2D uGround;   // terrain height under the box, rendered top down (see Rain)
uniform float uGroundRange;  // depth 0..1 maps to +range..-range about the box centre
uniform bool uGroundValid;   // false when the box is far above any land
uniform float uCameraHeight;

const float kGravity = 9.81;

// Box coordinates to camera-relative, inside the box.
vec3 wrapToBox(vec3 boxPos)
{
    return mod(boxPos - uBoxOrigin + uHalfSize, 2.0 * uHalfSize) - uHalfSize + uBoxCentre;
}

vec3 toBox(vec3 rel)
{
    return mod(rel - uBoxCentre + uBoxOrigin, 2.0 * uHalfSize);
}

// The ground map at uv, bilinearly filtered (by hand where depth textures cannot be filtered).
float groundSample(vec2 uv)
{
#ifdef NO_DEPTH_FILTERING
    vec2 size = vec2(textureSize(uGround, 0));
    vec2 texel = uv * size - 0.5;
    ivec2 base = ivec2(floor(texel));
    vec2 f = texel - floor(texel);
    ivec2 last = ivec2(size) - 1;
    float a = texelFetch(uGround, clamp(base, ivec2(0), last), 0).r;
    float b = texelFetch(uGround, clamp(base + ivec2(1, 0), ivec2(0), last), 0).r;
    float c = texelFetch(uGround, clamp(base + ivec2(0, 1), ivec2(0), last), 0).r;
    float d = texelFetch(uGround, clamp(base + ivec2(1, 1), ivec2(0), last), 0).r;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
#else
    return texture(uGround, uv).r;
#endif
}

// Camera-relative height of the ground or the sea, whichever is higher; land is true for ground.
float surfaceHeight(vec2 relXZ, out bool land)
{
    float sea = -uCameraHeight;
    float ground = -1e9;
    if (uGroundValid) {
        float depth = groundSample(relXZ / uHalfSize.xz * 0.5 + 0.5);
        ground = uBoxCentre.y + (0.5 - depth) * 2.0 * uGroundRange;
    }
    land = ground > sea;
    return max(ground, sea);
}

// Terminal fall speed (m/s) of a drop of diameter d (mm), after Gunn and Kinzer as fitted by Atlas.
float terminalVelocity(float d)
{
    return max(9.65 - 10.3 * exp(-0.6 * d), 0.5);
}

uint pcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Uniform in [0, 1), advancing seed.
float random(inout uint seed)
{
    seed = pcgHash(seed);
    return float(seed >> 8u) / 16777216.0;
}
