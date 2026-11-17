#version 330 core

layout(location = 0) in vec3 aPosition; // plane centred on the camera in XZ

uniform mat4 uView;          // rotation only
uniform mat4 uProjection;
uniform float uCameraHeight;
uniform vec2 uCameraXZ;

out vec3 vRelPos;    // relative to the camera
out vec2 vWorldXZ;

void main()
{
    // Already camera-centred in XZ, so the plane always reaches the horizon; sea level is y = 0.
    vRelPos = vec3(aPosition.x, -uCameraHeight, aPosition.z);
    vWorldXZ = aPosition.xz + uCameraXZ;
    gl_Position = uProjection * uView * vec4(vRelPos, 1.0);
}
