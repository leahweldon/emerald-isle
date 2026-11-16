#version 330 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldPos;

void main()
{
    // Mesh is built in world space, so no model matrix.
    vWorldPos = aPosition;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}
