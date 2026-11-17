#version 330 core

// Full-screen triangle at the far plane (z = w), so it only fills pixels nothing else covered.
out vec2 vNdc;

void main()
{
    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vNdc = corner * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 1.0, 1.0);
}
