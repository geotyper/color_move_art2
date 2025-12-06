#include "SqueegeeWindow.h"
#include <QDebug>
#include <QCoreApplication>
#include <QRandomGenerator>
#include <QtMath>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QStringList>

SqueegeeWindow::SqueegeeWindow(QWidget *parent)
    : QOpenGLWidget(parent)
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

    // Do not auto-generate content on startup; user triggers via UI
    m_hasGenerated = false;
}

void SqueegeeWindow::initShaders()
{
    auto loadSource = [](const QString& name) -> QByteArray {
        QString path = ":/shaders/" + name;
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = f.readAll();
            f.close();
            return data;
        }
        qWarning() << "Failed to open shader from resource" << path;
        return {};
    };

    auto createProgram = [&](QOpenGLShader::ShaderType type, const QString& file) -> QOpenGLShaderProgram* {
        QOpenGLShaderProgram* prog = new QOpenGLShaderProgram;
        QByteArray src = loadSource(file);
        if (!prog->addShaderFromSourceCode(type, src)) {
            qWarning() << "Shader compile failed for" << file << prog->log();
        }
        return prog;
    };

    m_program = new QOpenGLShaderProgram;
    QByteArray vsSrc = loadSource("render.vert");
    QByteArray fsSrc = loadSource("render.frag");
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vsSrc))
        qWarning() << m_program->log();
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fsSrc))
        qWarning() << m_program->log();
    if (!m_program->link()) qWarning() << m_program->log();

    m_computeGravity = createProgram(QOpenGLShader::Compute, "gravity.comp");
    m_computeGravity->link();

    m_computeSqueegee = createProgram(QOpenGLShader::Compute, "squeegee.comp");
    m_computeSqueegee->link();

    m_computeBlur = createProgram(QOpenGLShader::Compute, "blur.comp");
    m_computeBlur->link();

    m_computeSaturate = createProgram(QOpenGLShader::Compute, "saturate.comp");
    m_computeSaturate->link();

    m_computeCombFix = createProgram(QOpenGLShader::Compute, "combfix.comp");
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
        // Only regenerate if user already requested content
        if (m_hasGenerated) {
            generateComposition();
        }
    }
}

void SqueegeeWindow::paintGL()
{
    // Compute Pass: Squeegee
    if (m_isMouseDown) {
        m_computeSqueegee->bind();
        
        glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        
        QVector2D dir = m_currentMousePos - m_lastMousePos;
        if (dir.lengthSquared() < 0.0001f) dir = QVector2D(1.0f, 0.0f);
        BrushNoiseResult n = sampleBrushNoise(m_brushSize, m_currentMousePos, dir);
        QVector2D prevOffset = m_noiseOffsetAccum;
        QVector2D offset = prevOffset * 0.5f + n.offset * 0.5f; // smooth wobble but keep variance
        m_noiseOffsetAccum = offset;
        QVector2D noisyCurrent = m_currentMousePos + offset;
        QVector2D noisyLast = m_lastMousePos + prevOffset; // use previous offset so segment curves
        float brushSizeForShader = (m_brushNoiseMode == NoiseBrushIntensity) ? n.size : m_brushSize;
        
        m_computeSqueegee->setUniformValue("mousePos", QVector2D(noisyCurrent.x(), height() - noisyCurrent.y()));
        m_computeSqueegee->setUniformValue("lastMousePos", QVector2D(noisyLast.x(), height() - noisyLast.y()));
        m_computeSqueegee->setUniformValue("strokeDir", QVector2D(dir.x(), -dir.y())); // flip Y for GL space
        m_computeSqueegee->setUniformValue("brushSize", brushSizeForShader);
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
    
    // Ensure opaque white background
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    m_program->bind();
    m_program->setUniformValue("sharpenAmount", m_sharpenAmount);
    glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
    
    m_vao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vao.release();
    m_vao.release();
    m_program->release();
    
    update();
}

void SqueegeeWindow::mousePressEvent(QMouseEvent *event)
{
    m_isMouseDown = true;
    m_lastMousePos = QVector2D(event->position().x(), event->position().y());
    m_currentMousePos = m_lastMousePos;
    m_noiseOffsetAccum = QVector2D(0.0f, 0.0f);
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
    m_hasGenerated = true;
    
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
    
    generateDrops(data, w, h, d);
    
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
    QVector2D noiseOffsetAccum(0.0f, 0.0f);
    QVector2D noisyLast = last;
    
    m_computeSqueegee->bind();
    m_computeSqueegee->setUniformValue("isMouseDown", true);
    m_computeSqueegee->setUniformValue("toroidal", m_toroidal);
    m_computeSqueegee->setUniformValue("squeegeeMode", (int)m_squeegeeMode);
    
    for (int i = 0; i < steps; ++i) {
        float t = (float)i / (float)steps;
        current = start + dir * (len * t);
        QVector2D stepDir = current - last;
        if (stepDir.lengthSquared() < 0.0001f) stepDir = dir;
        BrushNoiseResult n = sampleBrushNoise(size, current, stepDir);
        QVector2D prevOffset = noiseOffsetAccum;
        QVector2D offset = prevOffset * 0.5f + n.offset * 0.5f;
        noiseOffsetAccum = offset;
        QVector2D noisyCurrent = current + offset;
        noisyLast = last + prevOffset;
        float brushSizeForShader = (m_brushNoiseMode == NoiseBrushIntensity) ? n.size : size;
        
        glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        
        m_computeSqueegee->setUniformValue("mousePos", QVector2D(noisyCurrent.x(), height() - noisyCurrent.y()));
        m_computeSqueegee->setUniformValue("lastMousePos", QVector2D(noisyLast.x(), height() - noisyLast.y()));
        m_computeSqueegee->setUniformValue("strokeDir", QVector2D(dir.x(), -dir.y())); // flip Y for GL space
        m_computeSqueegee->setUniformValue("brushSize", brushSizeForShader);
        
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
        m_computeSqueegee->setUniformValue("brushSize", brushSizeForShader);
        m_computeSqueegee->setUniformValue("isMouseDown", true);
        m_computeSqueegee->setUniformValue("toroidal", m_toroidal);
        m_computeSqueegee->setUniformValue("squeegeeMode", (int)m_squeegeeMode);
        
        last = current;
        noisyLast = noisyCurrent;
    }
    m_computeSqueegee->release();
}

SqueegeeWindow::BrushNoiseResult SqueegeeWindow::sampleBrushNoise(float baseSize, const QVector2D& pos, const QVector2D& dir) const
{
    BrushNoiseResult res;
    res.size = baseSize;

    if (m_brushNoiseMode == NoiseOff || m_brushNoiseStrength <= 0.0001f) {
        return res;
    }

    float scale = std::max(1.0f, m_brushNoiseScale);
    float n = m_noise.fractal(pos.x() / scale, pos.y() / scale, 4, 2.1f, 0.55f); // richer detail in [-1,1]

    if (m_brushNoiseMode == NoiseBrushIntensity) {
        float factor = 1.0f + m_brushNoiseStrength * n * 1.6f;
        factor = std::clamp(factor, 0.2f, 3.5f);
        res.size = baseSize * factor;
        return res;
    }

    if (m_brushNoiseMode == NoiseBrushOffset) {
        QVector2D dirNorm = dir;
        if (dirNorm.lengthSquared() < 0.0001f) dirNorm = QVector2D(1.0f, 0.0f);
        dirNorm.normalize();
        QVector2D perp(-dirNorm.y(), dirNorm.x());
        float pixelBase = std::max(4.0f, baseSize * 0.35f); // ensure visible even on small brushes
        float offsetMag = (pixelBase + baseSize * 0.35f) * m_brushNoiseStrength; // scale with brush size + strength
        res.offset = perp * (n * offsetMag);
        return res;
    }

    return res;
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

void SqueegeeWindow::clearCanvas()
{
    makeCurrent();
    int w = width();
    int h = height();
    int d = 32;
    std::vector<float> clearData(w * h * d * 4, 0.0f);
    
    glBindTexture(GL_TEXTURE_3D, m_texture3DA);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, w, h, d, GL_RGBA, GL_FLOAT, clearData.data());
    
    glBindTexture(GL_TEXTURE_3D, m_texture3DB);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, w, h, d, GL_RGBA, GL_FLOAT, clearData.data());
    
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
void SqueegeeWindow::generateDrops(std::vector<float>& buffer, int w, int h, int d)
{
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
            
            int minSize = std::max(1, int(m_dropMaxSize * m_minSizeRatio));
            int maxSize = std::max(minSize + 1, m_dropMaxSize + 1);
            
            int r = QRandomGenerator::global()->bounded(minSize, maxSize);
            points.append({cx, cy, cz, r});
        }
    } else { // ModeGrid
        for (int y = m_gridStep / 2; y < h; y += m_gridStep) {
            for (int x = m_gridStep / 2; x < w; x += m_gridStep) {
                int cz = QRandomGenerator::global()->bounded(d);
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
                float depthScale = float((d - 1) - cz) / float(d - 1);
                currentR = std::max(1, int(std::round(currentR * depthScale)));
            }
            
            int colorIdx = QRandomGenerator::global()->bounded(colorCount);
            QVector3D col = currentPalette[colorIdx];
            
            drawShapeIntoBuffer(buffer, w, h, d, cx, cy, cz, currentR, col);
        }
    }
}

void SqueegeeWindow::drawShapeIntoBuffer(std::vector<float>& buffer, int w, int h, int d, int cx, int cy, int cz, int r, QVector3D col)
{
    if (m_genShape == ShapeCircle) {
        // Circle Logic
        if (m_drawBorders) {
            int borderR = r + 1;
            for (int y = cy - borderR; y <= cy + borderR; ++y) {
                for (int x = cx - borderR; x <= cx + borderR; ++x) {
                    if (x >= 0 && x < w && y >= 0 && y < h) {
                        float dist = std::sqrt(std::pow(x - cx, 2) + std::pow(y - cy, 2));
                        if (dist <= borderR) {
                            int idx = (cz * w * h + y * w + x) * 4;
                            buffer[idx + 0] = 0.0f;
                            buffer[idx + 1] = 0.0f;
                            buffer[idx + 2] = 0.0f;
                            buffer[idx + 3] = 1.0f;
                        }
                    }
                }
            }
        }

        for (int y = cy - r; y <= cy + r; ++y) {
            for (int x = cx - r; x <= cx + r; ++x) {
                if (x >= 0 && x < w && y >= 0 && y < h) {
                    float dist = std::sqrt(std::pow(x - cx, 2) + std::pow(y - cy, 2));
                    if (dist <= r) {
                        int idx = (cz * w * h + y * w + x) * 4;
                        buffer[idx + 0] = col.x();
                        buffer[idx + 1] = col.y();
                        buffer[idx + 2] = col.z();
                        buffer[idx + 3] = 0.8f;
                    }
                }
            }
        }
    } else { // ShapeSquare
        // Square Logic
        if (m_drawBorders) {
            int borderR = r + 1;
            for (int y = cy - borderR; y <= cy + borderR; ++y) {
                for (int x = cx - borderR; x <= cx + borderR; ++x) {
                    if (x >= 0 && x < w && y >= 0 && y < h) {
                        int idx = (cz * w * h + y * w + x) * 4;
                        buffer[idx + 0] = 0.0f;
                        buffer[idx + 1] = 0.0f;
                        buffer[idx + 2] = 0.0f;
                        buffer[idx + 3] = 1.0f;
                    }
                }
            }
        }
        
        for (int y = cy - r; y <= cy + r; ++y) {
            for (int x = cx - r; x <= cx + r; ++x) {
                if (x >= 0 && x < w && y >= 0 && y < h) {
                    int idx = (cz * w * h + y * w + x) * 4;
                    buffer[idx + 0] = col.x();
                    buffer[idx + 1] = col.y();
                    buffer[idx + 2] = col.z();
                    buffer[idx + 3] = 0.8f;
                }
            }
        }
    }
}

void SqueegeeWindow::spawnDrops(const QVector<DropInfo>& drops)
{
    makeCurrent();
    int w = width();
    int h = height();
    int d = 32;
    
    // Readback
    std::vector<float> data(w * h * d * 4);
    glBindTexture(GL_TEXTURE_3D, m_texture3DA);
    glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, data.data());
    
    for (const auto& drop : drops) {
        int cz = QRandomGenerator::global()->bounded(d); // Random depth
        QVector3D col(drop.color.redF(), drop.color.greenF(), drop.color.blueF());
        // For agents, we might not want concentric rings or border per se, but let's respect current settings
        // for consistency? Or force simple circle?
        // Let's call helper. It uses m_settings.
        drawShapeIntoBuffer(data, w, h, d, (int)drop.pos.x(), (int)drop.pos.y(), cz, (int)drop.size, col);
    }
    
    // Upload
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, w, h, d, GL_RGBA, GL_FLOAT, data.data());
    update();
}

void SqueegeeWindow::paintPaths(const QVector<PathInfo>& paths)
{
    if (paths.isEmpty()) return;
    makeCurrent();
    
    // We reuse the compute shader logic from simulateStroke but manually stepping along the path
    m_computeSqueegee->bind();
    m_computeSqueegee->setUniformValue("isMouseDown", true);
    m_computeSqueegee->setUniformValue("toroidal", m_toroidal);
    m_computeSqueegee->setUniformValue("squeegeeMode", (int)m_squeegeeMode);
    
    // For each path
    for (const auto& path : paths) {
        if (path.points.size() < 2) continue;
        
        QVector2D lastPos = path.points[0];
        QVector2D noisyLast = lastPos; // Reset noise accumulation for each path? 
                                       // Or keep continuity? Reset makes sense for distinct strokes.
        m_noiseOffsetAccum = QVector2D(0,0);
        
        // Iterate segments
        for (int i = 0; i < path.points.size() - 1; ++i) {
            QVector2D start = path.points[i];
            QVector2D end = path.points[i+1];
            
            QVector2D dir = end - start;
            float len = dir.length();
            if (len < 0.001f) continue;
            
            QVector2D dirNorm = dir.normalized();
            
            // Step size: roughly 1 pixel or brushSize/4? 
            // Too small steps = slow. Too large = gaps.
            // Let's use 1.0f for smooth lines.
            int steps = std::max(1, (int)std::ceil(len)); // 1 step per pixel approx
            
            for (int s = 0; s < steps; ++s) {
                float t = (float)s / (float)steps;
                QVector2D current = start + dir * t;
                
                // --- Uniform Update & Dispatch (Inner loop of simulateStroke) ---
                BrushNoiseResult n = sampleBrushNoise(path.size, current, dirNorm);
                QVector2D prevOffset = m_noiseOffsetAccum;
                QVector2D offset = prevOffset * 0.5f + n.offset * 0.5f;
                m_noiseOffsetAccum = offset;
                
                QVector2D noisyCurrent = current + offset;
                // noisyLast is carried over
                
                float brushSizeForShader = (m_brushNoiseMode == NoiseBrushIntensity) ? n.size : path.size;
                
                glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
                glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
                
                m_computeSqueegee->setUniformValue("mousePos", noisyCurrent);
                m_computeSqueegee->setUniformValue("lastMousePos", noisyLast);
                m_computeSqueegee->setUniformValue("strokeDir", dirNorm);
                m_computeSqueegee->setUniformValue("brushSize", brushSizeForShader);
                
                // Color Injection? 
                // The squeegee shader currently just pushes existing paint. 
                // It DOES NOT inject new color unless we modify the shader or pre-seed the texture.
                // WAIT. The user wants to "paint with brush". 
                // The current squeegee tool pushes paint. It doesn't ADD paint usually, unless logic changed?
                // Let's check squeegee.comp? 
                // Actually, standard squeegee just smears.
                // BUT, if we want to visualize agents, we usually want to ADD color.
                // If the user said "conduct with brush along trajectory", maybe they imply smearing?
                // "провести кистью по траеторирям" -> "swipe brush along trajectories"
                // If we want COLOR, we might need to inject color.
                // For now, let's assume the user wants the SQUEEGEE effect (smearing existing paint).
                // Or...
                // If I look at `simulateStroke` in `generateComposition`, it moves paint.
                // If I want to DRAW trails, I might need to deposit paint first?
                // User said "conduct with brush", implying the tool they use manually.
                // If I manually use the brush, I am smearing.
                // So this logic is correct for "simulating the brush tool".
                
                glDispatchCompute((width() + 7) / 8, (height() + 7) / 8, 32);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                
                std::swap(m_texture3DA, m_texture3DB);
                
                // Gravity
                m_computeGravity->bind();
                glBindImageTexture(0, m_texture3DA, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
                glBindImageTexture(1, m_texture3DB, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);
                glDispatchCompute((width() + 7) / 8, (height() + 7) / 8, 1);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                std::swap(m_texture3DA, m_texture3DB);
                m_computeGravity->release();
                
                m_computeSqueegee->bind(); // Rebind
                // Restore uniforms lost by unbind? QOpenGLShaderProgram keeps module state usually?
                // Ideally yes, but let's be safe or just minimal rebind.
                // Actually we set uniforms every loop anyway.
                m_computeSqueegee->setUniformValue("isMouseDown", true);
                m_computeSqueegee->setUniformValue("toroidal", m_toroidal);
                m_computeSqueegee->setUniformValue("squeegeeMode", (int)m_squeegeeMode);
                
                noisyLast = noisyCurrent;
            } // end steps
            
            lastPos = end; // Update reference for next segment continuity if needed, 
                           // though we used stepping variable 'noisyLast' for continuity
        }
    }
    
    m_computeSqueegee->release();
    update();
}

void SqueegeeWindow::regenerateShiftedOverlay()
{
    qDebug() << "Regenerating Shifted Overlay with Simulation...";
    m_hasGenerated = true; // Mark as generated so we know there's content
    
    int w = width();
    int h = height();
    int d = 32;
    
    // 1. Read existing Main Texture (current state of canvas)
    std::vector<float> bufferExisting(w * h * d * 4);
    glBindTexture(GL_TEXTURE_3D, m_texture3DA);
    glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, bufferExisting.data());
    
    // 2. Prepare Temporary Textures for the new "Layer" generation
    // We need two textures to run the compute simulation (ping-pong)
    GLuint tempTexA, tempTexB;
    glGenTextures(1, &tempTexA);
    glBindTexture(GL_TEXTURE_3D, tempTexA);
    glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA32F, w, h, d);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    glGenTextures(1, &tempTexB);
    glBindTexture(GL_TEXTURE_3D, tempTexB);
    glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA32F, w, h, d);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    
    // 3. Initialize Temp Texture A with Drops (Fresh State)
    std::vector<float> bufferNew(w * h * d * 4, 0.0f);
    generateDrops(bufferNew, w, h, d); // Fill bufferNew with drops
    
    // Upload drops to TempA
    glBindTexture(GL_TEXTURE_3D, tempTexA);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, w, h, d, GL_RGBA, GL_FLOAT, bufferNew.data());

    // Clear TempB just in case
    // (Optional if simulateStroke overwrites completely, but good practice)
    std::vector<float> clearBuff(w * h * d * 4, 0.0f);
    glBindTexture(GL_TEXTURE_3D, tempTexB);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, w, h, d, GL_RGBA, GL_FLOAT, clearBuff.data());
    
    // 4. Run Squeegee Simulation on Temporary Textures
    // We temporarily swap the member variables so simulateStroke operates on our temps
    GLuint backupTexA = m_texture3DA;
    GLuint backupTexB = m_texture3DB;
    
    m_texture3DA = tempTexA;
    m_texture3DB = tempTexB;
    
    // -- Simulation Logic Copied from generateComposition --
    if (!m_showPreview) {
        float rad = qDegreesToRadians(m_genAngle);
        QVector2D dir(std::cos(rad), std::sin(rad));
        QVector2D center(w * 0.5f, h * 0.5f);
        float diag = std::sqrt(float(w*w + h*h));
        QVector2D start = center - dir * diag;
        QVector2D end = center + dir * diag;
        float squeegeeWidth = width() * m_genWidth;
        
        for (int i = 0; i < m_genPasses; ++i) {
            simulateStroke(start, end, squeegeeWidth);
        }
    }
    // Result is now in m_texture3DA (whichever was last target, usually simulateStroke swaps)
    // Actually simulateStroke does swaps. We need to be careful.
    // simulateStroke ends with a swap if it runs.
    // If it ran an odd number of swaps inside (it shouldn't, loop logic usually consistent),
    // m_texture3DA will point to the result.
    
    // 5. Read back the simulation result
    // The result is in the texture currently pointed to by m_texture3DA
    std::vector<float> bufferResult(w * h * d * 4);
    glBindTexture(GL_TEXTURE_3D, m_texture3DA);
    glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, bufferResult.data());
    
    // 6. Restore Member Variables
    m_texture3DA = backupTexA;
    m_texture3DB = backupTexB;
    
    // 7. Clean up Temporary Textures
    glDeleteTextures(1, &tempTexA);
    glDeleteTextures(1, &tempTexB);
    
    // 8. Mix Result into Existing Mix with Random Shift
    int zShift = QRandomGenerator::global()->bounded(d);
    
    for (int z = 0; z < d; ++z) {
        int targetZ = (z + zShift) % d;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int idxSrc = ((z * h + y) * w + x) * 4;
                int idxDst = ((targetZ * h + y) * w + x) * 4;
                
                float srcAlpha = bufferResult[idxSrc + 3];
                if (srcAlpha > 0.0f) {
                    // Overlay logic: simple overwrite if valid pixel
                    bufferExisting[idxDst + 0] = bufferResult[idxSrc + 0];
                    bufferExisting[idxDst + 1] = bufferResult[idxSrc + 1];
                    bufferExisting[idxDst + 2] = bufferResult[idxSrc + 2];
                    bufferExisting[idxDst + 3] = bufferResult[idxSrc + 3];
                }
            }
        }
    }
    
    // 9. Upload Final Composite to Real Texture
    glBindTexture(GL_TEXTURE_3D, m_texture3DA);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, w, h, d, GL_RGBA, GL_FLOAT, bufferExisting.data());
    
    update();
}
