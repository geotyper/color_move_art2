#version 430 core
in vec3 vNormal;
out vec4 fragColor;

uniform vec3 u_lightDir;
uniform bool u_wireframe;

void main() {
    if (u_wireframe) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0); // Black wireframe
    } else {
        vec3 N = normalize(vNormal);
        vec3 L = normalize(u_lightDir);
        
        // Simple Diffuse
        float diff = max(dot(N, L), 0.0);
        
        // Ambient
        float ambient = 0.2;
        
        // Sphere Base Color (Grey-ish or White?)
        vec3 baseColor = vec3(0.8, 0.8, 0.8);
        
        vec3 finalColor = baseColor * (diff + ambient);
        fragColor = vec4(finalColor, 1.0);
    }
}
