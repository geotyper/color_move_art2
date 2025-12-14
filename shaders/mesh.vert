#version 430 core
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec3 vertexColor;

out vec3 vNormal; // View-space or World-space normal
out vec3 vColor;

uniform mat4 u_mvp;
uniform mat4 u_model;

void main() {
    gl_Position = u_mvp * vec4(vertexPosition, 1.0);
    // Transform normal to world space (assuming rigid body, so model matrix is fine)
    // For correct non-uniform scale, use inverse-transpose.
    // Here we just use upper 3x3 of model.
    vNormal = mat3(u_model) * vertexNormal;
    vColor = vertexColor;
}
