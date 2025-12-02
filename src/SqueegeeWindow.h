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
    
    // 3D Textures (Double Buffered)
    // We use raw GL texture IDs for easier binding to image units in compute shaders
    GLuint m_texture3DA = 0;
    GLuint m_texture3DB = 0;
    
    QOpenGLBuffer m_vbo;
    QOpenGLVertexArrayObject m_vao;

    enum ToolMode {
        Squeegee,
        Dropper
    };

    ToolMode m_currentMode = Squeegee;
    float m_brushSize = 50.0f;
    QVector3D m_brushColor = QVector3D(1.0f, 0.0f, 0.0f);
    
    QVector2D m_lastMousePos;
    QVector2D m_currentMousePos;
    bool m_isMouseDown = false;
    int m_frameCount = 0;
};
