#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QTimer>
#include <vector>
#include <deque>
#include <QVector3D>
#include <QKeyEvent>
#include "HelpStructures.h"

#include "CgalMeshTypes.h"

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/index/rtree.hpp>

// GLM Includes
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

// Define a point in 3D
typedef bg::model::point<float, 3, bg::cs::cartesian> BoostPoint;
// Define a box (AABB)
typedef bg::model::box<BoostPoint> BoostBox;
// Value stored in R-tree: pair(Box, FaceHandle index)
// We store int index because FaceHandle is not trivially movable/copyable across all versions or simpler to just debug with int
typedef std::pair<BoostBox, int> BoostValue;

using SurfaceMesh = CgalMeshTypes::SurfaceMesh;
using FaceIndex  = CgalMeshTypes::F;
using VertexIndex = CgalMeshTypes::V;

struct SurfaceAgent {
    FaceIndex face = SurfaceMesh::null_face();
    glm::vec3 bary; // u, v, w
    glm::vec3 worldVelocity; // Tangent vector in World Space
    float speed; // Scalar speed
    int layer = 0; // Assigned texture layer for painting
    QColor color;
    std::deque<glm::vec3> trail; // World positions
    int age = 0;
    int maxAge = 1000;
    bool wasVisible = false;
    int invisibleTicks = 0;
};

class MeshViewerWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core
{
    Q_OBJECT

public:
    explicit MeshViewerWidget(QWidget *parent = nullptr);
    ~MeshViewerWidget() override;

    void setLightDirection(const QVector3D &dir);
    void setCameraDistance(float dist);
    void setAgentsPaused(bool paused);
    void setPaletteIndex(int idx) { m_paletteIndex = idx; }
    int getPaletteIndex() const { return m_paletteIndex; }
    void setPalettes(const QVector<QVector<QVector3D>>* palettes) { m_externalPalettes = palettes; }
    void setAgentBaseSpeed(float s) { m_agentBaseSpeed = s; }
    
    // Check intersection and return hit info
    bool checkRayIntersection(float x, float y, float viewWidth, float viewHeight, glm::vec3 &hitPos);
    void updateAgents();
    void setAgentLifetime(int ticks) { m_agentLifetime = ticks; }
    
    // Returns list of agents projected to 2D screen coordinates
    struct AgentRenderInfo {
        QVector2D screenPos;
        QColor color;
        bool isVisible;
        int layer = 0;
        float viewDepth = 0.0f; // View-space z (for depth sorting in 2D projections)
        std::vector<std::vector<QVector2D>> trailSegments;
        float headBrightness = 1.0f;
        std::vector<std::vector<float>> trailBrightness;
    };
    std::vector<AgentRenderInfo> getProjectedAgents(float viewWidth, float viewHeight);
    
    int getAgentCount() const { return static_cast<int>(m_surfaceAgents.size()); }
    void clearAgents() { m_surfaceAgents.clear(); }
    
    void updateMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
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
        glm::vec3 position;
        glm::vec3 normal;
    };
    std::vector<VertexData> m_vertices; // Type changed
    int m_vertexCount = 0;
    float m_rotationY = 0.0f;
    float m_rotationX = -20.0f; // Initial X rotation
    QPoint m_lastMousePos;
    bool m_isMouseDown = false;
    QTimer *m_timer = nullptr; // Modified initialization (removed nullptr from instruction, but keeping it for consistency with original)
    bool m_showWireframe = false;
    glm::vec3 m_lightDir = glm::normalize(glm::vec3(0.3f, 0.7f, 0.4f));
    float m_cameraDistance = 5.0f; // Modified value
    
    SurfaceMesh m_mesh; // New member
    std::vector<glm::vec3> m_faceNormals;
    std::vector<glm::vec3> m_vertexNormals;
    std::vector<SurfaceAgent> m_surfaceAgents; // New member
    int m_agentLifetime = 1000;
    bool m_agentsPaused = false;
    int m_paletteIndex = 0;
    const QVector<QVector<QVector3D>>* m_externalPalettes = nullptr;
    QVector<QVector<QVector3D>> m_palettes {
        { QVector3D(0.8f, 0.2f, 0.2f), QVector3D(0.2f, 0.6f, 0.4f), QVector3D(0.2f, 0.4f, 0.8f),
          QVector3D(0.9f, 0.7f, 0.1f), QVector3D(0.6f, 0.3f, 0.5f), QVector3D(0.2f, 0.2f, 0.3f) }
    };
    float m_agentBaseSpeed = 0.05f;
    
    // Spatial Index
    bgi::rtree<BoostValue, bgi::quadratic<16>> m_rtree;

    // Helper to get world pos from agent
    glm::vec3 getAgentWorldPos(const SurfaceAgent &agent); // New member
};
