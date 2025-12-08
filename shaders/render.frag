#version 430 core
in vec2 texCoord;
out vec4 fragColor;

layout(binding = 0, rgba32f) uniform image3D imgInput;
uniform float sharpenAmount;

vec4 compositePixel(ivec2 pos, ivec3 size) {
    vec4 color = vec4(1.0);
    for (int z = 0; z < size.z; ++z) {
        vec4 voxel = imageLoad(imgInput, ivec3(pos, z));
        if (voxel.a > 0.01) {
            color = mix(color, voxel, voxel.a);
        }
    }
    return color;
}

void main() {
    ivec3 size = imageSize(imgInput);
    ivec2 pixelPos = ivec2(texCoord * vec2(size.xy));
    
    vec4 base = compositePixel(pixelPos, size);

    if (sharpenAmount > 0.001) {
        vec3 blur = vec3(0.0);
        int count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                ivec2 nPos = pixelPos + ivec2(dx, dy);
                nPos.x = clamp(nPos.x, 0, size.x - 1);
                nPos.y = clamp(nPos.y, 0, size.y - 1);
                vec4 nCol = compositePixel(nPos, size);
                blur += nCol.rgb;
                count++;
            }
        }
        if (count > 0) blur /= float(count);
        vec3 sharpened = clamp(base.rgb + sharpenAmount * (base.rgb - blur), 0.0, 1.0);
        fragColor = vec4(sharpened, 1.0);
    } else {
        fragColor = base;
    }
}
