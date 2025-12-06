#include "MeshViewerWidget.h"

#include <QMatrix4x4>
#include <QtMath>
#include <cmath>
#include <QRandomGenerator>
#include <QDebug>
#include <limits>

MeshViewerWidget::MeshViewerWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumSize(480, 360);
    setFocusPolicy(Qt::StrongFocus);

    m_timer = new QTimer(this);
    // Connect timer to update for animation
    connect(m_timer, &QTimer::timeout, [this]() {
        if (!m_surfaceAgents.empty()) {
            update(); // Trigger paintGL which renders agents
        }
    });
    // m_timer->start(16); // Only start when needed? Or always for rotation?
    // Let's keep rotation off for now as per "removed rotationY" instruction, 
    // BUT I just restored rotationY member. 
    // The user wants agents to move.
    
    m_cameraDistance = 5.0f;
}

MeshViewerWidget::~MeshViewerWidget()
{
    makeCurrent();
    m_vao.destroy();
    m_vbo.destroy();
    m_program.release();
    doneCurrent();
}

void MeshViewerWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.2f, 0.2f, 0.25f, 1.0f);

    const char *vs = R"(#version 430 core
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

uniform mat4 u_mvp;
uniform mat4 u_model; 

out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec4 worldPos = u_model * vec4(inPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(u_model) * inNormal;
    gl_Position = u_mvp * vec4(inPos, 1.0);
})";

    const char *fs = R"(#version 430 core
in vec3 vNormal;
in vec3 vWorldPos;

out vec4 fragColor;

uniform bool u_wireframe;
uniform vec3 u_lightDir;
uniform vec3 u_cameraPos;

void main() {
    vec3 n = normalize(vNormal);
    vec3 l = normalize(u_lightDir);
    vec3 v = normalize(u_cameraPos - vWorldPos);
    vec3 h = normalize(l + v);

    float diff = max(dot(n, l), 0.0);
    float spec = pow(max(dot(n, h), 0.0), 32.0);
    
    vec3 ambientColor = vec3(0.1, 0.1, 0.15);
    vec3 diffuseColor = vec3(0.2, 0.5, 0.8);
    vec3 specColor = vec3(1.0, 1.0, 1.0);
    
    vec3 result = ambientColor + diff * diffuseColor + spec * specColor;

    if (u_wireframe) {
        fragColor = vec4(vec3(0.08), 1.0);
    } else {
        fragColor = vec4(result, 1.0);
    }
})";

    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vs)) {
        qWarning() << "Vertex shader compile error:" << m_program.log();
    }
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fs)) {
        qWarning() << "Fragment shader compile error:" << m_program.log();
    }
    if (!m_program.link()) {
        qWarning() << "Shader link error:" << m_program.log();
    }

    buildSphere();
    uploadMesh();

    qDebug() << "MeshViewerWidget::initializeGL - Success.";
}

void MeshViewerWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void MeshViewerWidget::paintGL()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    
    glClearColor(0.2f, 0.2f, 0.25f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_vertexCount == 0) return;

    QMatrix4x4 model;
    model.rotate(-20.0f, 1.0f, 0.0f, 0.0f);
    model.rotate(m_rotationY, 0.0f, 1.0f, 0.0f);

    QMatrix4x4 view;
    view.translate(0.0f, 0.0f, -m_cameraDistance);

    QMatrix4x4 proj;
    const float aspect = width() > 0 ? float(width()) / float(height()) : 1.0f;
    proj.perspective(45.0f, aspect, 0.1f, 100.0f);

    QMatrix4x4 mvp = proj * view * model;

    m_program.bind();
    m_program.setUniformValue("u_mvp", mvp);
    m_program.setUniformValue("u_model", model);
    m_program.setUniformValue("u_wireframe", false);
    m_program.setUniformValue("u_lightDir", m_lightDir);
    m_program.setUniformValue("u_cameraPos", QVector3D(0.0f, 0.0f, m_cameraDistance));

    m_vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);

    if (m_showWireframe) {
        m_program.setUniformValue("u_wireframe", true);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    m_vao.release();
    m_program.release();
}

void MeshViewerWidget::buildSphere()
{
    m_mesh.clear();
    m_vertices.clear();
    
    constexpr float pi = 3.14159265359f;
    const int slices = 32;
    const int stacks = 32;
    const float radius = 1.0f;
    
    // 1. Create Vertices
    // North Pole
    MyMesh::VertexHandle v_north = m_mesh.add_vertex(MyMesh::Point(0, 0, radius));
    
    // Rings
    std::vector<std::vector<MyMesh::VertexHandle>> rings;
    for (int i = 1; i < stacks; ++i) { // Stacks 1 to N-1
        float lat = pi * (-0.5f + (float)i / stacks);
        float z = std::sin(lat);
        float zr = std::cos(lat);
        
        std::vector<MyMesh::VertexHandle> ring;
        for (int j = 0; j < slices; ++j) {
            float lng = 2 * pi * (float)j / slices;
            float x = std::cos(lng) * zr;
            float y = std::sin(lng) * zr;
            ring.push_back(m_mesh.add_vertex(MyMesh::Point(x * radius, y * radius, z * radius)));
        }
        rings.push_back(ring);
    }
    
    // South Pole
    MyMesh::VertexHandle v_south = m_mesh.add_vertex(MyMesh::Point(0, 0, -radius));
    
    // 2. Create Faces
    // North Cap (Triangle Fan) connects to Last Ring (highest Z, i=stacks-1 in logic, here last in vector)
    // Wait, my loop i=1 is lowest Z (lat -PI/2 + delta).
    // So i=1 is near South Pole. i=stacks-1 is near North Pole.
    
    // South Cap (connect v_south to Ring 0)
    auto &southRing = rings[0];
    for (int j = 0; j < slices; ++j) {
        int next_j = (j + 1) % slices;
        // Tri: v_south, ring[next_j], ring[j] (CCW)
        m_mesh.add_face(v_south, southRing[next_j], southRing[j]);
    }
    
    // Middle Quads (as 2 tris)
    for (size_t i = 0; i < rings.size() - 1; ++i) {
        auto &lower = rings[i];
        auto &upper = rings[i+1];
        for (int j = 0; j < slices; ++j) {
            int next_j = (j + 1) % slices;
            // lower[j], lower[next_j], upper[next_j], upper[j]
            // Tri 1: lower[j], lower[next_j], upper[j]
            m_mesh.add_face(lower[j], lower[next_j], upper[j]);
            // Tri 2: lower[next_j], upper[next_j], upper[j]
            m_mesh.add_face(lower[next_j], upper[next_j], upper[j]);
        }
    }
    
    // North Cap (connect v_north to Last Ring)
    auto &northRing = rings.back();
    for (int j = 0; j < slices; ++j) {
        int next_j = (j + 1) % slices;
        // Tri: v_north, ring[j], ring[next_j] (CCW)
        m_mesh.add_face(v_north, northRing[j], northRing[next_j]);
    }
    
    m_mesh.request_vertex_normals();
    m_mesh.request_face_normals(); // Required for normal update
    m_mesh.update_normals();
    
    // Extract for Rendering (smooth shading)
    for (auto f : m_mesh.faces()) {
        for (auto v : m_mesh.fv_range(f)) {
            auto p = m_mesh.point(v);
            auto n = m_mesh.normal(v);
            m_vertices.push_back({
                QVector3D(p[0], p[1], p[2]),
                QVector3D(n[0], n[1], n[2])
            });
        }
    }
    m_vertexCount = static_cast<int>(m_vertices.size());
}

void MeshViewerWidget::uploadMesh()
{
    if (m_vertices.empty()) return;

    m_vao.create();
    m_vao.bind();

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(m_vertices.data(), static_cast<int>(m_vertices.size() * sizeof(VertexData)));

    m_program.enableAttributeArray(0);
    // Use offsetof with VertexData
    m_program.setAttributeBuffer(0, GL_FLOAT, offsetof(VertexData, position), 3, sizeof(VertexData));
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, offsetof(VertexData, normal), 3, sizeof(VertexData));

    m_vbo.release();
    m_vao.release();
}

void MeshViewerWidget::setLightDirection(const QVector3D &dir)
{
    m_lightDir = dir.normalized();
    update();
}

void MeshViewerWidget::setCameraDistance(float dist)
{
    m_cameraDistance = dist;
    update();
}

// Helper for Ray-Triangle Intersection
bool rayTriangleIntersect(const QVector3D &orig, const QVector3D &dir, 
                          const QVector3D &v0, const QVector3D &v1, const QVector3D &v2,
                          float &t, float &u, float &v) {
    QVector3D v0v1 = v1 - v0;
    QVector3D v0v2 = v2 - v0;
    QVector3D pvec = QVector3D::crossProduct(dir, v0v2);
    float det = QVector3D::dotProduct(v0v1, pvec);
    
    if (std::abs(det) < 1e-8) return false;
    float invDet = 1 / det;
    
    QVector3D tvec = orig - v0;
    u = QVector3D::dotProduct(tvec, pvec) * invDet;
    if (u < 0 || u > 1) return false;
    
    QVector3D qvec = QVector3D::crossProduct(tvec, v0v1);
    v = QVector3D::dotProduct(dir, qvec) * invDet;
    if (v < 0 || u + v > 1) return false;
    
    t = QVector3D::dotProduct(v0v2, qvec) * invDet;
    return true;
}

bool MeshViewerWidget::checkRayIntersection(float x, float y, float viewWidth, float viewHeight, QVector3D &hitPos)
{
    QMatrix4x4 view;
    view.translate(0.0f, 0.0f, -m_cameraDistance);
    QMatrix4x4 proj;
    float aspect = viewWidth > 0 ? viewWidth / viewHeight : 1.0f;
    proj.perspective(45.0f, aspect, 0.1f, 100.0f);
    
    QMatrix4x4 mvp = proj * view;
    QMatrix4x4 invVP = mvp.inverted();
    
    QMatrix4x4 model;
    model.rotate(-20.0f, 1.0f, 0.0f, 0.0f);
    model.rotate(m_rotationY, 0.0f, 1.0f, 0.0f);
    QMatrix4x4 invModel = model.inverted();

    float nx = (2.0f * x / viewWidth) - 1.0f;
    float ny = 1.0f - (2.0f * y / viewHeight);
    
    QVector4D pNear = invVP * QVector4D(nx, ny, -1.0f, 1.0f);
    QVector4D pFar = invVP * QVector4D(nx, ny, 1.0f, 1.0f);
    QVector3D rayOrigWorld = (pNear / pNear.w()).toVector3D();
    QVector3D rayEndWorld = (pFar / pFar.w()).toVector3D();
    QVector3D rayDirWorld = (rayEndWorld - rayOrigWorld).normalized();
    
    QVector3D rayOrigModel = (invModel * QVector4D(rayOrigWorld, 1.0)).toVector3D();
    QVector3D rayDirModel = (invModel * QVector4D(rayDirWorld, 0.0)).toVector3D().normalized();
    
    float minT = std::numeric_limits<float>::max();
    bool hit = false;
    SurfaceAgent newAgent;
    
    for (auto f : m_mesh.faces()) {
        auto fv = m_mesh.fv_range(f);
        auto it = fv.begin();
        auto p0 = m_mesh.point(*it);
        auto p1 = m_mesh.point(*(++it));
        auto p2 = m_mesh.point(*(++it));
        
        QVector3D v0(p0[0], p0[1], p0[2]);
        QVector3D v1(p1[0], p1[1], p1[2]);
        QVector3D v2(p2[0], p2[1], p2[2]);
        
        float t, u, v; // u,v corr. to v1, v2
        if (rayTriangleIntersect(rayOrigModel, rayDirModel, v0, v1, v2, t, u, v)) {
            if (t > 0 && t < minT) {
                minT = t;
                hit = true;
                newAgent.face = f;
                // Bary: w (v0), u (v1), v (v2)
                newAgent.bary = QVector3D(1.0f - u - v, u, v);
                
                // Random Tangent Velocity
                float du = (QRandomGenerator::global()->generateDouble() - 0.5) * 0.02; // Faster
                float dv = (QRandomGenerator::global()->generateDouble() - 0.5) * 0.02;
                newAgent.velocity = QVector3D(du, dv, -(du+dv)); 
                newAgent.color = QColor::fromHsvF(QRandomGenerator::global()->generateDouble(), 1.0, 1.0);
            }
        }
    }
    
    if (hit) {
        m_surfaceAgents.push_back(newAgent);
        hitPos = rayOrigWorld + rayDirWorld * minT;
        return true;
    }
    return false;
}

void MeshViewerWidget::updateAgents() {
    for (auto &agent : m_surfaceAgents) {
        // Record trail
        QVector3D currentPos = getAgentWorldPos(agent);
        agent.trail.push_back(currentPos);
        if (agent.trail.size() > 100) agent.trail.pop_front();
        
        // Move
        agent.bary += agent.velocity;
        
        // Robust crossing logic
        // Only handle one crossing at a time, priority: w, u, v
        // "Nudge" means: if we cross into neighbor, set coordinate to epsilon, not 0.
        
        int crossIndex = -1;
        // Check strict < 0
        if (agent.bary.x() < 0) crossIndex = 0; // w corresponds to v0. opposite edge: v1-v2
        else if (agent.bary.y() < 0) crossIndex = 1; // u corresponds to v1. opposite edge: v2-v0
        else if (agent.bary.z() < 0) crossIndex = 2; // v corresponds to v2. opposite edge: v0-v1
        
        if (crossIndex != -1) {
            std::vector<MyMesh::HalfedgeHandle> hes;
            for (auto he : m_mesh.fh_range(agent.face)) hes.push_back(he);
            
            std::vector<MyMesh::VertexHandle> vhs;
            for (auto v : m_mesh.fv_range(agent.face)) vhs.push_back(v);
            
            // Map Index to Target Edge Vertices
            // i=0 (w, v0) -> Edge v1-v2.
            // i=1 (u, v1) -> Edge v2-v0.
            // i=2 (v, v2) -> Edge v0-v1.
            MyMesh::VertexHandle startV = vhs[(crossIndex + 1) % 3];
            MyMesh::VertexHandle endV = vhs[(crossIndex + 2) % 3];
            
            bool found = false;
            MyMesh::HalfedgeHandle targetHe;
            for (auto he : hes) {
                if (m_mesh.from_vertex_handle(he) == startV && m_mesh.to_vertex_handle(he) == endV) {
                    targetHe = he;
                    found = true;
                    break;
                }
            }
            
            if (found) {
                auto opp = m_mesh.opposite_halfedge_handle(targetHe);
                if (!m_mesh.is_boundary(opp)) {
                    agent.face = m_mesh.face_handle(opp);
                    
                    // Simple Transition: Just force inside
                    // The coordinate for the new vertex (the one opposite the shared edge) is 0+epsilon.
                    // The other two coordinates need to be swapped/adjusted.
                    // A proper barycentric transition requires projection.
                    // But for random walk, we can just strictly clamp the crossed coordinate to epsilon
                    // and shuffle the others to keep sum=1? No, position must match.
                    
                    // Let's just generate random position on new face to prevent stuck/dead logic
                    // User complained about "respawning".
                    // Continuity is preferred.
                    // Let's try:
                    // 1. Calc World Pos on Edge.
                    // 2. Project world pos to new face barycentrics.
                    // 3. Keep velocity? Or randomize? Randomize is safer for now.
                    
                    QVector3D pWorld = getAgentWorldPos(agent); // Approx on edge
                    // Find barycentric on new face for pWorld... expensive?
                    // Cheap Hack: Set barycenter
                    agent.bary = QVector3D(0.33, 0.33, 0.33);
                    // agent.velocity = ... ? Keep same direction?
                    // Randomize avoids stuck loop
                    float du = (QRandomGenerator::global()->generateDouble() - 0.5) * 0.02;
                    float dv = (QRandomGenerator::global()->generateDouble() - 0.5) * 0.02;
                    agent.velocity = QVector3D(du, dv, -(du+dv));
                    
                } else {
                    // Bounce
                    agent.velocity = -agent.velocity;
                    // Clamp to inside
                    if (crossIndex==0) agent.bary.setX(0.01);
                    if (crossIndex==1) agent.bary.setY(0.01);
                    if (crossIndex==2) agent.bary.setZ(0.01);
                    // Re-normalize sum?
                    float sum = agent.bary.x() + agent.bary.y() + agent.bary.z();
                    agent.bary /= sum;
                }
            }
        }
    }
}

QVector3D MeshViewerWidget::getAgentWorldPos(const SurfaceAgent &agent) {
    if (!agent.face.is_valid()) return QVector3D();
    auto fv = m_mesh.fv_range(agent.face);
    auto it = fv.begin();
    auto p0 = m_mesh.point(*it);
    auto p1 = m_mesh.point(*(++it));
    auto p2 = m_mesh.point(*(++it));
    QVector3D v0(p0[0], p0[1], p0[2]);
    QVector3D v1(p1[0], p1[1], p1[2]);
    QVector3D v2(p2[0], p2[1], p2[2]);
    return v0 * agent.bary.x() + v1 * agent.bary.y() + v2 * agent.bary.z();
}

std::vector<MeshViewerWidget::AgentRenderInfo> MeshViewerWidget::getProjectedAgents(float viewWidth, float viewHeight) {
    std::vector<AgentRenderInfo> projected;
    
    QMatrix4x4 view;
    view.translate(0.0f, 0.0f, -m_cameraDistance);
    QMatrix4x4 proj;
    float aspect = viewWidth > 0 ? viewWidth / viewHeight : 1.0f;
    proj.perspective(45.0f, aspect, 0.1f, 100.0f);
    QMatrix4x4 model;
    model.rotate(-20.0f, 1.0f, 0.0f, 0.0f);
    model.rotate(m_rotationY, 0.0f, 1.0f, 0.0f);
    QMatrix4x4 mvp = proj * view * model;
    
    for (const auto &agent : m_surfaceAgents) {
        // Head
        QVector3D worldPos = getAgentWorldPos(agent);
        QVector4D clip = mvp * QVector4D(worldPos, 1.0);
        
        AgentRenderInfo info;
        info.color = agent.color;
        bool visible = false;
        
        if (clip.w() > 0) {
            QVector3D ndc = clip.toVector3D() / clip.w();
            if (ndc.z() >= -1 && ndc.z() <= 1 && ndc.x() >= -1 && ndc.x() <= 1 && ndc.y() >= -1 && ndc.y() <= 1) {
                float sx = (ndc.x() + 1.0f) * 0.5f * viewWidth;
                float sy = (1.0f - ndc.y()) * 0.5f * viewHeight;
                info.screenPos = QVector2D(sx, sy);
                visible = true;
            }
        }
        
        if (visible) {
            // Trail
            for (const auto &p : agent.trail) {
                 QVector4D tClip = mvp * QVector4D(p, 1.0);
                 if (tClip.w() > 0) {
                     QVector3D tNdc = tClip.toVector3D() / tClip.w();
                     // Don't clip strictly for trails, looks better if trails go off screen usually
                     float tx = (tNdc.x() + 1.0f) * 0.5f * viewWidth;
                     float ty = (1.0f - tNdc.y()) * 0.5f * viewHeight;
                     info.screenTrail.push_back(QVector2D(tx, ty));
                 }
            }
            projected.push_back(info);
        }
    }
    return projected;
}
