#version 330 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uCameraPos;

out vec3 vWorldPos;

void main()
{
    // Follow the camera in XZ so the plane always reaches the horizon.
    vWorldPos = aPosition + vec3(uCameraPos.x, 0.0, uCameraPos.z);
    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}
