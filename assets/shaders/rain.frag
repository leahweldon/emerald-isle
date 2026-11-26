#version 330 core

in float vAcross;
in float vAlong;
in float vAlpha;
in vec3 vColour;
out vec4 fragColour;

#include "tonemap.glsl"

void main()
{
    // Soft across the streak and fainter towards the tail; added to the scene.
    float alpha = vAlpha * (1.0 - vAcross * vAcross) * mix(0.3, 1.0, vAlong);
    fragColour = vec4(sceneOutput(vColour) * alpha, alpha);
}
