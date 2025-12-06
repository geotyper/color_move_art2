#include "MeshViewerWidget.h"
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QRandomGenerator>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QtMath>
#include <cmath>
#include <QDebug>
#include <limits>
#include <algorithm>

// We need boost geometry specifics here too if not fully in header
namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

MeshViewerWidget::MeshViewerWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MeshViewerWidget::updateAgents);
    m_timer->start(16); // ~60 FPS
    m_agentsPaused = false;
}

MeshViewerWidget::~MeshViewerWidget()
{
    makeCurrent();
    m_vbo.destroy();
    m_vao.destroy();
    doneCurrent();
}

void MeshViewerWidget::setLightDirection(const QVector3D &dir)
{
    m_lightDir = glm::normalize(glm::vec3(dir.x(), dir.y(), dir.z()));
    update();
}

void MeshViewerWidget::setCameraDistance(float dist)
{
    m_cameraDistance = dist;
    update();
}

void MeshViewerWidget::setAgentsPaused(bool paused)
{
    m_agentsPaused = paused;
}

void MeshViewerWidget::initializeGL()
{
    initializeOpenGLFunctions();
    
    // Create Shader Program
    if (!m_program.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/mesh.vert"))
        qDebug() << "Vertex shader error:" << m_program.log();
    
    if (!m_program.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/mesh.frag"))
        qDebug() << "Fragment shader error:" << m_program.log();
        
    if (!m_program.link())
        qDebug() << "Link error:" << m_program.log();
        
    // Create Sphere
    buildSphere();
    
    // VAO/VBO
    m_vao.create();
    m_vao.bind();
    
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(m_vertices.data(), m_vertices.size() * sizeof(VertexData));
    
    // Position (Loc 0)
    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, offsetof(VertexData, position), 3, sizeof(VertexData));
    
    // Normal (Loc 1)
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, offsetof(VertexData, normal), 3, sizeof(VertexData));
    
    m_vao.release();
    
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void MeshViewerWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void MeshViewerWidget::paintGL()
{
    glClearColor(0.2f, 0.2f, 0.25f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_vertexCount == 0) return;

    // GLM Matrix Setup
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(m_rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(m_rotationY), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -m_cameraDistance));

    float aspect = width() > 0 ? (float)width() / (float)height() : 1.0f;
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

    glm::mat4 mvp = proj * view * model;

    m_program.bind();
    
    int locMVP = m_program.uniformLocation("u_mvp");
    if (locMVP != -1) glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    
    int locModel = m_program.uniformLocation("u_model");
    if (locModel != -1) glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
    
    m_program.setUniformValue("u_wireframe", false);
    
    int locLight = m_program.uniformLocation("u_lightDir");
    if (locLight != -1) glUniform3fv(locLight, 1, glm::value_ptr(m_lightDir));
    
    int locCam = m_program.uniformLocation("u_cameraPos");
    if (locCam != -1) glUniform3f(locCam, 0.0f, 0.0f, m_cameraDistance);

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
    m_surfaceAgents.clear();
    
    constexpr float pi = 3.14159265359f;
    const int slices = 32;
    const int stacks = 32;
    const float radius = 1.0f;
    
    // 1. Create Vertices
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
            float x = zr * std::cos(lng);
            float y = zr * std::sin(lng);
            ring.push_back(m_mesh.add_vertex(MyMesh::Point(x * radius, y * radius, z * radius)));
        }
        rings.push_back(ring);
    }
    
    MyMesh::VertexHandle v_south = m_mesh.add_vertex(MyMesh::Point(0, 0, -radius));
    
    // 2. Create Faces
    // Top Cap
    for (int i = 0; i < slices; ++i) {
        m_mesh.add_face(v_north, rings.back()[i], rings.back()[(i + 1) % slices]);
    }
    
    // Middle
    for (int i = 0; i < stacks - 2; ++i) {
        for (int j = 0; j < slices; ++j) {
            MyMesh::VertexHandle next_j = rings[i][(j + 1) % slices];
            MyMesh::VertexHandle next_row_j = rings[i + 1][j];
            MyMesh::VertexHandle next_row_next_j = rings[i + 1][(j + 1) % slices];
            
            // CCW: Current, Right, Up
            m_mesh.add_face(rings[i][j], next_j, next_row_j);
            m_mesh.add_face(next_j, next_row_next_j, next_row_j);
        }
    }
    
    // Bottom Cap
    for (int i = 0; i < slices; ++i) {
        m_mesh.add_face(rings[0][i], v_south, rings[0][(i + 1) % slices]);
    }
    
    m_mesh.request_face_normals();
    m_mesh.update_normals();
    
    // 3. Build Rendering Data
    m_rtree.clear();
    
    for (auto f_it = m_mesh.faces_begin(); f_it != m_mesh.faces_end(); ++f_it) {
        auto fv_it = m_mesh.fv_iter(*f_it);
        auto p0 = m_mesh.point(*fv_it);
        auto p1 = m_mesh.point(*(++fv_it));
        auto p2 = m_mesh.point(*(++fv_it));
        
        // Use GLM types
        glm::vec3 v0(p0[0], p0[1], p0[2]);
        glm::vec3 v1(p1[0], p1[1], p1[2]);
        glm::vec3 v2(p2[0], p2[1], p2[2]);
        glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
        
        m_vertices.push_back({v0, n});
        m_vertices.push_back({v1, n});
        m_vertices.push_back({v2, n});
        
        // Add to R-tree
        float minX = std::min({v0.x, v1.x, v2.x});
        float minY = std::min({v0.y, v1.y, v2.y});
        float minZ = std::min({v0.z, v1.z, v2.z});
        float maxX = std::max({v0.x, v1.x, v2.x});
        float maxY = std::max({v0.y, v1.y, v2.y});
        float maxZ = std::max({v0.z, v1.z, v2.z});
        
        BoostBox box(BoostPoint(minX, minY, minZ), BoostPoint(maxX, maxY, maxZ));
        m_rtree.insert(std::make_pair(box, f_it->idx()));
    }
    
    m_vertexCount = static_cast<int>(m_vertices.size());
}

void MeshViewerWidget::uploadMesh()
{
}

bool rayTriangleIntersect(const glm::vec3 &orig, const glm::vec3 &dir, 
                          const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2, 
                          float &t, float &u, float &v) 
{
    glm::vec3 v0v1 = v1 - v0;
    glm::vec3 v0v2 = v2 - v0;
    glm::vec3 pvec = glm::cross(dir, v0v2);
    float det = glm::dot(v0v1, pvec);
    
    if (det < 1e-8) return false;
    
    float invDet = 1.0f / det;
    glm::vec3 tvec = orig - v0;
    u = glm::dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    
    glm::vec3 qvec = glm::cross(tvec, v0v1);
    v = glm::dot(dir, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;
    
    t = glm::dot(v0v2, qvec) * invDet;
    return true;
}

bool MeshViewerWidget::checkRayIntersection(float x, float y, float viewWidth, float viewHeight, glm::vec3 &hitPos)
{
    if (m_vertexCount == 0) return false;
    
    float aspect = viewWidth > 0 ? viewWidth / viewHeight : 1.0f;
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(m_rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(m_rotationY), glm::vec3(0.0f, 1.0f, 0.0f));
    
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -m_cameraDistance));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    
    glm::mat4 invMVP = glm::inverse(proj * view * model);
    
    float ndcX = (2.0f * x) / viewWidth - 1.0f;
    float ndcY = 1.0f - (2.0f * y) / viewHeight;
    
    glm::vec4 nearPoint = invMVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPoint = invMVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    
    glm::vec3 rayOrigWorld = glm::vec3(nearPoint);
    glm::vec3 rayDirWorld = glm::normalize(glm::vec3(farPoint - nearPoint));
    
    float minX = std::min(rayOrigWorld.x, rayOrigWorld.x + rayDirWorld.x * 100.0f);
    float minY = std::min(rayOrigWorld.y, rayOrigWorld.y + rayDirWorld.y * 100.0f);
    float minZ = std::min(rayOrigWorld.z, rayOrigWorld.z + rayDirWorld.z * 100.0f);
    float maxX = std::max(rayOrigWorld.x, rayOrigWorld.x + rayDirWorld.x * 100.0f);
    float maxY = std::max(rayOrigWorld.y, rayOrigWorld.y + rayDirWorld.y * 100.0f);
    float maxZ = std::max(rayOrigWorld.z, rayOrigWorld.z + rayDirWorld.z * 100.0f);
    
    BoostBox queryBox(BoostPoint(minX, minY, minZ), BoostPoint(maxX, maxY, maxZ));
    std::vector<BoostValue> result;
    m_rtree.query(bgi::intersects(queryBox), std::back_inserter(result));
    
    bool hit = false;
    float minT = 1e30f;
    
    SurfaceAgent newAgent;
    
    for (const auto &val : result) {
        int f_idx = val.second;
        OpenMesh::FaceHandle f(f_idx);
        
        auto fv = m_mesh.fv_range(f);
        auto it = fv.begin();
        auto p0 = m_mesh.point(*it);
        auto p1 = m_mesh.point(*(++it));
        auto p2 = m_mesh.point(*(++it));
        
        glm::vec3 v0(p0[0], p0[1], p0[2]);
        glm::vec3 v1(p1[0], p1[1], p1[2]);
        glm::vec3 v2(p2[0], p2[1], p2[2]);
        
        float t, u, v;
        if (rayTriangleIntersect(rayOrigWorld, rayDirWorld, v0, v1, v2, t, u, v)) {
            if (t > 0 && t < minT) {
                minT = t;
                hit = true;
                newAgent.face = f;
                newAgent.bary = glm::vec3(1.0f - u - v, u, v);
                
                // Calculate random tangent
                glm::vec3 n = glm::normalize(glm::cross(v1-v0, v2-v0));
                
                // Random vector not parallel to n
                float rx = (float)std::rand() / RAND_MAX - 0.5f;
                float ry = (float)std::rand() / RAND_MAX - 0.5f;
                float rz = (float)std::rand() / RAND_MAX - 0.5f;
                glm::vec3 rnd(rx, ry, rz);
                
                glm::vec3 tangent = glm::normalize(rnd - glm::dot(rnd, n) * n);
                if (glm::length(tangent) < 0.1f) {
                     // Degenerate case fallback
                     if (std::abs(n.z) < 0.9f) tangent = glm::normalize(glm::cross(n, glm::vec3(0,0,1)));
                     else tangent = glm::normalize(glm::cross(n, glm::vec3(0,1,0)));
                }

                newAgent.speed = 0.05f; // Increased speed for visibility
                newAgent.worldVelocity = tangent * newAgent.speed;
                // Color from active palette (cycling if more agents than colors)
                const QVector<QVector<QVector3D>>* palettesPtr = m_externalPalettes ? m_externalPalettes : &m_palettes;
                const QVector<QVector3D>& palette = (*palettesPtr)[m_paletteIndex % palettesPtr->size()];
                int colorIdx = (int)m_surfaceAgents.size() % palette.size();
                const QVector3D& pal = palette[colorIdx];
                newAgent.color = QColor::fromRgbF(pal.x(), pal.y(), pal.z());
                newAgent.maxAge = m_agentLifetime;
                newAgent.age = 0;
                newAgent.layer = QRandomGenerator::global()->bounded(32); // stick to a fixed texture slice
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

glm::vec3 solveBarycentricVelocity(const glm::vec3 &worldVel, const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2) {
    glm::vec3 e1 = v1 - v0;
    glm::vec3 e2 = v2 - v0;
    
    float d11 = glm::dot(e1, e1);
    float d12 = glm::dot(e1, e2);
    float d22 = glm::dot(e2, e2);
    float det = d11 * d22 - d12 * d12;
    float invDet = (std::abs(det) < 1e-8f) ? 0.0f : 1.0f / det;
    
    float b1 = glm::dot(worldVel, e1);
    float b2 = glm::dot(worldVel, e2);
    
    float u_dot = (d22 * b1 - d12 * b2) * invDet;
    float v_dot = (d11 * b2 - d12 * b1) * invDet;
    float w_dot = -u_dot - v_dot;
    
    return glm::vec3(w_dot, u_dot, v_dot);
}

void MeshViewerWidget::updateAgents() {
    if (m_agentsPaused) return;
    if (!m_surfaceAgents.empty()) {
        m_surfaceAgents.erase(std::remove_if(m_surfaceAgents.begin(), m_surfaceAgents.end(),
            [](const SurfaceAgent &a) { return a.age >= a.maxAge; }),
            m_surfaceAgents.end());
    }
    
    if (m_surfaceAgents.empty()) return;
    update();

    int agentId = 0;
    for (auto &agent : m_surfaceAgents) {
        agentId++;
        agent.age++;
        float remainingTime = 1.0f;
        int loopCount = 0;
        
        while (remainingTime > 1e-4f) {
            loopCount++;
            if (loopCount > 100) {
                 qDebug() << "Agent" << agentId << "stuck in loop!";
                 break; 
            }

            if (!agent.face.is_valid()) {
                qDebug() << "Agent" << agentId << "has invalid face!";
                break;
            }

            auto fv = m_mesh.fv_range(agent.face);
            auto it = fv.begin();
            // Safety check for degenerate faces
            if (it == fv.end()) { qDebug() << "Agent" << agentId << "degenerate face 0"; agent.face = OpenMesh::FaceHandle(); break; }
            auto p0 = m_mesh.point(*it);
            if (++it == fv.end()) { qDebug() << "Agent" << agentId << "degenerate face 1"; agent.face = OpenMesh::FaceHandle(); break; }
            auto p1 = m_mesh.point(*it);
            if (++it == fv.end()) { qDebug() << "Agent" << agentId << "degenerate face 2"; agent.face = OpenMesh::FaceHandle(); break; }
            auto p2 = m_mesh.point(*it);
            
            glm::vec3 v0(p0[0], p0[1], p0[2]);
            glm::vec3 v1(p1[0], p1[1], p1[2]);
            glm::vec3 v2(p2[0], p2[1], p2[2]);
            
            glm::vec3 baryVel = solveBarycentricVelocity(agent.worldVelocity, v0, v1, v2);
            if (std::isnan(baryVel.x) || std::isnan(baryVel.y) || std::isnan(baryVel.z)) {
                qDebug() << "Agent" << agentId << "NaN velocity!";
                break;
            }
            
            float minT = remainingTime;
            int hitIndex = -1; 
            
            float baryArr[3] = {agent.bary.x, agent.bary.y, agent.bary.z};
            float velArr[3] = {baryVel.x, baryVel.y, baryVel.z};
            
            for (int i = 0; i < 3; ++i) {
                if (velArr[i] < -1e-8f) { 
                    float t = -baryArr[i] / velArr[i];
                    if (t < minT) {
                        minT = t;
                        hitIndex = i;
                    }
                }
            }
            
            agent.bary += baryVel * minT;
            
            if (minT > 0) {
                 glm::vec3 currentPos = v0 * agent.bary.x + v1 * agent.bary.y + v2 * agent.bary.z;
                 
                 bool shouldPush = false;
                 if (agent.trail.empty()) {
                     shouldPush = true;
                 } else {
                     glm::vec3 diff = agent.trail.back() - currentPos;
                     if (glm::dot(diff, diff) > 1e-6f) {
                         shouldPush = true;
                     }
                 }
                 
                 if (shouldPush) {
                       agent.trail.push_back(currentPos);
                       if (agent.trail.size() > 1000) agent.trail.pop_front();
                 }
            }

            remainingTime -= minT;
            
            if (hitIndex != -1) {
                // Determine edge indices
                auto fv_it2 = m_mesh.fv_iter(agent.face);
                auto vh0 = *fv_it2;
                auto vh1 = *(++fv_it2);
                auto vh2 = *(++fv_it2);
                
                MyMesh::VertexHandle va, vb;
                // Correct logic for OpenMesh fv_iter order matches bary order?
                // bary[0] -> v0, bary[1] -> v1, bary[2] -> v2.
                // If bary[0] hits 0, we are on edge v1-v2.
                if (hitIndex == 0) { va = vh1; vb = vh2; } 
                if (hitIndex == 1) { va = vh2; vb = vh0; } 
                if (hitIndex == 2) { va = vh0; vb = vh1; } 
                
                OpenMesh::HalfedgeHandle hitEdge = m_mesh.find_halfedge(va, vb);
                if (!hitEdge.is_valid()) {
                     qDebug() << "Agent" << agentId << "hit invalid edge!";
                     agent.worldVelocity = -agent.worldVelocity; // Reflect on invalid edge
                     remainingTime = 0;
                     break;
                }
                auto opp = m_mesh.opposite_halfedge_handle(hitEdge);
                
                if (!m_mesh.is_boundary(opp)) {
                    agent.face = m_mesh.face_handle(opp);
                    if (!agent.face.is_valid()) {
                        remainingTime = 0; break; 
                    }
                    
                    // Recompute physics on new face
                    auto nfv = m_mesh.fv_range(agent.face);
                    auto nit = nfv.begin();
                    auto np0 = m_mesh.point(*nit);
                    auto np1 = m_mesh.point(*(++nit));
                    auto np2 = m_mesh.point(*(++nit));
                    glm::vec3 nv0(np0[0], np0[1], np0[2]);
                    glm::vec3 nv1(np1[0], np1[1], np1[2]);
                    glm::vec3 nv2(np2[0], np2[1], np2[2]);
                    
                    glm::vec3 ne1 = nv1 - nv0;
                    glm::vec3 ne2 = nv2 - nv0;
                    
                    // Simple projection to new barycentric coords
                    // Approximate wPos from shared edge
                    glm::vec3 wPos = v0 * agent.bary.x + v1 * agent.bary.y + v2 * agent.bary.z;
                    glm::vec3 P_v0 = wPos - nv0;
                    
                    float d00 = glm::dot(ne1, ne1);
                    float d01 = glm::dot(ne1, ne2);
                    float d11 = glm::dot(ne2, ne2);
                    float id = 1.0f / (d00 * d11 - d01 * d01);
                    float dP0 = glm::dot(P_v0, ne1);
                    float dP1 = glm::dot(P_v0, ne2);
                    float nu = (d11 * dP0 - d01 * dP1) * id;
                    float nv = (d00 * dP1 - d01 * dP0) * id;
                    float nw = 1.0f - nu - nv;
                    
                    agent.bary = glm::vec3(nw, nu, nv);
                    
                    glm::vec3 normal = glm::normalize(glm::cross(ne1, ne2));
                    glm::vec3 t = agent.worldVelocity - glm::dot(agent.worldVelocity, normal) * normal;
                    agent.worldVelocity = glm::normalize(t) * agent.speed;
                } else {
                    agent.worldVelocity = -agent.worldVelocity; // Bounce
                    remainingTime = 0; 
                }
            }
        }
    }
}

glm::vec3 MeshViewerWidget::getAgentWorldPos(const SurfaceAgent &agent) {
    if (!agent.face.is_valid()) return glm::vec3(0.0f);
    auto fv = m_mesh.fv_range(agent.face);
    auto it = fv.begin();
    auto p0 = m_mesh.point(*it);
    auto p1 = m_mesh.point(*(++it));
    auto p2 = m_mesh.point(*(++it));
    glm::vec3 v0(p0[0], p0[1], p0[2]);
    glm::vec3 v1(p1[0], p1[1], p1[2]);
    glm::vec3 v2(p2[0], p2[1], p2[2]);
    return v0 * agent.bary.x + v1 * agent.bary.y + v2 * agent.bary.z;
}

std::vector<MeshViewerWidget::AgentRenderInfo> MeshViewerWidget::getProjectedAgents(float viewWidth, float viewHeight) {
    std::vector<AgentRenderInfo> projected;
    
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(m_rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(m_rotationY), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -m_cameraDistance));
    
    // Debug Instability
    qDebug() << "getProjectedAgents: Rot" << m_rotationX << m_rotationY << "CamDist" << m_cameraDistance << "ViewSize" << viewWidth << viewHeight;
    if (!m_surfaceAgents.empty()) {
       qDebug() << "Agent[0] World" << getAgentWorldPos(m_surfaceAgents[0]).x << getAgentWorldPos(m_surfaceAgents[0]).y << getAgentWorldPos(m_surfaceAgents[0]).z;
    }

    // Use MeshViewer's own aspect ratio to match the 3D view exactly
    float aspect = width() > 0 ? (float)width() / (float)height() : 1.0f;
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    
    glm::mat4 mv = view * model;
    glm::vec4 viewport(0.0f, 0.0f, viewWidth, viewHeight);
    
    glm::vec3 sphereCenterView = glm::vec3(mv * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    
    auto isVisible = [&](const glm::vec3 &worldPos) -> bool {
        glm::vec3 pView = glm::vec3(mv * glm::vec4(worldPos, 1.0f));
        glm::vec3 normal = glm::normalize(pView - sphereCenterView);
        return normal.z > 0.0f;
    };

    auto project = [&](const glm::vec3 &worldPos) -> QVector2D {
        glm::vec3 p = glm::project(worldPos, mv, proj, viewport);
        return QVector2D(p.x, p.y);
    };

    auto lambert = [&](const glm::vec3 &worldPos) -> float {
        glm::vec3 n = glm::normalize(worldPos); // sphere radius 1
        float ndl = std::max(0.0f, glm::dot(n, glm::normalize(m_lightDir)));
        // Higher contrast and slightly brighter overall to match the 3D view appearance.
        return std::clamp(0.5f + 0.6f * ndl, 0.0f, 1.0f);
    };

    for (const auto &agent : m_surfaceAgents) {
        glm::vec3 pos = getAgentWorldPos(agent);
        
        AgentRenderInfo info;
        info.screenPos = project(pos);
        info.color = agent.color;
        info.isVisible = isVisible(pos);
        info.layer = agent.layer;
        info.headBrightness = lambert(pos);
        
        std::vector<QVector2D> currentSegment;
        std::vector<float> currentBright;
        for (const auto &p : agent.trail) {
             if (isVisible(p)) {
                 currentSegment.push_back(project(p));
                 currentBright.push_back(lambert(p));
             } else {
                 if (!currentSegment.empty()) {
                     info.trailSegments.push_back(currentSegment);
                     info.trailBrightness.push_back(currentBright);
                     currentSegment.clear();
                     currentBright.clear();
                 }
             }
        }
        if (!currentSegment.empty()) {
            info.trailSegments.push_back(currentSegment);
            info.trailBrightness.push_back(currentBright);
        }
        
        projected.push_back(info);
    }
    
    return projected;
}

void MeshViewerWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_isMouseDown = true;
        m_lastMousePos = event->pos();
    }
}

void MeshViewerWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_isMouseDown && (event->buttons() & Qt::LeftButton)) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        
        m_rotationY += delta.x() * 0.5f;
        m_rotationX += delta.y() * 0.5f;
        
        update();
    }
}
