#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QTimer>
#include <QVector3D>
#include <QKeyEvent>

#include <vector>

#include <OpenMesh/Core/Mesh/TriMesh_ArrayKernelT.hh>

class MeshViewerWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core
{
    Q_OBJECT

public:
    explicit MeshViewerWidget(QWidget *parent = nullptr);
    ~MeshViewerWidget() override;

    void setLightDirection(const QVector3D &dir);
    void setCameraDistance(float dist);
    
    bool checkRayIntersection(float x, float y, float viewWidth, float viewHeight);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    using Mesh = OpenMesh::TriMesh_ArrayKernelT<>;

    struct Vertex {
        QVector3D position;
        QVector3D normal;
    };

    void buildSphere();
    void uploadMesh();

    QOpenGLShaderProgram m_program;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};

    std::vector<Vertex> m_vertices;
    int m_vertexCount = 0;
    float m_rotationY = 0.0f;
    QTimer *m_timer = nullptr;
    bool m_showWireframe = false;
    QVector3D m_lightDir = QVector3D(0.3f, 0.7f, 0.4f).normalized();
    float m_cameraDistance = 3.0f;
};
