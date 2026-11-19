#version 330 core

layout(location = 0) in vec2 aPosition; // ring grid, relative to the camera in XZ

uniform mat4 uView;          // rotation only
uniform mat4 uProjection;
uniform float uCameraHeight;
uniform vec2 uCameraXZ;

// Gerstner waves (see SeaSurface): wave vector x, z, amplitude, angular frequency.
uniform vec4 uWaves[4];
uniform vec4 uWaveSteepness;
uniform vec2 uWaveOrigin;    // camera XZ wrapped to the wave period
uniform float uWaveTime;

out vec3 vRelPos;    // relative to the camera
out vec2 vWorldXZ;
out vec2 vWaveXZ;    // wrapped world XZ, for ripple textures
out vec3 vWaveNormal;

void main()
{
    vec2 local = aPosition;
    vec2 wrapped = uWaveOrigin + local;
    float distance = length(local);

    // GPU Gems 1, ch. 1. Each wave fades out from 3 to 8 wavelengths away, while the rings are
    // still several vertices per wavelength.
    vec3 offset = vec3(0.0);
    vec3 normal = vec3(0.0, 1.0, 0.0);
    for (int i = 0; i < 4; ++i) {
        vec2 k = uWaves[i].xy;
        float wavelength = 6.2831853 / length(k);
        float amplitude = uWaves[i].z * (1.0 - smoothstep(3.0 * wavelength, 8.0 * wavelength, distance));
        float phase = dot(k, wrapped) - uWaves[i].w * uWaveTime;
        vec2 dir = normalize(k);
        float c = cos(phase);
        float s = sin(phase);
        float q = uWaveSteepness[i];
        offset += vec3(q * amplitude * dir.x * c, amplitude * s, q * amplitude * dir.y * c);
        float ka = length(k) * amplitude;
        normal += vec3(-dir.x * ka * c, -q * ka * s, -dir.y * ka * c);
    }

    vec2 xz = local + offset.xz;
    vRelPos = vec3(xz.x, offset.y - uCameraHeight, xz.y);
    vWorldXZ = xz + uCameraXZ;
    vWaveXZ = uWaveOrigin + xz;
    vWaveNormal = normalize(normal);
    gl_Position = uProjection * uView * vec4(vRelPos, 1.0);
}
