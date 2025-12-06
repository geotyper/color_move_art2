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
    m_vertexCount = static_cast<int>(m_vertices.size());
    
    // Build R-tree
    m_rtree.clear();
    for (auto f : m_mesh.faces()) {
        // Calculate AABB for face
        float minX = std::numeric_limits<float>::max(), minY = minX, minZ = minX;
        float maxX = std::numeric_limits<float>::lowest(), maxY = maxX, maxZ = maxX;
        
        for (auto v : m_mesh.fv_range(f)) {
            auto p = m_mesh.point(v);
            minX = std::min(minX, p[0]); minY = std::min(minY, p[1]); minZ = std::min(minZ, p[2]);
            maxX = std::max(maxX, p[0]); maxY = std::max(maxY, p[1]); maxZ = std::max(maxZ, p[2]);
        }
        
        BoostBox box(BoostPoint(minX, minY, minZ), BoostPoint(maxX, maxY, maxZ));
        m_rtree.insert(std::make_pair(box, f.idx()));
    }
    }
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
    

    
    // Broad Phase: R-tree query
    // Create segment from ray start to end (or distinct point far away)
    // Ray originates at rayOrigModel, goes slightly past 1.0 logic, but let's effectively treat it as segment
    // Intersecting Ray with Box in Boost requires a linear geometry?
    // intersects(segment, box) is supported.
    
    // Create query segment
    BoostPoint p1(rayOrigModel.x(), rayOrigModel.y(), rayOrigModel.z());
    // Create a point very far in direction
    QVector3D rayFar = rayOrigModel + rayDirModel * 1000.0f; // 1000 units is enough for our radius 1 sphere
    BoostPoint p2(rayFar.x(), rayFar.y(), rayFar.z());
    
    bg::model::segment<BoostPoint> raySegment(p1, p2);
    
    std::vector<BoostValue> candidates;
    m_rtree.query(bgi::intersects(raySegment), std::back_inserter(candidates));
    
    for (const auto &val : candidates) {
        auto f = m_mesh.face_handle(val.second);
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
                
                // Random Tangent Velocity in World Space
                float du = (QRandomGenerator::global()->generateDouble() - 0.5);
                float dv = (QRandomGenerator::global()->generateDouble() - 0.5);
                
                // Construct tangent plane basis? Or just random vector projected.
                QVector3D n = QVector3D::crossProduct(v1-v0, v2-v0).normalized();
                QVector3D rnd(du, dv, (QRandomGenerator::global()->generateDouble() - 0.5));
                QVector3D tangent = (rnd - QVector3D::dotProduct(rnd, n) * n).normalized();
                
                newAgent.speed = 0.005f; // reduced speed for stability check
                newAgent.worldVelocity = tangent * newAgent.speed;
                newAgent.color = QColor::fromHsvF(QRandomGenerator::global()->generateDouble(), 1.0, 1.0);
                newAgent.maxAge = m_agentLifetime;
                newAgent.age = 0;
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

// Convert World Velocity to Barycentric Change Rates (w_dot, u_dot, v_dot)
QVector3D solveBarycentricVelocity(const QVector3D &worldVel, const QVector3D &v0, const QVector3D &v1, const QVector3D &v2) {
    // V = u_dot * (v1 - v0) + v_dot * (v2 - v0)
    // We solve for u_dot, v_dot.
    // Overdetermined system (3 eq, 2 var). Use Least Squares or project to 2D?
    // Projecting to 2D plane basis is best. But algebraic solve works too.
    // V . e1 = u_dot * (e1.e1) + v_dot * (e2.e1)
    // V . e2 = u_dot * (e1.e2) + v_dot * (e2.e2)
    
    QVector3D e1 = v1 - v0;
    QVector3D e2 = v2 - v0;
    
    float dot00 = QVector3D::dotProduct(e1, e1);
    float dot01 = QVector3D::dotProduct(e1, e2);
    float dot11 = QVector3D::dotProduct(e2, e2);
    float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
    
    float dotV0 = QVector3D::dotProduct(worldVel, e1);
    float dotV1 = QVector3D::dotProduct(worldVel, e2);
    
    float u_dot = (dot11 * dotV0 - dot01 * dotV1) * invDenom;
    float v_dot = (dot00 * dotV1 - dot01 * dotV0) * invDenom;
    float w_dot = -u_dot - v_dot;
    
    return QVector3D(w_dot, u_dot, v_dot);
}

void MeshViewerWidget::updateAgents() {
    // Remove dead agents
    if (!m_surfaceAgents.empty()) {
        m_surfaceAgents.erase(std::remove_if(m_surfaceAgents.begin(), m_surfaceAgents.end(),
            [](const SurfaceAgent &a) { return a.age >= a.maxAge; }),
            m_surfaceAgents.end());
    }

    for (auto &agent : m_surfaceAgents) {
        agent.age++;
        float remainingTime = 1.0f;
        
        while (remainingTime > 1e-4f) {
            if (!agent.face.is_valid()) break;

            // Get Vertices
            auto fv = m_mesh.fv_range(agent.face);
            auto it = fv.begin();
            auto p0 = m_mesh.point(*it);
            auto p1 = m_mesh.point(*(++it));
            auto p2 = m_mesh.point(*(++it));
            QVector3D v0(p0[0], p0[1], p0[2]);
            QVector3D v1(p1[0], p1[1], p1[2]);
            QVector3D v2(p2[0], p2[1], p2[2]);
            
            // Calculate Barycentric Velocity
            QVector3D baryVel = solveBarycentricVelocity(agent.worldVelocity, v0, v1, v2);
            
            // Calculate Max Time until Edge Hit
            // bary + t * baryVel = 0 for any component
            // t = -bary / baryVel
            float minT = remainingTime;
            int hitIndex = -1; // 0=w, 1=u, 2=v
            
            float baryArr[3] = {agent.bary.x(), agent.bary.y(), agent.bary.z()};
            float velArr[3] = {baryVel.x(), baryVel.y(), baryVel.z()};
            
            for (int i = 0; i < 3; ++i) {
                if (velArr[i] < -1e-8f) { // Moving towards 0
                    float t = -baryArr[i] / velArr[i];
                    if (t < minT) {
                        minT = t;
                        hitIndex = i;
                    }
                }
            }
            
            // Move Agent
            agent.bary += baryVel * minT;
            
            // Update Trail
            if (minT > 0) {
                 // Sample position occasionally or at end of step?
                 // For smooth curve, maybe sample each sub-step
                 // But loop might run many times.
                 // Let's just update trail once per frame outside loop? 
                 // Or here?
                 // The trails look jagged if we don't capture corners.
                 // Let's add corner point.
                 QVector3D currentPos = v0 * agent.bary.x() + v1 * agent.bary.y() + v2 * agent.bary.z();
                 if (agent.trail.empty() || (agent.trail.back() - currentPos).lengthSquared() > 1e-6) {
                      agent.trail.push_back(currentPos);
                      if (agent.trail.size() > 100) agent.trail.pop_front();
                 }
            }

            remainingTime -= minT;
            
            // Handle Crossing
            if (hitIndex != -1) {
                // We hit an edge.
                // Clamp coordinate to 0 to prevent drift
                if (hitIndex == 0) agent.bary.setX(0.0f);
                if (hitIndex == 1) agent.bary.setY(0.0f);
                if (hitIndex == 2) agent.bary.setZ(0.0f);
                
                // Identify Edge
                // w=0 (hitIndex 0) -> Edge v1-v2.
                // u=0 (hitIndex 1) -> Edge v2-v0.
                // v=0 (hitIndex 2) -> Edge v0-v1.
                std::vector<MyMesh::VertexHandle> vhs;
                vhs.push_back(*fv.begin());       // v0
                vhs.push_back(*(++fv.begin()));   // v1
                vhs.push_back(*(++++fv.begin())); // v2
                
                MyMesh::VertexHandle startV = vhs[(hitIndex + 1) % 3];
                MyMesh::VertexHandle endV = vhs[(hitIndex + 2) % 3];
                
                // Find Halfedge
                bool found = false;
                MyMesh::HalfedgeHandle targetHe;
                for (auto he : m_mesh.fh_range(agent.face)) {
                    if (m_mesh.from_vertex_handle(he) == startV && m_mesh.to_vertex_handle(he) == endV) {
                        targetHe = he;
                        found = true;
                        break;
                    }
                }
                
                bool bounced = true;
                if (found) {
                    auto opp = m_mesh.opposite_halfedge_handle(targetHe);
                    if (!m_mesh.is_boundary(opp)) {
                        bounced = false;
                        agent.face = m_mesh.face_handle(opp);
                        
                        // New Face Vertices
                        // We need to map position P (on shared edge) to new barycentrics.
                        // Or simplify: Shared vertices retain their world positions.
                        // We are at P = weightA * A + weightB * B.
                        // In new face, A and B are some vertices.
                        // Let's just re-calculate barycentric from World Pos P for robustness.
                        QVector3D wPos = v0 * agent.bary.x() + v1 * agent.bary.y() + v2 * agent.bary.z();
                        
                        // Get new vertices
                        auto nfv = m_mesh.fv_range(agent.face);
                        auto nit = nfv.begin();
                        auto np0 = m_mesh.point(*nit);
                        auto np1 = m_mesh.point(*(++nit));
                        auto np2 = m_mesh.point(*(++nit));
                        QVector3D nv0(np0[0], np0[1], np0[2]);
                        QVector3D nv1(np1[0], np1[1], np1[2]);
                        QVector3D nv2(np2[0], np2[1], np2[2]);
                        
                        // Solve for P = x*nv0 + y*nv1 + z*nv2
                        // P is on edge, so one weight is 0.
                        // But solving fully corrects any drift.
                        // Usage: same helper as solveBarycentricVelocity but for Position P relative to v0?
                        // P - v0 = u * e1 + v * e2
                        QVector3D ne1 = nv1 - nv0;
                        QVector3D ne2 = nv2 - nv0;
                        QVector3D P_v0 = wPos - nv0;
                        
                        float d00 = QVector3D::dotProduct(ne1, ne1);
                        float d01 = QVector3D::dotProduct(ne1, ne2);
                        float d11 = QVector3D::dotProduct(ne2, ne2);
                        float id = 1.0f / (d00 * d11 - d01 * d01);
                        
                        float dP0 = QVector3D::dotProduct(P_v0, ne1);
                        float dP1 = QVector3D::dotProduct(P_v0, ne2);
                        
                        float nu = (d11 * dP0 - d01 * dP1) * id;
                        float nv = (d00 * dP1 - d01 * dP0) * id;
                        float nw = 1.0f - nu - nv;
                        
                        agent.bary = QVector3D(nw, nu, nv); // (w, u, v)
                        
                        // Refract Velocity
                        // Project current velocity onto new plane
                        QVector3D normal = QVector3D::crossProduct(ne1, ne2).normalized();
                        QVector3D tangent = agent.worldVelocity - QVector3D::dotProduct(agent.worldVelocity, normal) * normal;
                        agent.worldVelocity = tangent.normalized() * agent.speed;
                        
                        // Nudge slightly into face to avoid immediate bounce back due to float precision?
                        // Or just trust the loop.
                        // Let's nudge a tiny bit towards center (0.33, 0.33, 0.33)
                        // agent.bary = agent.bary * 0.99f + QVector3D(0.33f, 0.33f, 0.33f) * 0.01f;
                        
                    }
                }
                
                if (bounced) {
                    // Reflect velocity?
                    // For now simple bounce
                     agent.worldVelocity = -agent.worldVelocity;
                     remainingTime = 0; // End step
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
    
    QMatrix4x4 mv = view * model; // ModelView for Visibility Check
    QMatrix4x4 mvp = proj * mv;   // MVP for Projection
    
    // Sphere Center in View Space (assuming model center is 0,0,0)
    QVector3D sphereCenterView = (mv * QVector4D(0,0,0,1)).toVector3D();

    auto isVisible = [&](const QVector3D &worldPos) -> bool {
        QVector3D pView = (mv * QVector4D(worldPos, 1.0)).toVector3D();
        QVector3D normal = (pView - sphereCenterView).normalized(); 
        QVector3D viewDir = -pView.normalized(); // Or just (0,0,1) if proper View Space? 
                                                 // In View Space, camera is at 0,0,0 looking down -Z.
                                                 // So viewDir is (0,0,1)? 
                                                 // Dot(Normal, (0,0,1)) > 0 means Normal.z > 0.
                                                 // Or simple check: Normal.z > 0.
                                                 // Let's use Normal.z > 0 for standard view space.
        return normal.z() > 0.05f; // Slight culling bias to hide horizon artifacts
    };

    auto project = [&](const QVector3D &worldPos) -> QVector2D {
        QVector4D clip = mvp * QVector4D(worldPos, 1.0);
        if (clip.w() > 0) {
            QVector3D ndc = clip.toVector3D() / clip.w();
             float sx = (ndc.x() + 1.0f) * 0.5f * viewWidth;
             float sy = (1.0f - ndc.y()) * 0.5f * viewHeight;
             return QVector2D(sx, sy);
        }
        return QVector2D(-10000, -10000);
    };
    
    for (const auto &agent : m_surfaceAgents) {
        QVector3D worldPos = getAgentWorldPos(agent);
        
        AgentRenderInfo info;
        info.color = agent.color;
        info.isVisible = isVisible(worldPos);
        info.screenPos = project(worldPos);
        
        // Process Trail
        std::vector<QVector2D> currentSegment;
        for (const auto &p : agent.trail) {
             if (isVisible(p)) {
                 currentSegment.push_back(project(p));
             } else {
                 if (!currentSegment.empty()) {
                     info.trailSegments.push_back(currentSegment);
                     currentSegment.clear();
                 }
             }
        }
        if (!currentSegment.empty()) {
            info.trailSegments.push_back(currentSegment);
        }
        
        projected.push_back(info);
    }
    return projected;
}
