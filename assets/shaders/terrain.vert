#version 330 core

// One shared grid drawn per CDLOD node; heights come from the node's streamed tile.
// With SHADOW_PASS defined it only positions vertices for a depth pass; with REFLECTION_PASS
// it draws the terrain mirrored in the sea (shaded as usual, positioned through uMirror).
layout(location = 0) in vec3 aGrid; // grid x, grid z (0..quads), skirt flag

uniform mat4 uView;          // rotation only
uniform mat4 uProjection;
uniform float uCameraHeight;

uniform sampler2D uHeightTile;
uniform float uTileSamples;  // texels per tile side, including the border
uniform float uTileBorder;
uniform float uTileQuads;

uniform vec2 uNodeOrigin;      // node's -X/-Z corner relative to the camera
uniform vec2 uNodeMaterialOrigin; // same corner in world space modulo the material period
uniform float uNodeSize;
uniform vec2 uMorphRange;      // distance where morphing to the coarser grid starts and ends

#ifdef SHADOW_PASS
uniform mat4 uShadowViewProj;  // camera-relative position to the cascade's clip space
#else
#ifdef REFLECTION_PASS
uniform mat4 uMirror;          // camera-relative position to its mirror image below sea level
#endif
out vec3 vRelPos;    // relative to the camera
out vec2 vWorldXZ;
out float vWorldY;
out vec2 vTileUV;
out float vMorph;
out vec4 vNoise;     // broad, local, patches, macro: material noise, smooth enough per vertex
#endif

// Material noise repeats exactly every kMaterialPeriod metres (each scale divides it), matching
// the per-node wrap of world coordinates. Evaluated per vertex: it is low frequency and costly.
const float kMaterialPeriod = 360000.0;

float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Value noise whose lattice repeats every `period` cells.
float valueNoise(vec2 p, float period)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    vec2 i0 = mod(i, period);
    vec2 i1 = mod(i + 1.0, period);
    return mix(mix(hash(i0), hash(vec2(i1.x, i0.y)), u.x),
               mix(hash(vec2(i0.x, i1.y)), hash(i1), u.x), u.y);
}

// Four octaves, roughly in [0, 1], repeating every kMaterialPeriod metres for any scale that
// divides it.
float fbm(vec2 worldXZ, float scale, vec2 offset)
{
    vec2 p = worldXZ / scale + offset;
    float period = kMaterialPeriod / scale;
    float sum = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; ++i) {
        sum += amplitude * valueNoise(p, period);
        p = p * 2.0 + vec2(1.7, 9.2);
        period *= 2.0;
        amplitude *= 0.5;
    }
    return sum / 0.9375;
}

vec2 tileUV(vec2 grid)
{
    return (grid + uTileBorder + 0.5) / uTileSamples;
}

float heightAt(vec2 grid)
{
    return textureLod(uHeightTile, tileUV(grid), 0.0).r;
}

void main()
{
    float spacing = uNodeSize / uTileQuads;
    vec2 grid = aGrid.xy;

    // Geomorph: odd vertices slide onto the coarser grid as distance approaches the node's range.
    vec2 xz = uNodeOrigin + grid * spacing;
    float dist = length(vec3(xz.x, heightAt(grid) - uCameraHeight, xz.y));
    float morph = clamp((dist - uMorphRange.x) / (uMorphRange.y - uMorphRange.x), 0.0, 1.0);
    grid -= fract(grid * 0.5) * 2.0 * morph;

    xz = uNodeOrigin + grid * spacing;
    float height = heightAt(grid);
    // Skirts hang below the edge to cover gaps against a coarser neighbour.
    height -= aGrid.z * max(2.0 * spacing, 20.0);

    vec3 relPos = vec3(xz.x, height - uCameraHeight, xz.y);
#ifdef SHADOW_PASS
    gl_Position = uShadowViewProj * vec4(relPos, 1.0);
#else
    vRelPos = relPos;
    vWorldXZ = uNodeMaterialOrigin + grid * spacing;
    vWorldY = height;
    vTileUV = tileUV(grid);
    vMorph = morph;
    vNoise = vec4(fbm(vWorldXZ, 24000.0, vec2(0.0)), fbm(vWorldXZ, 6000.0, vec2(17.0)),
                  fbm(vWorldXZ, 2500.0, vec2(5.0)), fbm(vWorldXZ, 40000.0, vec2(3.0)));
#ifdef REFLECTION_PASS
    gl_Position = uProjection * uView * uMirror * vec4(vRelPos, 1.0);
#else
    gl_Position = uProjection * uView * vec4(vRelPos, 1.0);
#endif
#endif
}
