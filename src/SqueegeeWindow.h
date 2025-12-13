#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QVector2D>
#include <QVector3D>
#include <QRandomGenerator>
#include <QColor>

#include <algorithm>
#include <QOpenGLFunctions_4_3_Core>
#include "Noise2D.h"

class SqueegeeWindow : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core
{
public:
    SqueegeeWindow(QWidget *parent = nullptr);
    ~SqueegeeWindow();

    enum GenShape {
        ShapeCircle,
        ShapeSquare
    };

    enum GenMode {
        ModeRandom,
        ModeGrid
    };

    enum SqueegeeMode {
        SqueegeeSolid,
        SqueegeeSoft,
        SqueegeeAccurate
    };

    enum BrushNoiseMode {
        NoiseOff,
        NoiseBrushIntensity,
        NoiseBrushOffset
    };

    void setBackgroundColor(const QColor &color);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void initShaders();
    bool m_gravityEnabled = false; // Default off to stabilize projection
    void initGeometry();
    void initSimulation();
    void generateComposition();
    void drawDrop(QVector2D pos, float size, QVector3D color);
    void simulateStroke(QVector2D start, QVector2D end, float size);
    void applyBlur();

    QOpenGLShaderProgram *m_program = nullptr; // Render
    QOpenGLShaderProgram *m_computeGravity = nullptr;
    QOpenGLShaderProgram *m_computeSqueegee = nullptr;
    QOpenGLShaderProgram *m_computeBlur = nullptr;
    QOpenGLShaderProgram *m_computeSaturate = nullptr;
    QOpenGLShaderProgram *m_computeCombFix = nullptr;
    
    // 3D Textures (Double Buffered)
    // We use raw GL texture IDs for easier binding to image units in compute shaders
    GLuint m_texture3DA = 0;
    GLuint m_texture3DB = 0;
    
    QOpenGLBuffer m_vbo;
    QOpenGLVertexArrayObject m_vao;

    enum ToolMode {
        Squeegee,
        RandomComb
    };





    ToolMode m_currentMode = Squeegee;
    float m_brushSize = 50.0f;
    QVector3D m_brushColor = QVector3D(1.0f, 0.0f, 0.0f);
    
    // Generation Parameters
    float m_genAngle = 45.0f; // Degrees
    float m_genWidth = 2.0f; // Multiplier of screen width
    int m_genPasses = 1;
    int m_genSteps = 600; // Speed (Higher = Slower/More Physics)
    
    GenShape m_genShape = ShapeCircle;
    GenMode m_genMode = ModeRandom;
    int m_gridStep = 50;
    
    bool m_showPreview = false;
    bool m_toroidal = false; // Toroidal wrapping
    bool m_keepExisting = false; // Overlay mode
    bool m_drawBorders = false; // Draw black borders
    int m_dropMaxSize = 25;
    int m_genConcentric = 1; // Max nested circles (1-7)
    int m_genDensity = 600; // Number of drops
    int m_currentPaletteIdx = 0;
    SqueegeeMode m_squeegeeMode = SqueegeeSolid;
    bool m_depthRadiusScaling = false;
    float m_sharpenAmount = 0.0f;
    QVector<QVector<QVector3D>> m_palettes;
    Noise2D m_noise;
    BrushNoiseMode m_brushNoiseMode = NoiseOff;
    float m_brushNoiseScale = 120.0f;
    float m_brushNoiseStrength = 0.0f; // 0..1
    float m_minSizeRatio = 0.1f; // 0.1 .. 0.9
    QVector2D m_noiseOffsetAccum = QVector2D(0.0f, 0.0f);

    QVector2D m_lastMousePos;
    QVector2D m_currentMousePos;
    bool m_isMouseDown = false;
    int m_frameCount = 0;
    bool m_hasGenerated = false;

public:
    void setGenAngle(float angle) { m_genAngle = angle; }
    void setGenWidth(float width) { m_genWidth = width; }
    void setGenPasses(int passes) { m_genPasses = passes; }
    void setGenSteps(int steps) { m_genSteps = steps; }

    void setGenShape(GenShape shape) { m_genShape = shape; }
    void setGenMode(GenMode mode) { m_genMode = mode; }
    void setGridStep(int step) { m_gridStep = step; }
    
    void setShowPreview(bool show) { m_showPreview = show; }
    void setToroidal(bool toroidal) { m_toroidal = toroidal; }
    void setKeepExisting(bool keep) { m_keepExisting = keep; }
    void setDrawBorders(bool draw) { m_drawBorders = draw; }
    void setDropMaxSize(int size) { m_dropMaxSize = size; }
    void setGenConcentric(int count) { m_genConcentric = count; }
    void setGenDensity(int density) { m_genDensity = density; }
    void setPalette(int index) { m_currentPaletteIdx = index; }
    void setSqueegeeMode(SqueegeeMode mode) { m_squeegeeMode = mode; }
    void setDepthRadiusScaling(bool enabled) { m_depthRadiusScaling = enabled; }
    void setSharpenAmount(float amount) { m_sharpenAmount = amount; update(); }
    void setBrushNoiseMode(BrushNoiseMode mode) { m_brushNoiseMode = mode; }
    void setBrushNoiseScale(float scale) { m_brushNoiseScale = std::max(1.0f, scale); }
    void setBrushNoiseStrength(float strength) { m_brushNoiseStrength = std::clamp(strength, 0.0f, 1.0f); }
    void setMinSizeRatio(float ratio) { m_minSizeRatio = std::clamp(ratio, 0.1f, 0.9f); }
    const QVector<QVector3D>& getPalettes() const { return m_palettes[m_currentPaletteIdx]; }
    const QVector<QVector<QVector3D>>& allPalettes() const { return m_palettes; }
    
    void regenerate() { generateComposition(); update(); }
    void regenerateSqueegeeOnly();
    void regenerateShiftedOverlay();
    void applySaturation();
    void applyCombFix();
    void clearCanvas();



    struct DropInfo {
        QVector2D pos;
        QColor color;
        float size;
        int layer = -1; // -1 -> random layer, otherwise fixed
    };
    void spawnDrops(const QVector<DropInfo>& drops);

    struct PathInfo {
        QVector<QVector2D> points;
        QColor color;
        float size;
        float brightness = 1.0f;
        int layer = -1;
        bool useQtPainter = false;
    };
    void paintPaths(const QVector<PathInfo>& paths);

private:
    void generateDrops(std::vector<float>& buffer, int w, int h, int d);
    void drawShapeIntoBuffer(std::vector<float>& buffer, int w, int h, int d, int cx, int cy, int cz, int r, QVector3D col);

    QColor m_backgroundColor = Qt::white;

    struct BrushNoiseResult {
        float size = 0.0f;
        QVector2D offset = QVector2D(0.0f, 0.0f);
    };
    BrushNoiseResult sampleBrushNoise(float baseSize, const QVector2D& pos, const QVector2D& dir) const;
};
