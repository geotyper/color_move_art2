#version 430 core
in vec3 vNormal;
in vec3 vColor;
out vec4 fragColor;

uniform vec3 u_lightDir;
uniform float u_ambient;
uniform bool u_wireframe;
uniform bool u_debugNormals;

void main() {
    if (u_wireframe) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0); // Black wireframe
    } else if (u_debugNormals) {
        vec3 N = normalize(vNormal);
        fragColor = vec4(N * 0.5 + 0.5, 1.0);
    } else {
        vec3 N = normalize(vNormal);
        vec3 L = normalize(u_lightDir);
        
        // Simple Diffuse
        float diff = max(dot(N, L), 0.0);
        
        vec3 baseColor = vColor;
        
        vec3 finalColor = baseColor * (diff + u_ambient);
        fragColor = vec4(finalColor, 1.0);
    }
}
