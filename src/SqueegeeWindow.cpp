#include "SqueegeeWindow.h"
#include <QDebug>
#include <QCoreApplication>
#include <QRandomGenerator>
#include <QtMath>

SqueegeeWindow::SqueegeeWindow()
{
    // Palette 1: Modern Art (Original Calm)
    m_palettes.append({
        QVector3D(0.1f, 0.1f, 0.1f), QVector3D(0.9f, 0.9f, 0.85f), QVector3D(0.8f, 0.2f, 0.2f), QVector3D(0.2f, 0.4f, 0.6f),
        QVector3D(0.9f, 0.7f, 0.1f), QVector3D(0.3f, 0.6f, 0.4f), QVector3D(0.6f, 0.3f, 0.5f), QVector3D(0.2f, 0.2f, 0.3f)
    });
    
    // Palette 2: Modern Earth (High Contrast Interior)
    m_palettes.append({
        QVector3D(0.15f, 0.15f, 0.18f), // Deep Charcoal
        QVector3D(0.85f, 0.35f, 0.15f), // Burnt Rust
        QVector3D(0.92f, 0.88f, 0.82f), // Warm Beige
        QVector3D(0.35f, 0.40f, 0.30f), // Olive Green
        QVector3D(0.30f, 0.35f, 0.40f), // Slate Grey
        QVector3D(0.85f, 0.65f, 0.25f), // Ochre Gold
        QVector3D(0.25f, 0.15f, 0.10f), // Espresso
        QVector3D(0.75f, 0.75f, 0.78f)  // Soft Grey
    });
    
    // Palette 3: Deep Ocean (Vibrant Blues)
    m_palettes.append({
        QVector3D(0.0f, 0.0f, 0.4f), // Midnight Blue
        QVector3D(0.0f, 0.4f, 0.8f), // Royal Blue
        QVector3D(0.0f, 0.8f, 1.0f), // Cyan
        QVector3D(0.0f, 0.6f, 0.6f), // Teal
        QVector3D(1.0f, 1.0f, 1.0f), // White (Contrast)
        QVector3D(0.2f, 0.0f, 0.5f), // Indigo
        QVector3D(0.4f, 0.7f, 0.9f), // Sky Blue
        QVector3D(0.0f, 0.1f, 0.2f)  // Deep Navy
    });
    
    // Palette 4: Vibrant Sunset (Replaces Pastel)
    m_palettes.append({
        QVector3D(1.0f, 0.0f, 0.4f), // Hot Pink
        QVector3D(1.0f, 0.5f, 0.0f), // Bright Orange
        QVector3D(0.5f, 0.0f, 0.5f), // Deep Purple
        QVector3D(1.0f, 0.9f, 0.0f), // Sunshine Yellow
        QVector3D(1.0f, 0.2f, 0.2f), // Red-Orange
        QVector3D(0.8f, 0.0f, 0.8f), // Magenta
        QVector3D(0.2f, 0.0f, 0.4f), // Dark Violet
        QVector3D(1.0f, 0.8f, 0.6f)  // Peach
    });
    
    // Palette 5: Forest & Berry
    m_palettes.append({
        QVector3D(0.1f, 0.3f, 0.2f), QVector3D(0.2f, 0.4f, 0.2f), QVector3D(0.4f, 0.1f, 0.2f), QVector3D(0.6f, 0.2f, 0.3f),
        QVector3D(0.8f, 0.8f, 0.9f), QVector3D(0.3f, 0.3f, 0.4f), QVector3D(0.5f, 0.6f, 0.5f), QVector3D(0.2f, 0.1f, 0.2f)
    });
    
    // Palette 6: Modern Pop Art
    m_palettes.append({
        QVector3D(1.0f, 0.0f, 0.0f), // Red
        QVector3D(0.0f, 0.0f, 1.0f), // Blue
        QVector3D(1.0f, 1.0f, 0.0f), // Yellow
        QVector3D(1.0f, 1.0f, 1.0f), // White
        QVector3D(0.0f, 0.0f, 0.0f), // Black
        QVector3D(1.0f, 0.5f, 0.0f), // Orange
        QVector3D(0.0f, 1.0f, 0.0f), // Green
        QVector3D(1.0f, 0.0f, 1.0f)  // Magenta
    });
    
    // Palette 7: Cyberpunk
    m_palettes.append({
        QVector3D(0.0f, 1.0f, 0.8f), // Neon Cyan
        QVector3D(1.0f, 0.0f, 0.5f), // Neon Pink
        QVector3D(0.5f, 0.0f, 1.0f), // Electric Purple
        QVector3D(0.9f, 1.0f, 0.0f), // Acid Yellow
        QVector3D(0.05f, 0.05f, 0.1f), // Dark Void
        QVector3D(0.0f, 0.0f, 0.2f), // Deep Blue
        QVector3D(1.0f, 0.2f, 0.2f), // Laser Red
        QVector3D(0.8f, 0.8f, 0.9f)  // Chrome
    });
}

SqueegeeWindow::~SqueegeeWindow()
{
    makeCurrent();
    delete m_program;
    delete m_computeGravity;
    delete m_computeSqueegee;
    delete m_computeBlur;
    delete m_computeSaturate;
    delete m_computeCombFix;
    
    if (m_texture3DA) glDeleteTextures(1, &m_texture3DA);
    if (m_texture3DB) glDeleteTextures(1, &m_texture3DB);
    
    m_vbo.destroy();
    m_vao.destroy();
    doneCurrent();
}

void SqueegeeWindow::initializeGL()
{
    initializeOpenGLFunctions();
    
    // Set clear color to White
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    
    initShaders();
    initSimulation();
    initGeometry();
    
    qDebug() << "OpenGL 4.3 Compute Initialized";
    
    generateComposition();
}

void SqueegeeWindow::initShaders()
{
    // Render Program (Raymarch/Stack)
    m_program = new QOpenGLShaderProgram;
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 430 core
        layout(location = 0) in vec3 vertexPosition;
        layout(location = 1) in vec2 vertexTexCoord;
        out vec2 texCoord;
        void main() {
            gl_Position = vec4(vertexPosition, 1.0);
            texCoord = vertexTexCoord;
        }
    )");
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
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
    )");
    m_program->link();

    // Compute Shader: Gravity
    m_computeGravity = new QOpenGLShaderProgram;
    m_computeGravity->addShaderFromSourceCode(QOpenGLShader::Compute, R"(
        #version 430 core
        layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
        
        layout(binding = 0, rgba32f) uniform image3D imgIn;
        layout(binding = 1, rgba32f) uniform image3D imgOut;
        
        void main() {
            ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
            ivec3 size = imageSize(imgIn);
            
            if (pos.x >= size.x || pos.y >= size.y) return;
            
            // Column processing: Bottom to Top
            // We read the entire column first to avoid read/write hazards within the same pass if possible,
            // but for a simple simulation, we can just iterate.
            
            // We need to write to imgOut.
            // Let's copy imgIn to imgOut first (or assume it's done).
            // Actually, we can just do the logic and write the result.
            
            // To simulate falling, we scan from bottom (z=0) up to top.
            // If we find a gap, we pull the pixel above down.
            
            // Load entire column into local array (32 layers is small)
            vec4 column[32];
            for (int z = 0; z < 32; ++z) {
                column[z] = imageLoad(imgIn, ivec3(pos.xy, z));
            }
            
            // Apply Gravity (Bubble sort style or just shift down)
            // Simple approach: For each empty slot, find the nearest non-empty above and move it there.
            for (int z = 0; z < 32; ++z) {
                if (column[z].a < 0.1) { // Empty
                    // Find nearest above
                    for (int above = z + 1; above < 32; ++above) {
                        if (column[above].a > 0.1) {
                            // Move it down
                            column[z] = column[above];
                            column[above] = vec4(0.0); // Clear source
                            break; // Filled this slot, move to next
                        }
                    }
                }
            }
            
            // Store back
            for (int z = 0; z < 32; ++z) {
                imageStore(imgOut, ivec3(pos.xy, z), column[z]);
            }
        }
    )");
    m_computeGravity->link();

    // Compute Shader: Squeegee (Smear)
    m_computeSqueegee = new QOpenGLShaderProgram;
    m_computeSqueegee->addShaderFromSourceCode(QOpenGLShader::Compute, R"(
        #version 430 core
        layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
        
        layout(binding = 0, rgba32f) uniform image3D imgIn;
        layout(binding = 1, rgba32f) uniform image3D imgOut;
        
        uniform vec2 mousePos;
        uniform vec2 lastMousePos;
        uniform float brushSize;
        uniform bool isMouseDown;
        uniform bool toroidal;
        uniform int squeegeeMode; // 0: solid, 1: soft, 2: accurate
        
        // Pseudo-random function
        float rand(vec2 co){
            return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
        }
        
        float segmentDistance(vec2 p, vec2 a, vec2 b) {
            vec2 pa = p - a;
            vec2 ba = b - a;
            float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
            return length(pa - ba * h);
        }
        
        void main() {
            ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
            ivec3 size = imageSize(imgIn);
            
            if (pos.x >= size.x || pos.y >= size.y) return;
            
            vec4 current = imageLoad(imgIn, pos);
            
            if (!isMouseDown) {
                imageStore(imgOut, pos, current);
                return;
            }
            
            // 2D Distance check with Toroidal Wrapping
            float dist = segmentDistance(vec2(pos.xy), lastMousePos, mousePos);
            
            if (toroidal) {
                vec2 fSize = vec2(size.xy);
                // Check 8 neighbors
                for (float dy = -1.0; dy <= 1.0; dy += 1.0) {
                    for (float dx = -1.0; dx <= 1.0; dx += 1.0) {
                        if (dx == 0.0 && dy == 0.0) continue;
                        vec2 offset = vec2(dx, dy) * fSize;
                        dist = min(dist, segmentDistance(vec2(pos.xy), lastMousePos + offset, mousePos + offset));
                    }
                }
            }
            
            if (dist < brushSize) {
                // Squeegee Logic:
                // Shift paint in direction of movement.
                vec2 dir = normalize(mousePos - lastMousePos);
                if (length(mousePos - lastMousePos) < 0.1) dir = vec2(0.0);
                vec2 perp = vec2(-dir.y, dir.x);

                float falloff = 1.0;
                if (squeegeeMode == 1) { // soft: non-linear falloff based on distance
                    float t = clamp(dist / max(brushSize, 0.0001), 0.0, 1.0);
                    falloff = pow(1.0 - t, 0.5);
                }
                
                float baseShift = 2.0;
                float shift = baseShift; // keep displacement consistent across modes

                // Sample from "behind"
                vec2 offsetDir = dir;
                if (squeegeeMode == 2) { // accurate: small perpendicular jitter to fill gaps
                    float jitter = ((int(pos.x + pos.y + pos.z) & 1) == 0) ? 0.5 : -0.5;
                    offsetDir += perp * jitter * 0.05;
                }
                ivec3 samplePos = ivec3(vec2(pos.xy) - offsetDir * shift, pos.z);
                
                if (toroidal) {
                    // Wrap
                    samplePos.x = int(mod(float(samplePos.x), float(size.x)));
                    samplePos.y = int(mod(float(samplePos.y), float(size.y)));
                } else {
                    // Clamp
                    samplePos.x = clamp(samplePos.x, 0, size.x - 1);
                    samplePos.y = clamp(samplePos.y, 0, size.y - 1);
                }
                
                vec4 smearColor = imageLoad(imgIn, samplePos);
                
                // If sampling from paint, pull it.
                if (smearColor.a > 0.01) {
                    vec4 result = smearColor;
                    
                    // Mixing Logic:
                    // If the current voxel has paint (we hit a drop), mix it into the smear.
                    if (current.a > 0.01) {
                        float pickup = (squeegeeMode == 2) ? 0.35 : 0.2;
                        if (squeegeeMode == 1) pickup *= falloff; // softer pickup toward edges
                        result = mix(result, current, pickup);
                    }
                    
                    // Friction/Decay:
                    float decay = 0.995;
                    if (squeegeeMode == 1) decay = mix(0.997, 0.999, 1.0 - falloff); // softer edges decay more
                    if (squeegeeMode == 2) decay = 0.999;       // accurate tries to keep continuity
                    result.a *= decay;
                    
                    imageStore(imgOut, pos, result);
                } else {
                    // Infinite Smear: Don't erase if we are dragging nothing.
                    imageStore(imgOut, pos, current);
                }
            } else {
                imageStore(imgOut, pos, current);
            }
        }
    )");
    m_computeSqueegee->link();

    // Compute Shader: Blur (Box Blur)
    m_computeBlur = new QOpenGLShaderProgram;
    m_computeBlur->addShaderFromSourceCode(QOpenGLShader::Compute, R"(
        #version 430 core
        layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
        
        layout(binding = 0, rgba32f) uniform image3D imgIn;
        layout(binding = 1, rgba32f) uniform image3D imgOut;
        
        void main() {
            ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
            ivec3 size = imageSize(imgIn);
            
            if (pos.x >= size.x || pos.y >= size.y) return;
            
            vec4 sum = vec4(0.0);
            float count = 0.0;
            
            // 3x3 Box Blur (XY plane only, don't blur across layers)
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    ivec3 samplePos = pos + ivec3(dx, dy, 0);
                    
                    // Clamp
                    samplePos.x = clamp(samplePos.x, 0, size.x - 1);
                    samplePos.y = clamp(samplePos.y, 0, size.y - 1);
                    
                    vec4 val = imageLoad(imgIn, samplePos);
                    sum += val;
                    count += 1.0;
                }
            }
            
            imageStore(imgOut, pos, sum / count);
        }
    )");
    m_computeBlur->link();

    // Compute Shader: Saturate
    m_computeSaturate = new QOpenGLShaderProgram;
    m_computeSaturate->addShaderFromSourceCode(QOpenGLShader::Compute, R"(
        #version 430 core
        layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
        
        layout(binding = 0, rgba32f) uniform image3D imgIn;
        layout(binding = 1, rgba32f) uniform image3D imgOut;
        
        vec3 rgb2hsv(vec3 c) {
            vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
            vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
            vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
            float d = q.x - min(q.w, q.y);
            float e = 1.0e-10;
            return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
        }

        vec3 hsv2rgb(vec3 c) {
            vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
            vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
            return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
        }

        void main() {
            ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
            ivec3 size = imageSize(imgIn);
            
            if (pos.x >= size.x || pos.y >= size.y) return;
            
            vec4 color = imageLoad(imgIn, pos);
            if (color.a > 0.01) {
                vec3 hsv = rgb2hsv(color.rgb);
                hsv.y = min(hsv.y * 1.2, 1.0); // Increase saturation by 20%
                color.rgb = hsv2rgb(hsv);
            }
            imageStore(imgOut, pos, color);
        }
    )");
    m_computeSaturate->link();

    // Compute Shader: Comb Fix (fill alternating empty lines/columns)
    m_computeCombFix = new QOpenGLShaderProgram;
    m_computeCombFix->addShaderFromSourceCode(QOpenGLShader::Compute, R"(
        #version 430 core
        layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

        layout(binding = 0, rgba32f) uniform image3D imgIn;
        layout(binding = 1, rgba32f) uniform image3D imgOut;

        bool isFilled(vec4 v) { return v.a > 0.05; }
        float colorDist(vec3 a, vec3 b) { return length(a - b); }

        void main() {
            ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
            ivec3 size = imageSize(imgIn);

            if (pos.x >= size.x || pos.y >= size.y) return;

            vec4 cur = imageLoad(imgIn, pos);
            if (isFilled(cur)) {
                imageStore(imgOut, pos, cur);
                return;
            }

            // Clamp neighbor fetches
            ivec3 leftPos  = ivec3(max(pos.x - 1, 0), pos.y, pos.z);
            ivec3 rightPos = ivec3(min(pos.x + 1, size.x - 1), pos.y, pos.z);
            ivec3 upPos    = ivec3(pos.x, max(pos.y - 1, 0), pos.z);
            ivec3 downPos  = ivec3(pos.x, min(pos.y + 1, size.y - 1), pos.z);

            vec4 left  = imageLoad(imgIn, leftPos);
            vec4 right = imageLoad(imgIn, rightPos);
            vec4 up    = imageLoad(imgIn, upPos);
            vec4 down  = imageLoad(imgIn, downPos);

            bool horizGap = isFilled(left) && isFilled(right) && colorDist(left.rgb, right.rgb) < 0.3;
            bool vertGap  = isFilled(up) && isFilled(down) && colorDist(up.rgb, down.rgb) < 0.3;

            if (horizGap || vertGap) {
                vec4 a = horizGap ? left : up;
                vec4 b = horizGap ? right : down;
                vec4 fill = vec4((a.rgb + b.rgb) * 0.5, min(1.0, max(a.a, b.a)));
                imageStore(imgOut, pos, fill);
            } else {
                imageStore(imgOut, pos, cur);
            }
        }
    )");
    m_computeCombFix->link();
}

void SqueegeeWindow::initSimulation()
{
    int w = width();
    int h = height();
    int d = 32; // 32 Layers
    
    glGenTextures(1, &m_texture3DA);
    glBindTexture(GL_TEXTURE_3D, m_texture3DA);
    glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA32F, w, h, d);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    glGenTextures(1, &m_texture3DB);
    glBindTexture(GL_TEXTURE_3D, m_texture3DB);
    glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA32F, w, h, d);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    // Clear textures
    std::vector<float> clearData(w * h * d * 4, 0.0f);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, w, h, d, GL_RGBA, GL_FLOAT, clearData.data());
    
    glBindTexture(GL_TEXTURE_3D, m_texture3DB);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, w, h, d, GL_RGBA, GL_FLOAT, clearData.data());
}

void SqueegeeWindow::initGeometry()
{
    float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
    };
    
    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices, sizeof(vertices));
    
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, 5 * sizeof(float));
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 2, 5 * sizeof(float));
    
    m_vao.release();
    m_vbo.release();
}

void SqueegeeWindow::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    // Re-init textures if size changes
    if (m_texture3DA) {
        glDeleteTextures(1, &m_texture3DA);
        glDeleteTextures(1, &m_texture3DB);
        initSimulation();
        generateComposition();
    }
}

void SqueegeeWindow::paintGL()
{
    // Compute Pass: Squeegee
    if (m_isMouseDown) {
        m_computeSqueegee->bind();
        
        glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        
        m_computeSqueegee->setUniformValue("mousePos", QVector2D(m_currentMousePos.x(), height() - m_currentMousePos.y()));
        m_computeSqueegee->setUniformValue("lastMousePos", QVector2D(m_lastMousePos.x(), height() - m_lastMousePos.y()));
        m_computeSqueegee->setUniformValue("brushSize", m_brushSize);
        m_computeSqueegee->setUniformValue("isMouseDown", m_isMouseDown);
        m_computeSqueegee->setUniformValue("toroidal", m_toroidal);
        
        glDispatchCompute((width() + 7) / 8, (height() + 7) / 8, 32); // 32 layers
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        
        std::swap(m_texture3DA, m_texture3DB); // Swap
        m_lastMousePos = m_currentMousePos;
        
        m_computeSqueegee->release();
    }
    
    // Compute Pass: Gravity (Run every frame or every N frames)
    {
        m_computeGravity->bind();
        glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        
        // Dispatch 2D grid, Z is handled inside
        glDispatchCompute((width() + 7) / 8, (height() + 7) / 8, 1); 
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        
        std::swap(m_texture3DA, m_texture3DB);
        m_computeGravity->release();
    }
    
    // Render Pass
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    m_program->bind();
    m_program->setUniformValue("sharpenAmount", m_sharpenAmount);
    glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
    
    m_vao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vao.release();
    m_program->release();
    
    update();
}

void SqueegeeWindow::mousePressEvent(QMouseEvent *event)
{
    m_isMouseDown = true;
    m_lastMousePos = QVector2D(event->position().x(), event->position().y());
    m_currentMousePos = m_lastMousePos;
}

void SqueegeeWindow::mouseMoveEvent(QMouseEvent *event)
{
    m_currentMousePos = QVector2D(event->position().x(), event->position().y());
}

void SqueegeeWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_isMouseDown = false;
}

void SqueegeeWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_B) {
        applyBlur();
        qDebug() << "Blur Applied";
    }
}

void SqueegeeWindow::wheelEvent(QWheelEvent *event)
{
    float delta = event->angleDelta().y() / 120.0f;
    m_brushSize += delta * 5.0f;
    if (m_brushSize < 5.0f) m_brushSize = 5.0f;
}

void SqueegeeWindow::generateComposition()
{
    qDebug() << "Generating 3D composition...";
    
    // Clear or Keep
    int w = width();
    int h = height();
    int d = 32;
    std::vector<float> data(w * h * d * 4, 0.0f);

    if (m_keepExisting) {
        glBindTexture(GL_TEXTURE_3D, m_texture3DA);
        glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, data.data());
    } else {
        // Explicitly clear texture if not keeping (though we overwrite data anyway, 
        // uploading 0s first ensures clean slate if we don't fill everything)
        // Actually, we just init data to 0s above.
        // But we should probably clear the texture on GPU too just in case?
        // Uploading 0s is fine.
        glBindTexture(GL_TEXTURE_3D, m_texture3DA);
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, w, h, d, GL_RGBA, GL_FLOAT, data.data());
    }
    
    // Use selected palette
    const QVector<QVector3D>& currentPalette = m_palettes[m_currentPaletteIdx];
    int colorCount = currentPalette.size();
    
    // Determine points to draw
    struct DrawPoint {
        int x, y, z, r;
    };
    QVector<DrawPoint> points;
    
    if (m_genMode == ModeRandom) {
        for (int i = 0; i < m_genDensity; ++i) {
            int cx = QRandomGenerator::global()->bounded(w);
            int cy = QRandomGenerator::global()->bounded(h);
            int cz = QRandomGenerator::global()->bounded(d);
            int r = QRandomGenerator::global()->bounded(5, m_dropMaxSize + 1);
            points.append({cx, cy, cz, r});
        }
    } else { // ModeGrid
        for (int y = m_gridStep / 2; y < h; y += m_gridStep) {
            for (int x = m_gridStep / 2; x < w; x += m_gridStep) {
                int cz = QRandomGenerator::global()->bounded(d);
                // For grid, size is fixed to Max Size or random?
                // Let's make it random up to max size for variety, or fixed?
                // User asked for "Grid with defined step and size of squares".
                // So size should probably be related to Drop Max Size.
                // Let's use Drop Max Size as the size.
                int r = m_dropMaxSize; 
                points.append({x, y, cz, r});
            }
        }
    }
    
    for (const auto& p : points) {
        int cx = p.x;
        int cy = p.y;
        int cz = p.z;
        int r = p.r;
        
        // Concentric Logic
        int rings = 1;
        if (m_genConcentric > 1) {
            rings = QRandomGenerator::global()->bounded(1, m_genConcentric + 1);
        }
        
        for (int ring = 0; ring < rings; ++ring) {
            int currentR = r * (rings - ring) / rings;
            if (currentR < 2) break;

            if (m_depthRadiusScaling) {
                // Scale radius by depth: top layers get smaller radius, deeper layers larger.
                float depthScale = float((d - 1) - cz) / float(d - 1); // 0 at top, 1 at bottom
                currentR = std::max(1, int(std::round(currentR * depthScale)));
            }
            
            int colorIdx = QRandomGenerator::global()->bounded(colorCount);
            QVector3D col = currentPalette[colorIdx];
            
            if (m_genShape == ShapeCircle) {
                // Circle Logic
                if (m_drawBorders) {
                    int borderR = currentR + 1;
                    for (int y = cy - borderR; y <= cy + borderR; ++y) {
                        for (int x = cx - borderR; x <= cx + borderR; ++x) {
                            if (x >= 0 && x < w && y >= 0 && y < h) {
                                float dist = std::sqrt(std::pow(x - cx, 2) + std::pow(y - cy, 2));
                                if (dist <= borderR) {
                                    int idx = (cz * w * h + y * w + x) * 4;
                                    data[idx + 0] = 0.0f;
                                    data[idx + 1] = 0.0f;
                                    data[idx + 2] = 0.0f;
                                    data[idx + 3] = 1.0f;
                                }
                            }
                        }
                    }
                }

                for (int y = cy - currentR; y <= cy + currentR; ++y) {
                    for (int x = cx - currentR; x <= cx + currentR; ++x) {
                        if (x >= 0 && x < w && y >= 0 && y < h) {
                            float dist = std::sqrt(std::pow(x - cx, 2) + std::pow(y - cy, 2));
                            if (dist <= currentR) {
                                int idx = (cz * w * h + y * w + x) * 4;
                                data[idx + 0] = col.x();
                                data[idx + 1] = col.y();
                                data[idx + 2] = col.z();
                                data[idx + 3] = 0.8f;
                            }
                        }
                    }
                }
            } else { // ShapeSquare
                // Square Logic
                // currentR is half-width
                if (m_drawBorders) {
                    int borderR = currentR + 1;
                    for (int y = cy - borderR; y <= cy + borderR; ++y) {
                        for (int x = cx - borderR; x <= cx + borderR; ++x) {
                            if (x >= 0 && x < w && y >= 0 && y < h) {
                                int idx = (cz * w * h + y * w + x) * 4;
                                data[idx + 0] = 0.0f;
                                data[idx + 1] = 0.0f;
                                data[idx + 2] = 0.0f;
                                data[idx + 3] = 1.0f;
                            }
                        }
                    }
                }
                
                for (int y = cy - currentR; y <= cy + currentR; ++y) {
                    for (int x = cx - currentR; x <= cx + currentR; ++x) {
                        if (x >= 0 && x < w && y >= 0 && y < h) {
                            int idx = (cz * w * h + y * w + x) * 4;
                            data[idx + 0] = col.x();
                            data[idx + 1] = col.y();
                            data[idx + 2] = col.z();
                            data[idx + 3] = 0.8f;
                        }
                    }
                }
            }
        }
    }
    
    glBindTexture(GL_TEXTURE_3D, m_texture3DA);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, w, h, d, GL_RGBA, GL_FLOAT, data.data());
    
    // If Preview Mode is ON, skip the squeegee stroke
    if (m_showPreview) {
        return;
    }
    
    // Simulate squeegee stroke (Configurable)
    // We need to run the compute shader loop here.
    
    float rad = qDegreesToRadians(m_genAngle);
    QVector2D dir(std::cos(rad), std::sin(rad));
    
    // Calculate start and end points to cover the screen
    // Center of screen
    QVector2D center(w * 0.5f, h * 0.5f);
    // Diagonal length ensures we go off-screen
    float diag = std::sqrt(float(w*w + h*h));
    
    QVector2D start = center - dir * diag;
    QVector2D end = center + dir * diag;
    
    float squeegeeWidth = width() * m_genWidth;
    
    for (int i = 0; i < m_genPasses; ++i) {
        simulateStroke(start, end, squeegeeWidth);
    }
}

void SqueegeeWindow::regenerateSqueegeeOnly()
{
    // Run only the squeegee stroke on the current texture content.
    if (m_showPreview) {
        return;
    }

    int w = width();
    int h = height();
    if (w <= 0 || h <= 0) {
        return;
    }

    float rad = qDegreesToRadians(m_genAngle);
    QVector2D dir(std::cos(rad), std::sin(rad));

    QVector2D center(w * 0.5f, h * 0.5f);
    float diag = std::sqrt(float(w * w + h * h));

    QVector2D start = center - dir * diag;
    QVector2D end = center + dir * diag;

    float squeegeeWidth = w * m_genWidth;

    for (int i = 0; i < m_genPasses; ++i) {
        simulateStroke(start, end, squeegeeWidth);
    }

    update();
}

void SqueegeeWindow::drawDrop(QVector2D, float, QVector3D) {}

void SqueegeeWindow::simulateStroke(QVector2D start, QVector2D end, float size)
{
    int steps = m_genSteps; // Use configurable steps (Speed)
    QVector2D dir = end - start;
    float len = dir.length();
    dir.normalize();
    
    QVector2D current = start;
    QVector2D last = start;
    
    m_computeSqueegee->bind();
    m_computeSqueegee->setUniformValue("brushSize", size);
    m_computeSqueegee->setUniformValue("isMouseDown", true);
    m_computeSqueegee->setUniformValue("toroidal", m_toroidal);
    m_computeSqueegee->setUniformValue("squeegeeMode", (int)m_squeegeeMode);
    
    for (int i = 0; i < steps; ++i) {
        float t = (float)i / (float)steps;
        current = start + dir * (len * t);
        
        glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        
        m_computeSqueegee->setUniformValue("mousePos", QVector2D(current.x(), height() - current.y()));
        m_computeSqueegee->setUniformValue("lastMousePos", QVector2D(last.x(), height() - last.y()));
        // Uniforms set once above persist if shader is bound, but we re-bind inside loop?
        // Wait, we release gravity then re-bind squeegee. So we must re-set uniforms.
        m_computeSqueegee->setUniformValue("squeegeeMode", (int)m_squeegeeMode);
        
        glDispatchCompute((width() + 7) / 8, (height() + 7) / 8, 32);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        
        std::swap(m_texture3DA, m_texture3DB);
        
        // Apply Gravity immediately after Squeegee step
        // This allows paint to "fall" into lower layers as it moves, creating non-linear decay.
        m_computeGravity->bind();
        glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        
        glDispatchCompute((width() + 7) / 8, (height() + 7) / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        
        std::swap(m_texture3DA, m_texture3DB);
        m_computeGravity->release();
        
        // Re-bind Squeegee for next iteration
        m_computeSqueegee->bind();
        m_computeSqueegee->setUniformValue("brushSize", size);
        m_computeSqueegee->setUniformValue("isMouseDown", true);
        
        // Re-bind Squeegee for next iteration
        m_computeSqueegee->bind();
        m_computeSqueegee->setUniformValue("brushSize", size);
        m_computeSqueegee->setUniformValue("isMouseDown", true);
        m_computeSqueegee->setUniformValue("toroidal", m_toroidal);
        m_computeSqueegee->setUniformValue("squeegeeMode", (int)m_squeegeeMode);
        
        last = current;
    }
    m_computeSqueegee->release();
}

void SqueegeeWindow::applyBlur()
{
    m_computeBlur->bind();
    glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    
    glDispatchCompute((width() + 7) / 8, (height() + 7) / 8, 32);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    
    std::swap(m_texture3DA, m_texture3DB);
    m_computeBlur->release();
    update();
}

void SqueegeeWindow::applyCombFix()
{
    m_computeCombFix->bind();
    glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    glDispatchCompute((width() + 7) / 8, (height() + 7) / 8, 32);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    std::swap(m_texture3DA, m_texture3DB);
    m_computeCombFix->release();
    update();
}

void SqueegeeWindow::applySaturation()
{
    m_computeSaturate->bind();
    glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    
    glDispatchCompute((width() + 7) / 8, (height() + 7) / 8, 32);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    
    std::swap(m_texture3DA, m_texture3DB);
    m_computeSaturate->release();
    update();
}
