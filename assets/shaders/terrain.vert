#version 330 core

// One shared grid drawn per CDLOD node; heights come from the node's streamed tile.
layout(location = 0) in vec3 aGrid; // grid x, grid z (0..quads), skirt flag

uniform mat4 uView;          // rotation only
uniform mat4 uProjection;
uniform float uCameraHeight;

uniform sampler2DArray uHeightTiles;
uniform float uTileSamples;  // texels per tile side, including the border
uniform float uTileBorder;
uniform float uTileQuads;

uniform vec2 uNodeOrigin;      // node's -X/-Z corner relative to the camera
uniform vec2 uNodeMaterialOrigin; // same corner in world space modulo the material period
uniform float uNodeSize;
uniform float uTileLayer;
uniform vec2 uMorphRange;      // distance where morphing to the coarser grid starts and ends

out vec3 vRelPos;    // relative to the camera
out vec2 vWorldXZ;
out float vWorldY;
out vec2 vTileUV;
out float vMorph;

vec2 tileUV(vec2 grid)
{
    return (grid + uTileBorder + 0.5) / uTileSamples;
}

float heightAt(vec2 grid)
{
    return textureLod(uHeightTiles, vec3(tileUV(grid), uTileLayer), 0.0).r;
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

    vRelPos = vec3(xz.x, height - uCameraHeight, xz.y);
    vWorldXZ = uNodeMaterialOrigin + grid * spacing;
    vWorldY = height;
    vTileUV = tileUV(grid);
    vMorph = morph;
    gl_Position = uProjection * uView * vec4(vRelPos, 1.0);
}
