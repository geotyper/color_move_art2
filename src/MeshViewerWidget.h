#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QTimer>
#include <QVector3D>
#include <QKeyEvent>
#include <QColor> // Added for SurfaceAgent color

#include <vector>
#include <deque>

#include <OpenMesh/Core/Mesh/TriMesh_ArrayKernelT.hh>

#include <OpenMesh/Core/IO/MeshIO.hh> // New include
#include <OpenMesh/Core/Mesh/TriMesh_ArrayKernelT.hh> // Already present, but instruction had it again. Keeping it as is.

typedef OpenMesh::TriMesh_ArrayKernelT<> MyMesh;

struct SurfaceAgent {
    OpenMesh::FaceHandle face;
    QVector3D bary; // u, v, w
    QVector3D worldVelocity; // Tangent vector in World Space
    float speed; // Scalar speed
    QColor color;
    std::deque<QVector3D> trail; // World positions
};

class MeshViewerWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core
{
    Q_OBJECT

public:
    explicit MeshViewerWidget(QWidget *parent = nullptr);
    ~MeshViewerWidget() override;

    void setLightDirection(const QVector3D &dir);
    void setCameraDistance(float dist);
    
    // Check intersection and return hit info
    bool checkRayIntersection(float x, float y, float viewWidth, float viewHeight, QVector3D &hitPos);
    void updateAgents();
    
    // Returns list of agents projected to 2D screen coordinates
    struct AgentRenderInfo {
        QVector2D screenPos;
        QColor color;
        std::vector<QVector2D> screenTrail;
    };
    std::vector<AgentRenderInfo> getProjectedAgents(float viewWidth, float viewHeight);
    
    int getAgentCount() const { return static_cast<int>(m_surfaceAgents.size()); }
    void clearAgents() { m_surfaceAgents.clear(); }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    // keyPressEvent removed as per instruction

private:
    // using Mesh = OpenMesh::TriMesh_ArrayKernelT<>; // Removed as per instruction

    // struct Vertex { // Renamed to VertexData and moved
    //     QVector3D position;
    //     QVector3D normal;
    // };

    void buildSphere();
    void uploadMesh();
    
    // ... shaders ...
    QOpenGLShaderProgram m_program;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo; // Modified initialization

    struct VertexData { // New struct definition
        QVector3D position;
        QVector3D normal;
    };
    std::vector<VertexData> m_vertices; // Type changed
    int m_vertexCount = 0;
    float m_rotationY = 0.0f;
    QTimer *m_timer = nullptr; // Modified initialization (removed nullptr from instruction, but keeping it for consistency with original)
    bool m_showWireframe = false;
    QVector3D m_lightDir = QVector3D(0.3f, 0.7f, 0.4f).normalized();
    float m_cameraDistance = 5.0f; // Modified value
    
    MyMesh m_mesh; // New member
    std::vector<SurfaceAgent> m_surfaceAgents; // New member
    
    // Helper to get world pos from agent
    QVector3D getAgentWorldPos(const SurfaceAgent &agent); // New member
};
