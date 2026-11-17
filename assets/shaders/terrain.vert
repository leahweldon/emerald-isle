#version 330 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uView;          // rotation only
uniform mat4 uProjection;
uniform vec3 uCameraOffset;  // camera position; this mesh is built in world space

out vec3 vWorldPos;
out vec3 vRelPos;            // relative to the camera

void main()
{
    vWorldPos = aPosition;
    vRelPos = aPosition - uCameraOffset;
    gl_Position = uProjection * uView * vec4(vRelPos, 1.0);
}
