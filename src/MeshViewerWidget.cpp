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
#include <array>
#include <QDebug>
#include <QOpenGLContext>
#include <limits>
#include <algorithm>
#include <map>

static glm::vec3 randomTangentAroundNormal(const glm::vec3& nInput) {
    glm::vec3 n = glm::normalize(nInput);
    if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z) || glm::length(n) < 1e-6f) {
        n = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    glm::vec3 up = (std::abs(n.z) < 0.9f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 t1 = glm::normalize(glm::cross(n, up));
    if (glm::length(t1) < 1e-6f) t1 = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 t2 = glm::normalize(glm::cross(n, t1));
    float angle = static_cast<float>(QRandomGenerator::global()->generateDouble() * (2.0 * M_PI));
    glm::vec3 tangent = glm::normalize(std::cos(angle) * t1 + std::sin(angle) * t2);
    return tangent;
}

// We need boost geometry specifics here too if not fully in header
namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

namespace {
using SurfaceMesh = CgalMeshTypes::SurfaceMesh;
using FaceIndex = CgalMeshTypes::F;
using VertexIndex = CgalMeshTypes::V;
using Point_3 = CgalMeshTypes::Point_3;

inline glm::vec3 toGlm(const Point_3& p) {
    return glm::vec3(static_cast<float>(p.x()), static_cast<float>(p.y()), static_cast<float>(p.z()));
}

bool fetchFaceData(const SurfaceMesh& mesh, FaceIndex f, FaceData& out) {
    if (f == SurfaceMesh::null_face()) return false;
    auto h = mesh.halfedge(f);
    if (h == SurfaceMesh::null_halfedge()) return false;
    size_t i = 0;
    for (auto v : mesh.vertices_around_face(h)) {
        if (i >= 3) break;
        out.verts[i] = v;
        out.positions[i] = toGlm(mesh.point(v));
        ++i;
    }
    return i == 3;
}
} // namespace

const FaceCacheEntry* MeshViewerWidget::faceCache(FaceIndex f) const {
    if (f == SurfaceMesh::null_face()) return nullptr;
    int idx = f.idx();
    if (idx < 0 || idx >= static_cast<int>(m_faceCache.size())) return nullptr;
    const FaceCacheEntry& entry = m_faceCache[idx];
    return entry.valid ? &entry : nullptr;
}

bool MeshViewerWidget::getFaceData(FaceIndex f, FaceData& out) const {
    if (const FaceCacheEntry* c = faceCache(f)) {
        out.verts = c->verts;
        out.positions = c->positions;
        return true;
    }
    return fetchFaceData(m_mesh, f, out);
}

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
    float clamped = std::clamp(dist, 0.5f, 30.0f);
    if (std::abs(clamped - m_cameraDistance) < 1e-4f) return;
    m_cameraDistance = clamped;
    emit cameraDistanceChanged(m_cameraDistance);
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
    
    // Color (Loc 2)
    m_program.enableAttributeArray(2);
    m_program.setAttributeBuffer(2, GL_FLOAT, offsetof(VertexData, color), 3, sizeof(VertexData));
    
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
    int locAmbient = m_program.uniformLocation("u_ambient");
    if (locAmbient != -1) glUniform1f(locAmbient, m_ambient);
    int locDbg = m_program.uniformLocation("u_debugNormals");
    if (locDbg != -1) glUniform1i(locDbg, m_debugNormals ? 1 : 0);
    
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
    m_agentSystem.clear();
    m_faceNormals.clear();
    m_vertexNormals.clear();
    
    struct VertexKey {
        long long x, y, z;
        bool operator<(const VertexKey& o) const {
            if (x != o.x) return x < o.x;
            if (y != o.y) return y < o.y;
            return z < o.z;
        }
    };
    
    const float quant = 100000.0f;
    std::map<VertexKey, VertexIndex> uniqueVertices;
    std::vector<VertexIndex> indexToHandle(vertices.size());
    
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
            VertexIndex vh = m_mesh.add_vertex(Point_3(v.position.x, v.position.y, v.position.z));
            uniqueVertices[key] = vh;
            indexToHandle[i] = vh;
        }
    }
    
    std::vector<glm::vec3> faceColorsInput;
    faceColorsInput.reserve(indices.size() / 3);

    for(size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t idx0 = indices[i];
        uint32_t idx1 = indices[i+1];
        uint32_t idx2 = indices[i+2];
        
        VertexIndex vh0 = indexToHandle[idx0];
        VertexIndex vh1 = indexToHandle[idx1];
        VertexIndex vh2 = indexToHandle[idx2];

        if (vh0 == vh1 || vh1 == vh2 || vh2 == vh0) continue;

        FaceIndex fh = m_mesh.add_face(vh0, vh1, vh2);
        if (fh == SurfaceMesh::null_face()) {
            qDebug() << "Skipped invalid face (maybe non-manifold or duplicate edge)" << idx0 << idx1 << idx2;
            continue;
        }
        glm::vec3 c0(vertices[idx0].color.x, vertices[idx0].color.y, vertices[idx0].color.z);
        glm::vec3 c1(vertices[idx1].color.x, vertices[idx1].color.y, vertices[idx1].color.z);
        glm::vec3 c2(vertices[idx2].color.x, vertices[idx2].color.y, vertices[idx2].color.z);
        faceColorsInput.push_back((c0 + c1 + c2) / 3.0f);
    }
    
    m_rtree.clear();
    m_faceCache.assign(m_mesh.number_of_faces(), {});
    m_vertices.reserve(m_mesh.number_of_faces() * 3);
    m_faceNormals.assign(m_mesh.number_of_faces(), glm::vec3(0.0f));
    m_vertexNormals.assign(m_mesh.number_of_vertices(), glm::vec3(0.0f));

    // Determine whether the mesh is closed. For open meshes we keep the provided winding.
    bool hasBoundary = false;
    for (auto he : m_mesh.halfedges()) {
        if (m_mesh.face(he) == SurfaceMesh::null_face() || m_mesh.face(m_mesh.opposite(he)) == SurfaceMesh::null_face()) {
            hasBoundary = true;
            break;
        }
    }

    // Approximate mesh center in model space (used to orient normals outward for closed meshes).
    glm::vec3 meshCenter(0.0f);
    if (m_mesh.number_of_vertices() > 0) {
        for (auto v : m_mesh.vertices()) {
            meshCenter += toGlm(m_mesh.point(v));
        }
        meshCenter /= static_cast<float>(m_mesh.number_of_vertices());
    }
    
    for (auto f : m_mesh.faces()) {
        if (m_mesh.is_removed(f)) continue;
        FaceData fd;
        if (!getFaceData(f, fd)) continue;
        
        glm::vec3 v0 = fd.positions[0];
        glm::vec3 v1 = fd.positions[1];
        glm::vec3 v2 = fd.positions[2];
        
        glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
        if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) continue;

        // For closed meshes, flip normals to be outward-facing relative to the mesh center.
        if (!hasBoundary) {
            glm::vec3 c = (v0 + v1 + v2) * (1.0f / 3.0f);
            if (glm::dot(n, c - meshCenter) < 0.0f) n = -n;
        }
        
        m_faceNormals[f.idx()] = n;
        m_vertexNormals[fd.verts[0].idx()] += n;
        m_vertexNormals[fd.verts[1].idx()] += n;
        m_vertexNormals[fd.verts[2].idx()] += n;
        // Cache face geometry for fast lookups during agent simulation.
        if (f.idx() >= 0 && f.idx() < static_cast<int>(m_faceCache.size())) {
            auto& cache = m_faceCache[f.idx()];
            cache.verts = fd.verts;
            cache.positions = fd.positions;
            cache.normal = n;
            cache.valid = true;
        }
        
        glm::vec3 faceColor = (f.idx() < (int)faceColorsInput.size()) ? faceColorsInput[f.idx()] : glm::vec3(0.8f);

        m_vertices.push_back({v0, n, faceColor});
        m_vertices.push_back({v1, n, faceColor});
        m_vertices.push_back({v2, n, faceColor});
        
        float minX = std::min({v0.x, v1.x, v2.x});
        float minY = std::min({v0.y, v1.y, v2.y});
        float minZ = std::min({v0.z, v1.z, v2.z});
        float maxX = std::max({v0.x, v1.x, v2.x});
        float maxY = std::max({v0.y, v1.y, v2.y});
        float maxZ = std::max({v0.z, v1.z, v2.z});
        
        BoostBox box(BoostPoint(minX, minY, minZ), BoostPoint(maxX, maxY, maxZ));
        m_rtree.insert(std::make_pair(box, f.idx()));
    }
    
    for (auto& n : m_vertexNormals) {
        float len2 = glm::dot(n, n);
        if (len2 > 1e-12f) n = glm::normalize(n);
        else n = glm::vec3(0.0f, 1.0f, 0.0f);
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
    glm::mat4 invModel = glm::inverse(model);
    // Work in mesh/model space (same frame as the R-tree). Use camera as ray origin so we don't
    // accidentally start inside the mesh when zoomed in (near plane can clip through geometry).
    glm::vec3 cameraPosModel = glm::vec3(invModel * glm::vec4(0.0f, 0.0f, m_cameraDistance, 1.0f));
    
    float ndcX = (2.0f * x) / viewWidth - 1.0f;
    float ndcY = 1.0f - (2.0f * y) / viewHeight;
    
    glm::vec4 nearPoint = invMVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPoint = invMVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    
    glm::vec3 rayOrigWorld = cameraPosModel;
    glm::vec3 rayDirWorld = glm::normalize(glm::vec3(farPoint) - cameraPosModel);

    glm::vec3 rayEndWorld = rayOrigWorld + rayDirWorld * 100.0f;
    BoostSegment querySeg(
        BoostPoint(rayOrigWorld.x, rayOrigWorld.y, rayOrigWorld.z),
        BoostPoint(rayEndWorld.x, rayEndWorld.y, rayEndWorld.z)
    );
    std::vector<BoostValue> result;
    m_rtree.query(bgi::intersects(querySeg), std::back_inserter(result));
    
    bool hit = false;
    float minTFront = 1e30f;
    float minTAny = 1e30f;
    bool hitFront = false;
    
    SurfaceAgent newAgent;
    FaceIndex bestFaceFront = SurfaceMesh::null_face();
    glm::vec3 bestBaryFront(0.0f);
    glm::vec3 bestNFront(0.0f);

    FaceIndex bestFaceAny = SurfaceMesh::null_face();
    glm::vec3 bestBaryAny(0.0f);
    glm::vec3 bestNAny(0.0f);
    
    for (const auto &val : result) {
        int f_idx = val.second;
        FaceIndex f(f_idx);
        FaceData fd;
        if (!getFaceData(f, fd)) continue;
        glm::vec3 v0 = fd.positions[0];
        glm::vec3 v1 = fd.positions[1];
        glm::vec3 v2 = fd.positions[2];
        
        float t, u, v;
        if (rayTriangleIntersect(rayOrigWorld, rayDirWorld, v0, v1, v2, t, u, v)) {
            if (t <= 0.0f) continue;

            glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
            glm::vec3 pHit = rayOrigWorld + rayDirWorld * t;
            glm::vec3 vDir = glm::normalize(cameraPosModel - pHit);
            bool frontFacing = glm::dot(n, vDir) > 0.0f;

            if (t < minTAny) {
                minTAny = t;
                bestFaceAny = f;
                bestBaryAny = glm::vec3(1.0f - u - v, u, v);
                bestNAny = n;
            }
            if (frontFacing && t < minTFront) {
                minTFront = t;
                bestFaceFront = f;
                bestBaryFront = glm::vec3(1.0f - u - v, u, v);
                bestNFront = n;
                hitFront = true;
            }
        }
    }

    FaceIndex useFace = hitFront ? bestFaceFront : bestFaceAny;
    glm::vec3 useBary = hitFront ? bestBaryFront : bestBaryAny;
    glm::vec3 useN = hitFront ? bestNFront : bestNAny;
    float useT = hitFront ? minTFront : minTAny;

    if (useFace != SurfaceMesh::null_face()) {
        hit = true;
        newAgent.face = useFace;
        newAgent.bary = useBary;

        glm::vec3 tangent = randomTangentAroundNormal(useN);
        newAgent.speed = m_agentBaseSpeed; // configurable speed
        newAgent.worldVelocity = tangent * newAgent.speed;
        // Color from active palette (cycling if more agents than colors)
        const QVector<QVector<QVector3D>>* palettesPtr = m_externalPalettes ? m_externalPalettes : &m_palettes;
        const QVector<QVector3D>& palette = (*palettesPtr)[m_paletteIndex % palettesPtr->size()];
        int colorIdx = (int)m_agentSystem.count() % palette.size();
        const QVector3D& pal = palette[colorIdx];
        newAgent.color = QColor::fromRgbF(pal.x(), pal.y(), pal.z());
        newAgent.maxAge = m_agentLifetime;
        newAgent.age = 0;
        newAgent.layer = QRandomGenerator::global()->bounded(32); // stick to a fixed texture slice
    }

    if (hit) {
        m_agentSystem.add(newAgent);
        hitPos = rayOrigWorld + rayDirWorld * useT;
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
    m_agentSystem.removeIf([](const SurfaceAgent &a) { return a.age >= a.maxAge; });
    auto& agents = m_agentSystem.agents();
    
    if (agents.empty()) return;
    update();

    // Matrices shared across agents for visibility checks
    float viewWidth = std::max(1, width());
    float viewHeight = std::max(1, height());
    float aspect = viewWidth > 0 ? (float)viewWidth / (float)viewHeight : 1.0f;

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(m_rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(m_rotationY), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -m_cameraDistance));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    glm::mat4 mv = view * model;
    glm::mat4 invModel = glm::inverse(model);
    // Bring camera into mesh/model space so visibility tests use the same frame as the R-tree.
    glm::vec3 cameraPosModel = glm::vec3(invModel * glm::vec4(0.0f, 0.0f, m_cameraDistance, 1.0f));

    auto isClipVisible = [&](const glm::vec3 &worldPos) -> bool {
        glm::vec4 clip = proj * mv * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0f) return false;
        return std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && clip.z >= -clip.w && clip.z <= clip.w;
    };

    auto firstHitAlongRay = [&](const glm::vec3& worldPos) -> float {
        glm::vec3 dir = glm::normalize(worldPos - cameraPosModel);
        float targetT = glm::length(worldPos - cameraPosModel);
        if (targetT < 1e-5f) return 0.0f;
        BoostSegment querySeg(
            BoostPoint(cameraPosModel.x, cameraPosModel.y, cameraPosModel.z),
            BoostPoint(worldPos.x, worldPos.y, worldPos.z)
        );

        std::vector<BoostValue> result;
        m_rtree.query(bgi::intersects(querySeg), std::back_inserter(result));

        float bestT = std::numeric_limits<float>::max();
        for (const auto &val : result) {
            FaceIndex f(val.second);
            FaceData fd;
            if (!getFaceData(f, fd)) continue;
            float t, u, v;
            if (rayTriangleIntersect(cameraPosModel, dir, fd.positions[0], fd.positions[1], fd.positions[2], t, u, v)) {
                if (t > 0.0f && t < bestT) bestT = t;
            }
        }
        return bestT;
    };

    auto isVisible = [&](const SurfaceAgent&, const glm::vec3 &worldPos) -> bool {
        if (!isClipVisible(worldPos)) return false;
        float targetT = glm::length(worldPos - cameraPosModel);
        float hitT = firstHitAlongRay(worldPos);
        // visible if nothing hit before target (with small tolerance)
        return hitT == std::numeric_limits<float>::max() || targetT <= hitT + 1e-3f;
    };

    auto computeNormalModel = [&](const SurfaceAgent& agentRef, const glm::vec3& fallbackPos) -> glm::vec3 {
        glm::vec3 normal = glm::normalize(fallbackPos); // fallback
        if (agentRef.face != SurfaceMesh::null_face()) {
            FaceData fd;
            if (getFaceData(agentRef.face, fd)) {
                glm::vec3 nv0 = m_vertexNormals[fd.verts[0].idx()];
                glm::vec3 nv1 = m_vertexNormals[fd.verts[1].idx()];
                glm::vec3 nv2 = m_vertexNormals[fd.verts[2].idx()];
                glm::vec3 bary = agentRef.bary;
                normal = nv0 * bary.x + nv1 * bary.y + nv2 * bary.z;
                if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) || glm::dot(normal, normal) < 1e-10f) {
                    normal = glm::normalize(fallbackPos);
                }
            }
        }
        normal = glm::normalize(normal);
        if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) || glm::dot(normal, normal) < 1e-10f) {
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        return normal;
    };

    auto respawnAgent = [&](SurfaceAgent& agent) {
        bool placed = false;
        for (int attempt = 0; attempt < 32 && !placed; ++attempt) {
            float sx = QRandomGenerator::global()->bounded(viewWidth);
            float sy = QRandomGenerator::global()->bounded(viewHeight);

            float ndcX = (2.0f * sx) / viewWidth - 1.0f;
            float ndcY = 1.0f - (2.0f * sy) / viewHeight;
            glm::mat4 invMVP = glm::inverse(proj * mv);
            glm::vec4 nearPoint = invMVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
            glm::vec4 farPoint  = invMVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
            nearPoint /= nearPoint.w;
            farPoint  /= farPoint.w;
            // Use camera origin in mesh/model space so we always pick the true front-most hit
            // from the current view, even if the near plane clips into geometry.
            glm::vec3 rayOrigWorld = cameraPosModel;
            glm::vec3 rayDirWorld  = glm::normalize(glm::vec3(farPoint) - cameraPosModel);
            glm::vec3 rayEndWorld = rayOrigWorld + rayDirWorld * 100.0f;
            BoostSegment querySeg(
                BoostPoint(rayOrigWorld.x, rayOrigWorld.y, rayOrigWorld.z),
                BoostPoint(rayEndWorld.x, rayEndWorld.y, rayEndWorld.z)
            );

            std::vector<BoostValue> result;
            m_rtree.query(bgi::intersects(querySeg), std::back_inserter(result));

            float bestTFront = std::numeric_limits<float>::max();
            FaceIndex bestFaceFront = SurfaceMesh::null_face();
            glm::vec3 bestBaryFront(0.0f);

            float bestTAny = std::numeric_limits<float>::max();
            FaceIndex bestFaceAny = SurfaceMesh::null_face();
            glm::vec3 bestBaryAny(0.0f);

            for (const auto &val : result) {
                FaceIndex f(val.second);
                FaceData fd;
                if (!getFaceData(f, fd)) continue;
                glm::vec3 v0 = fd.positions[0];
                glm::vec3 v1 = fd.positions[1];
                glm::vec3 v2 = fd.positions[2];
                float t,u,v;
                if (rayTriangleIntersect(rayOrigWorld, rayDirWorld, v0, v1, v2, t, u, v)) {
                    if (t <= 0.0f) continue;
                    glm::vec3 n = glm::normalize(glm::cross(v1 - v0, v2 - v0));
                    glm::vec3 pHit = rayOrigWorld + rayDirWorld * t;
                    glm::vec3 vDir = glm::normalize(cameraPosModel - pHit);
                    bool frontFacing = glm::dot(n, vDir) > 0.0f;

                    if (t < bestTAny) {
                        bestTAny = t;
                        bestFaceAny = f;
                        bestBaryAny = glm::vec3(1.0f - u - v, u, v);
                    }
                    if (frontFacing && t < bestTFront) {
                        bestTFront = t;
                        bestFaceFront = f;
                        bestBaryFront = glm::vec3(1.0f - u - v, u, v);
                    }
                }
            }

            FaceIndex bestFace = (bestFaceFront != SurfaceMesh::null_face()) ? bestFaceFront : bestFaceAny;
            glm::vec3 bestBary = (bestFaceFront != SurfaceMesh::null_face()) ? bestBaryFront : bestBaryAny;

            if (bestFace != SurfaceMesh::null_face()) {
                agent.face = bestFace;
                agent.bary = bestBary;

                FaceData fd;
                if (!getFaceData(bestFace, fd)) continue;
                glm::vec3 n = glm::normalize(glm::cross(fd.positions[1] - fd.positions[0], fd.positions[2] - fd.positions[0]));
                glm::vec3 tangent = randomTangentAroundNormal(n);
                agent.speed = m_agentBaseSpeed;
                agent.worldVelocity = tangent * agent.speed;
                agent.trail.clear();
                agent.wasVisible = false;
                agent.invisibleTicks = 0;
                placed = true;
            }
        }
    };

    int agentId = 0;
    for (auto &agent : agents) {
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

            if (agent.face == SurfaceMesh::null_face()) {
                qDebug() << "Agent" << agentId << "has invalid face!";
                break;
            }

                FaceData fd;
                if (!getFaceData(agent.face, fd)) {
                    qDebug() << "Agent" << agentId << "degenerate face";
                    agent.face = SurfaceMesh::null_face();
                    break;
                }
            
            glm::vec3 v0 = fd.positions[0];
            glm::vec3 v1 = fd.positions[1];
            glm::vec3 v2 = fd.positions[2];
            
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
                 // Smooth per-point normal using vertex normals and barycentric weights.
                 glm::vec3 n0 = m_vertexNormals[fd.verts[0].idx()];
                 glm::vec3 n1 = m_vertexNormals[fd.verts[1].idx()];
                 glm::vec3 n2 = m_vertexNormals[fd.verts[2].idx()];
                 glm::vec3 currentNormal = glm::normalize(n0 * agent.bary.x + n1 * agent.bary.y + n2 * agent.bary.z);
                 if (!std::isfinite(currentNormal.x) || !std::isfinite(currentNormal.y) || !std::isfinite(currentNormal.z) || glm::dot(currentNormal, currentNormal) < 1e-10f) {
                     currentNormal = glm::normalize(faceNormal);
                 }
                 currentNormal = glm::normalize(glm::mat3(model) * currentNormal);
                 
                 bool shouldPush = false;
                 if (agent.trail.empty()) {
                     shouldPush = true;
                 } else {
                     glm::vec3 diff = agent.trail.back().pos - currentPos;
                     if (glm::dot(diff, diff) > 1e-6f) {
                         shouldPush = true;
                     }
                 }
                 
                 if (shouldPush) {
                      agent.trail.push_back({currentPos, currentNormal});
                      if (agent.trail.size() > 1000) agent.trail.pop_front();
                 }
            }

            remainingTime -= minT;
            
            if (hitIndex != -1) {
                VertexIndex va, vb;
                if (hitIndex == 0) { va = fd.verts[1]; vb = fd.verts[2]; } 
                if (hitIndex == 1) { va = fd.verts[2]; vb = fd.verts[0]; } 
                if (hitIndex == 2) { va = fd.verts[0]; vb = fd.verts[1]; } 
                
                SurfaceMesh::Face_index nextFace = SurfaceMesh::null_face();
                auto hStart = m_mesh.halfedge(agent.face);
                for (auto he : m_mesh.halfedges_around_face(hStart)) {
                    auto from = m_mesh.source(he);
                    auto to = m_mesh.target(he);
                    if ((from == va && to == vb) || (from == vb && to == va)) {
                        auto heUse = (from == va && to == vb) ? he : m_mesh.opposite(he);
                        nextFace = m_mesh.face(m_mesh.opposite(heUse));
                        break;
                    }
                }

                if (nextFace != SurfaceMesh::null_face() && !m_mesh.is_removed(nextFace)) {
                    agent.face = nextFace;

                    FaceData nd;
                    if (!getFaceData(agent.face, nd)) {
                        agent.face = SurfaceMesh::null_face();
                        break;
                    }
                    glm::vec3 nv0 = nd.positions[0];
                    glm::vec3 nv1 = nd.positions[1];
                    glm::vec3 nv2 = nd.positions[2];
                    
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
                    glm::vec3 edgeDir = glm::normalize(toGlm(vbPos) - toGlm(vaPos));

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

        // Visibility-driven behaviors: break trail and respawn if off-screen too long.
        glm::vec3 headPos = getAgentWorldPos(agent);
        glm::vec3 nModelHead = computeNormalModel(agent, headPos);
        glm::vec3 vDirHead = glm::normalize(cameraPosModel - headPos);
        bool headFrontFacing = glm::dot(nModelHead, vDirHead) > 0.0f;
        bool headVisible = headFrontFacing && isVisible(agent, headPos);
        if (!headVisible) {
            agent.invisibleTicks++;
            // Break trail (mark stop) so запись возобновится только после появления
            if (agent.wasVisible) {
                agent.trail.push_back({glm::vec3(std::numeric_limits<float>::quiet_NaN()),
                                       glm::vec3(std::numeric_limits<float>::quiet_NaN())});
            }
            // Продолжаем движение без добавления точек пока невидим
            if (agent.invisibleTicks > 50) {
                respawnAgent(agent);
                headPos = getAgentWorldPos(agent);
                nModelHead = computeNormalModel(agent, headPos);
                vDirHead = glm::normalize(cameraPosModel - headPos);
                headFrontFacing = glm::dot(nModelHead, vDirHead) > 0.0f;
                headVisible = headFrontFacing && isVisible(agent, headPos);
            }
        } else {
            agent.invisibleTicks = 0;
            // при появлении снова продолжаем добавление точек
        }
        agent.wasVisible = headVisible;
    }
}

glm::vec3 MeshViewerWidget::getAgentWorldPos(const SurfaceAgent &agent) {
    if (agent.face == SurfaceMesh::null_face()) return glm::vec3(0.0f);
    FaceData fd;
    if (!getFaceData(agent.face, fd)) return glm::vec3(0.0f);
    glm::vec3 v0 = fd.positions[0];
    glm::vec3 v1 = fd.positions[1];
    glm::vec3 v2 = fd.positions[2];
    return v0 * agent.bary.x + v1 * agent.bary.y + v2 * agent.bary.z;
}

std::vector<MeshViewerWidget::AgentRenderInfo> MeshViewerWidget::getProjectedAgents(float viewWidth, float viewHeight) {
    std::vector<AgentRenderInfo> projected;
    
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(m_rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(m_rotationY), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -m_cameraDistance));
    
    // Debug Instability
    if (!m_agentSystem.agents().empty()) {
       qDebug() << "getProjectedAgents: Rot" << m_rotationX << m_rotationY << "CamDist" << m_cameraDistance << "ViewSize" << viewWidth << viewHeight;
       qDebug() << "Agent[0] World" << getAgentWorldPos(m_agentSystem.agents()[0]).x << getAgentWorldPos(m_agentSystem.agents()[0]).y << getAgentWorldPos(m_agentSystem.agents()[0]).z;
    }

    // Use caller-provided viewport size to match AgentProjectionWindow
    float aspect = viewWidth > 0 ? (float)viewWidth / (float)viewHeight : 1.0f;
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    
    glm::mat4 mv = view * model;
    glm::vec4 viewport(0.0f, 0.0f, viewWidth, viewHeight);
    glm::mat4 invModel = glm::inverse(model);
    
    // Visibility: frustum + occlusion via rtree ray test.
    auto isClipVisible = [&](const glm::vec3 &worldPos) -> bool {
        glm::vec4 clip = proj * mv * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0f) return false; // behind eye
        return std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && clip.z >= -clip.w && clip.z <= clip.w;
    };

    // Match the frame used by the rtree (model space) to avoid false occlusion.
    glm::vec3 cameraPosModel = glm::vec3(invModel * glm::vec4(0.0f, 0.0f, m_cameraDistance, 1.0f));
    auto firstHitAlongRay = [&](const glm::vec3& worldPos) -> float {
        glm::vec3 dir = glm::normalize(worldPos - cameraPosModel);
        float targetT = glm::length(worldPos - cameraPosModel);
        if (targetT < 1e-5f) return 0.0f;
        BoostSegment querySeg(
            BoostPoint(cameraPosModel.x, cameraPosModel.y, cameraPosModel.z),
            BoostPoint(worldPos.x, worldPos.y, worldPos.z)
        );

        std::vector<BoostValue> result;
        m_rtree.query(bgi::intersects(querySeg), std::back_inserter(result));

        float bestT = std::numeric_limits<float>::max();
        for (const auto &val : result) {
            FaceIndex f(val.second);
            FaceData fd;
            if (!getFaceData(f, fd)) continue;
            float t, u, v;
            if (rayTriangleIntersect(cameraPosModel, dir, fd.positions[0], fd.positions[1], fd.positions[2], t, u, v)) {
                if (t > 0.0f && t < bestT) bestT = t;
            }
        }
        return bestT;
    };

    auto isVisible = [&](const glm::vec3 &worldPos) -> bool {
        if (!isClipVisible(worldPos)) return false;
        float targetT = glm::length(worldPos - cameraPosModel);
        float hitT = firstHitAlongRay(worldPos);
        return hitT == std::numeric_limits<float>::max() || targetT <= hitT + 1e-3f;
    };

    auto project = [&](const glm::vec3 &worldPos) -> QVector2D {
        glm::vec3 p = glm::project(worldPos, mv, proj, viewport);
        return QVector2D(p.x, p.y);
    };

    const float eps = 0.02f; // small offset in model units for orientation estimation

    auto computeNormal = [&](const SurfaceAgent& agentRef, const glm::vec3& fallbackPos) -> glm::vec3 {
        glm::vec3 normal = glm::normalize(fallbackPos); // fallback
        if (agentRef.face != SurfaceMesh::null_face()) {
            FaceData fd;
            if (getFaceData(agentRef.face, fd)) {
                glm::vec3 nv0 = m_vertexNormals[fd.verts[0].idx()];
                glm::vec3 nv1 = m_vertexNormals[fd.verts[1].idx()];
                glm::vec3 nv2 = m_vertexNormals[fd.verts[2].idx()];
                glm::vec3 bary = agentRef.bary;
                normal = nv0 * bary.x + nv1 * bary.y + nv2 * bary.z;
                if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) || glm::dot(normal, normal) < 1e-10f) {
                    normal = glm::normalize(fallbackPos);
                }
            }
        }
        // Rotate normal with the same model transform as the 3D view.
        glm::vec3 nWorld = glm::normalize(glm::mat3(model) * glm::normalize(normal));
        if (!std::isfinite(nWorld.x) || !std::isfinite(nWorld.y) || !std::isfinite(nWorld.z)) {
            nWorld = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        return nWorld;
    };

    auto computeNormalModel = [&](const SurfaceAgent& agentRef, const glm::vec3& fallbackPos) -> glm::vec3 {
        glm::vec3 normal = glm::normalize(fallbackPos); // fallback
        if (agentRef.face != SurfaceMesh::null_face()) {
            FaceData fd;
            if (getFaceData(agentRef.face, fd)) {
                glm::vec3 nv0 = m_vertexNormals[fd.verts[0].idx()];
                glm::vec3 nv1 = m_vertexNormals[fd.verts[1].idx()];
                glm::vec3 nv2 = m_vertexNormals[fd.verts[2].idx()];
                glm::vec3 bary = agentRef.bary;
                normal = nv0 * bary.x + nv1 * bary.y + nv2 * bary.z;
                if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) || glm::dot(normal, normal) < 1e-10f) {
                    normal = glm::normalize(fallbackPos);
                }
            }
        }
        normal = glm::normalize(normal);
        if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z) || glm::dot(normal, normal) < 1e-10f) {
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        return normal;
    };

    auto lambert = [&](const glm::vec3 &normalWorld) -> float {
        float ndl = std::max(0.0f, glm::dot(glm::normalize(normalWorld), glm::normalize(m_lightDir)));
        return std::clamp(m_ambient + ndl, 0.0f, 2.0f);
    };

    for (const auto &agent : m_agentSystem.agents()) {
        glm::vec3 pos = getAgentWorldPos(agent);
        glm::vec4 viewPos4 = mv * glm::vec4(pos, 1.0f);
        
        AgentRenderInfo info;
        info.screenPos = project(pos);
        info.color = agent.color;
        info.viewDepth = viewPos4.z; // more negative = farther
        glm::vec3 normalWorld = computeNormal(agent, pos);
        // Heads: occlusion + front-facing to avoid drawing agents on back side.
        glm::vec3 nModelHead = computeNormalModel(agent, pos);
        glm::vec3 vDirHead = glm::normalize(cameraPosModel - pos);
        bool headFrontFacing = glm::dot(nModelHead, vDirHead) > 0.0f;
        info.isVisible = headFrontFacing && isVisible(pos);
        
        info.layer = agent.layer;
        info.headBrightness = lambert(normalWorld);
        // Foreshorten factor for brush footprint: 1 face-on, ~0 edge-on.
        {
            float cosNV = std::abs(glm::dot(nModelHead, vDirHead));
            info.headForeshorten = std::clamp(cosNV, 0.05f, 1.0f);
        }
        
        // Trails: projection-only (frustum check), no occlusion test per point (too expensive).
        // Also downsample long trail segments to keep UI responsive.
        constexpr int kMaxTrailPointsPerSegment = 256;
        auto isFinite3 = [](const glm::vec3& p) {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        };

        std::vector<std::pair<int,int>> ranges;
        ranges.reserve(16);
        int start = -1;
        for (int i = 0; i < (int)agent.trail.size(); ++i) {
            if (isFinite3(agent.trail[i].pos)) {
                if (start < 0) start = i;
            } else {
                if (start >= 0) ranges.push_back({start, i});
                start = -1;
            }
        }
        if (start >= 0) ranges.push_back({start, (int)agent.trail.size()});

        for (const auto& r : ranges) {
            int count = r.second - r.first;
            if (count < 2) continue;
            int stride = std::max(1, (count + kMaxTrailPointsPerSegment - 1) / kMaxTrailPointsPerSegment);

            std::vector<QVector2D> current2d;
            std::vector<float> currentBright;
            std::vector<float> currentForesh;
            std::vector<QVector2D> currentWidthDir;
            current2d.reserve((count + stride - 1) / stride);
            currentBright.reserve((count + stride - 1) / stride);
            currentForesh.reserve((count + stride - 1) / stride);
            currentWidthDir.reserve((count + stride - 1) / stride);

            auto flush = [&]() {
                if (current2d.size() > 1) {
                    info.trailSegments.push_back(current2d);
                    info.trailBrightness.push_back(currentBright);
                    info.trailForeshorten.push_back(currentForesh);
                    info.trailWidthDirs.push_back(currentWidthDir);
                }
                current2d.clear();
                currentBright.clear();
                currentForesh.clear();
                currentWidthDir.clear();
            };

            for (int i = r.first; i < r.second; i += stride) {
                const auto& sample = agent.trail[i];
                const glm::vec3& p = sample.pos;

                glm::vec3 nModel = computeNormalModel(agent, p);
                glm::vec3 vDir = glm::normalize(cameraPosModel - p);
                float ndv = glm::dot(nModel, vDir);
                bool frontFacing = ndv > 0.0f;
                if (!frontFacing || !isClipVisible(p)) {
                    flush();
                    continue;
                }

                glm::vec3 nWorld = sample.normal;
                if (!std::isfinite(nWorld.x) || !std::isfinite(nWorld.y) || !std::isfinite(nWorld.z) || glm::dot(nWorld, nWorld) < 1e-8f) {
                    nWorld = computeNormal(agent, p);
                }

                current2d.push_back(project(p));
                currentBright.push_back(lambert(nWorld));
                float cosNV = std::abs(ndv);
                currentForesh.push_back(std::clamp(cosNV, 0.05f, 1.0f));

                // Compute oriented "across" direction for brush footprint based on surface tangent plane.
                int prevIdx = std::max(r.first, i - stride);
                int nextIdx = std::min(r.second - 1, i + stride);
                glm::vec3 tangentModel = agent.trail[nextIdx].pos - agent.trail[prevIdx].pos;
                float tLen2 = glm::dot(tangentModel, tangentModel);
                if (tLen2 < 1e-10f || !std::isfinite(tangentModel.x) || !std::isfinite(tangentModel.y) || !std::isfinite(tangentModel.z)) {
                    tangentModel = glm::cross(nModel, glm::vec3(0.0f, 0.0f, 1.0f));
                    if (glm::dot(tangentModel, tangentModel) < 1e-10f)
                        tangentModel = glm::cross(nModel, glm::vec3(0.0f, 1.0f, 0.0f));
                }
                tangentModel = glm::normalize(tangentModel - glm::dot(tangentModel, nModel) * nModel);
                if (glm::dot(tangentModel, tangentModel) < 1e-10f) tangentModel = glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec3 bitangentModel = glm::normalize(glm::cross(nModel, tangentModel));
                if (!std::isfinite(bitangentModel.x) || !std::isfinite(bitangentModel.y) || !std::isfinite(bitangentModel.z) || glm::dot(bitangentModel, bitangentModel) < 1e-10f) {
                    bitangentModel = glm::normalize(glm::cross(nModel, glm::vec3(1.0f, 0.0f, 0.0f)));
                }
                QVector2D pPlus = project(p + bitangentModel * eps);
                QVector2D pMinus = project(p - bitangentModel * eps);
                QVector2D wdir = pPlus - pMinus;
                if (wdir.lengthSquared() < 1e-6f) {
                    QVector2D pt = project(p + tangentModel * eps) - project(p - tangentModel * eps);
                    if (pt.lengthSquared() < 1e-6f) pt = QVector2D(1.0f, 0.0f);
                    wdir = QVector2D(-pt.y(), pt.x());
                }
                wdir.normalize();
                currentWidthDir.push_back(wdir);
            }
            flush();
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

void MeshViewerWidget::wheelEvent(QWheelEvent *event) {
    // Use angleDelta to support high-resolution wheels; 120 units per notch.
    QPoint numDegrees = event->angleDelta() / 8;
    if (numDegrees.isNull()) return;
    int steps = numDegrees.y() / 15; // 15 deg per step (120 / 8)
    if (steps == 0) return;
    float factor = std::pow(0.9f, steps); // zoom in for positive steps
    setCameraDistance(m_cameraDistance * factor);
    event->accept();
}
