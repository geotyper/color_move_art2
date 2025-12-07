#include "MeshViewerWidget.h"
#include "GeomCreate.h"
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QRandomGenerator>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QtMath>
#include <cmath>
#include <QDebug>
#include <QOpenGLContext>
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
        
    // Create VAO/VBO first so buildSphere (which calls updateMesh) works
    m_vao.create();
    m_vao.bind();
    
    m_vbo.create();
    m_vbo.bind();
    // No allocate yet, updateMesh will do it
    
    // Create Sphere
    buildSphere(); 
    
    // VBO was released by updateMesh, so we must rebind it for setAttributeBuffer!
    m_vbo.bind();
    
    // Position (Loc 0)
    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, offsetof(VertexData, position), 3, sizeof(VertexData));
    
    // Normal (Loc 1)
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, offsetof(VertexData, normal), 3, sizeof(VertexData));
    
    m_vao.release();
    
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); // Debug: Disable culling to see if winding is wrong
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

void MeshViewerWidget::updateMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    qDebug() << "updateMesh called with" << vertices.size() << "vertices and" << indices.size() << "indices";
    m_mesh.clear();
    m_vertices.clear();
    m_surfaceAgents.clear();
    
    // 1. Add Vertices to OpenMesh (deduplicating)
    struct VertexKey {
        long long x, y, z;
        bool operator<(const VertexKey& o) const {
            if (x != o.x) return x < o.x;
            if (y != o.y) return y < o.y;
            return z < o.z;
        }
    };
    
    const float quant = 100000.0f;
    std::map<VertexKey, MyMesh::VertexHandle> uniqueVertices;
    std::vector<MyMesh::VertexHandle> indexToHandle;
    // indexToHandle size based on max index in indices? Or do we assume vertices are indexed 0..N-1?
    // The input 'vertices' is a list of unique vertices (usually), or raw list? 
    // GeomCreate functions return 'outVertices' and 'outIndices'. 
    // 'outIndices' refer to 'outVertices'.
    
    // Actually, GeomCreate functions generate vertices and indices.
    // Ideally we just add them. 
    // BUT, some generators (like hexsphere or low poly) might duplicate vertices for flat shading or seam texturing?
    // OpenMesh needs shared vertices for connectivity. 
    // So we MUST weld vertices based on position if we want agents to move across faces smoothly.
    
    indexToHandle.resize(vertices.size());
    
    for(size_t i = 0; i < vertices.size(); ++i) {
        const auto& v = vertices[i];
        VertexKey key;
        key.x = static_cast<long long>(std::round(v.position.x * quant));
        key.y = static_cast<long long>(std::round(v.position.y * quant));
        key.z = static_cast<long long>(std::round(v.position.z * quant));
        
        auto it = uniqueVertices.find(key);
        if (it != uniqueVertices.end()) {
            indexToHandle[i] = it->second;
        } else {
            // Note: VertexData uses glm::vec3, Vertex uses glm::vec4. 
            MyMesh::VertexHandle vh = m_mesh.add_vertex(MyMesh::Point(v.position.x, v.position.y, v.position.z));
            uniqueVertices[key] = vh;
            indexToHandle[i] = vh;
        }
    }
    
    // 2. Add Faces
    for(size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size()) break;
        uint32_t idx0 = indices[i];
        uint32_t idx1 = indices[i+1];
        uint32_t idx2 = indices[i+2];
        
        std::vector<MyMesh::VertexHandle> face_vhandles;
        auto vh0 = indexToHandle[idx0];
        auto vh1 = indexToHandle[idx1];
        auto vh2 = indexToHandle[idx2];

        // Skip degenerate triangles (duplicate vertices after welding)
        if (vh0 == vh1 || vh1 == vh2 || vh2 == vh0) continue;

        face_vhandles.push_back(vh0);
        face_vhandles.push_back(vh1);
        face_vhandles.push_back(vh2);
        auto fh = m_mesh.add_face(face_vhandles);
        if (!fh.is_valid()) {
            qDebug() << "Skipped invalid face (maybe non-manifold or duplicate edge)" << idx0 << idx1 << idx2;
        }
    }
    
    m_mesh.request_face_normals();
    m_mesh.request_vertex_normals();
    m_mesh.update_normals();
    
    // 3. Build Rendering Buffer & R-Tree
    m_rtree.clear();
    m_vertices.reserve(m_mesh.n_faces() * 3);
    
    for (auto f_it = m_mesh.faces_begin(); f_it != m_mesh.faces_end(); ++f_it) {
        auto fv_it = m_mesh.fv_iter(*f_it);
        auto p0 = m_mesh.point(*fv_it);
        auto p1 = m_mesh.point(*(++fv_it));
        auto p2 = m_mesh.point(*(++fv_it));
        
        glm::vec3 v0(p0[0], p0[1], p0[2]);
        glm::vec3 v1(p1[0], p1[1], p1[2]);
        glm::vec3 v2(p2[0], p2[1], p2[2]);
        
            glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0)); // Flat shading normal
            if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) continue;
        
        m_vertices.push_back({v0, n});
        m_vertices.push_back({v1, n});
        m_vertices.push_back({v2, n});
        
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
    qDebug() << "updateMesh finished. Generated" << m_vertexCount << "render vertices.";
    
    // Update VBO
    QOpenGLContext* ctx = context();
    const bool needsMakeCurrent = ctx && QOpenGLContext::currentContext() != ctx;
    if (needsMakeCurrent) {
        makeCurrent();
    }

    if (QOpenGLContext::currentContext() == ctx) {
        m_vbo.bind();
        m_vbo.allocate(m_vertices.data(), m_vertices.size() * sizeof(VertexData));
        m_vbo.release();
    } else {
        qWarning() << "updateMesh: no current GL context, skipping VBO upload";
    }

    if (needsMakeCurrent) {
        doneCurrent();
    }
    
    update();
}

void MeshViewerWidget::buildSphere()
{
    // Generate HexSphere geometry
    std::vector<Vertex> rawVertices;
    std::vector<uint32_t> rawIndices;
    GeomCreate::createHexSphere(2, 1.0f, rawVertices, rawIndices);
    updateMesh(rawVertices, rawIndices);
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
            
            glm::vec3 faceNormal = glm::normalize(glm::cross(v1 - v0, v2 - v0));
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
                
                // Robust edge traversal: find the undirected edge and pick the opposite face even if winding differs.
                OpenMesh::HalfedgeHandle heForward = m_mesh.find_halfedge(va, vb);
                OpenMesh::HalfedgeHandle heBackward = m_mesh.find_halfedge(vb, va);
                OpenMesh::FaceHandle f0, f1;
                if (heForward.is_valid()) f0 = m_mesh.face_handle(heForward);
                if (heBackward.is_valid()) f1 = m_mesh.face_handle(heBackward);

                OpenMesh::FaceHandle nextFace;
                if (f0 == agent.face && f1.is_valid()) nextFace = f1;
                else if (f1 == agent.face && f0.is_valid()) nextFace = f0;
                else if (f0.is_valid() && f1.is_valid()) nextFace = (f0 == agent.face) ? f1 : f0;

                if (nextFace.is_valid()) {
                    agent.face = nextFace;

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
                    glm::vec3 wPos = v0 * agent.bary.x + v1 * agent.bary.y + v2 * agent.bary.z;
                    glm::vec3 P_v0 = wPos - nv0;
                    
                    float d00 = glm::dot(ne1, ne1);
                    float d01 = glm::dot(ne1, ne2);
                    float d11 = glm::dot(ne2, ne2);
                    float denom = d00 * d11 - d01 * d01;
                    if (std::abs(denom) < 1e-8f) denom = 1e-8f;
                    float id = 1.0f / denom;
                    float dP0 = glm::dot(P_v0, ne1);
                    float dP1 = glm::dot(P_v0, ne2);
                    float nu = (d11 * dP0 - d01 * dP1) * id;
                    float nv = (d00 * dP1 - d01 * dP0) * id;
                    float nw = 1.0f - nu - nv;
                    
                    agent.bary = glm::vec3(nw, nu, nv);
                    
                    // Rotate velocity across the hinge to preserve direction across the edge.
                    auto vaPos = m_mesh.point(va);
                    auto vbPos = m_mesh.point(vb);
                    glm::vec3 edgeDir = glm::normalize(glm::vec3(vbPos[0] - vaPos[0], vbPos[1] - vaPos[1], vbPos[2] - vaPos[2]));

                    glm::vec3 newNormal = glm::normalize(glm::cross(ne1, ne2));
                    float cosAng = std::clamp(glm::dot(faceNormal, newNormal), -1.0f, 1.0f);
                    float sign = glm::dot(edgeDir, glm::cross(faceNormal, newNormal)) >= 0.0f ? 1.0f : -1.0f;
                    float angle = std::acos(cosAng) * sign;

                    glm::vec3 rotatedVel = glm::vec3(glm::rotate(glm::mat4(1.0f), angle, edgeDir) * glm::vec4(agent.worldVelocity, 0.0f));
                    // Ensure tangential to the new face
                    glm::vec3 normal = newNormal;
                    glm::vec3 t = rotatedVel - glm::dot(rotatedVel, normal) * normal;
                    if (glm::dot(t, t) < 1e-8f) {
                        // Degenerate tangent; pick any perpendicular
                        t = glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f));
                        if (glm::dot(t, t) < 1e-8f) t = glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f));
                    }
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
    
    // Visibility: use proper clip-space frustum test so non-spherical meshes (e.g., cubes) are not culled incorrectly.
    auto isVisible = [&](const glm::vec3 &worldPos) -> bool {
        glm::vec4 clip = proj * mv * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0f) return false; // behind eye
        // Inside clip frustum
        return std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && clip.z >= -clip.w && clip.z <= clip.w;
    };

    auto project = [&](const glm::vec3 &worldPos) -> QVector2D {
        glm::vec3 p = glm::project(worldPos, mv, proj, viewport);
        return QVector2D(p.x, p.y);
    };

    auto computeNormal = [&](const SurfaceAgent& agentRef, const glm::vec3& fallbackPos) -> glm::vec3 {
        glm::vec3 normal = glm::normalize(fallbackPos); // fallback
        if (agentRef.face.is_valid()) {
            auto fv_norm_it = m_mesh.fv_iter(agentRef.face);
            auto np0 = m_mesh.normal(*fv_norm_it);
            auto np1 = m_mesh.normal(*(++fv_norm_it));
            auto np2 = m_mesh.normal(*(++fv_norm_it));
            glm::vec3 nv0(np0[0], np0[1], np0[2]);
            glm::vec3 nv1(np1[0], np1[1], np1[2]);
            glm::vec3 nv2(np2[0], np2[1], np2[2]);
            glm::vec3 bary = agentRef.bary;
            normal = nv0 * bary.x + nv1 * bary.y + nv2 * bary.z;
            if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) || glm::dot(normal, normal) < 1e-10f) {
                normal = glm::normalize(fallbackPos);
            }
        }
        // Rotate normal with the same model transform as the 3D view.
        glm::vec3 nWorld = glm::normalize(glm::mat3(model) * glm::normalize(normal));
        if (!std::isfinite(nWorld.x) || !std::isfinite(nWorld.y) || !std::isfinite(nWorld.z)) {
            nWorld = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        return nWorld;
    };

    auto lambert = [&](const glm::vec3 &normalWorld) -> float {
        float ndl = std::max(0.0f, glm::dot(glm::normalize(normalWorld), glm::normalize(m_lightDir)));
        // Match mesh.frag: ambient 0.2 + diffuse
        return std::clamp(0.2f + ndl, 0.0f, 1.0f);
    };

    for (const auto &agent : m_surfaceAgents) {
        glm::vec3 pos = getAgentWorldPos(agent);
        
        AgentRenderInfo info;
        info.screenPos = project(pos);
        info.color = agent.color;
        info.isVisible = isVisible(pos);
        info.layer = agent.layer;
        glm::vec3 normalWorld = computeNormal(agent, pos);
        info.headBrightness = lambert(normalWorld);
        
        std::vector<QVector2D> currentSegment;
        std::vector<float> currentBright;
        for (const auto &p : agent.trail) {
             if (isVisible(p)) {
                 currentSegment.push_back(project(p));
                 currentBright.push_back(lambert(computeNormal(agent, p)));
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
