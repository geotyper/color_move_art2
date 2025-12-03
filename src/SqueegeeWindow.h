#pragma once

#include <QOpenGLWindow>
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

#include <QOpenGLFunctions_4_3_Core>

class SqueegeeWindow : public QOpenGLWindow, protected QOpenGLFunctions_4_3_Core
{
public:
    SqueegeeWindow();
    ~SqueegeeWindow();

    enum GenShape {
        ShapeCircle,
        ShapeSquare
    };

    enum GenMode {
        ModeRandom,
        ModeGrid
    };

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
    QVector<QVector<QVector3D>> m_palettes;
    
    QVector2D m_lastMousePos;
    QVector2D m_currentMousePos;
    bool m_isMouseDown = false;
    int m_frameCount = 0;

public:
    void setGenAngle(float angle) { m_genAngle = angle; }
    void setGenWidth(float width) { m_genWidth = width; }
    void setGenPasses(int passes) { m_genPasses = passes; }
    void setGenSteps(int steps) { m_genSteps = steps; }

    void setGenShape(GenShape shape) { m_genShape = shape; regenerate(); }
    void setGenMode(GenMode mode) { m_genMode = mode; regenerate(); }
    void setGridStep(int step) { m_gridStep = step; regenerate(); }
    
    void setShowPreview(bool show) { m_showPreview = show; regenerate(); }
    void setToroidal(bool toroidal) { m_toroidal = toroidal; regenerate(); }
    void setKeepExisting(bool keep) { m_keepExisting = keep; }
    void setDrawBorders(bool draw) { m_drawBorders = draw; regenerate(); }
    void setDropMaxSize(int size) { m_dropMaxSize = size; regenerate(); }
    void setGenConcentric(int count) { m_genConcentric = count; regenerate(); }
    void setGenDensity(int density) { m_genDensity = density; regenerate(); }
    void setPalette(int index) { m_currentPaletteIdx = index; regenerate(); }
    
    void regenerate() { generateComposition(); update(); }
    void applySaturation();
};
