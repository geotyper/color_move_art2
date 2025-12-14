#include "MeshRepository.h"
#include <glm/glm.hpp>
#include <CGAL/boost/graph/helpers.h>
#include <cmath>

namespace {
using SM = CgalMeshBuilder::SurfaceMesh;
using V = SM::Vertex_index;
using F = SM::Face_index;

inline glm::vec3 toGlm(const SM::Point& p) {
    return glm::vec3(static_cast<float>(p.x()),
                     static_cast<float>(p.y()),
                     static_cast<float>(p.z()));
}

Vertex makeVertex(const glm::vec3& pos, const glm::vec3& normal, const QColor& color) {
    Vertex v;
    v.position = glm::vec4(pos, 1.0f);
    v.normal   = glm::vec4(normal, 0.0f);
    v.color    = glm::vec4(color.redF(), color.greenF(), color.blueF(), 1.0f);
    return v;
}
} // namespace

int MeshRepository::addMesh(const SM& mesh, const QColor& color) {
    MeshEntry entry;
    entry.polyMesh = mesh;
    entry.meshColor = color;
    entry.faceColors.resize(mesh.number_of_faces(), color);
    rebuildRenderData(entry);
    m_meshes.push_back(std::move(entry));
    m_activeIndex = static_cast<int>(m_meshes.size()) - 1;
    return m_activeIndex;
}

MeshRepository::MeshEntry* MeshRepository::active() {
    if (m_activeIndex < 0 || m_activeIndex >= static_cast<int>(m_meshes.size())) return nullptr;
    return &m_meshes[m_activeIndex];
}

const MeshRepository::MeshEntry* MeshRepository::active() const {
    if (m_activeIndex < 0 || m_activeIndex >= static_cast<int>(m_meshes.size())) return nullptr;
    return &m_meshes[m_activeIndex];
}

void MeshRepository::setActiveIndex(int idx) {
    if (idx >= 0 && idx < static_cast<int>(m_meshes.size())) {
        m_activeIndex = idx;
    }
}

void MeshRepository::clear() {
    m_meshes.clear();
    m_activeIndex = -1;
}

MeshRepository::MeshEntry* MeshRepository::get(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_meshes.size())) return nullptr;
    return &m_meshes[idx];
}

void MeshRepository::setMeshColor(MeshEntry& entry, const QColor& color) {
    entry.meshColor = color;
    if (entry.faceColors.empty()) {
        entry.faceColors.resize(entry.polyMesh.number_of_faces(), color);
    }
}

void MeshRepository::setFaceColor(MeshEntry& entry, F f, const QColor& color) {
    if (f == SM::null_face()) return;
    if (entry.faceColors.size() < entry.polyMesh.number_of_faces()) {
        entry.faceColors.resize(entry.polyMesh.number_of_faces(), entry.meshColor);
    }
    if (static_cast<std::size_t>(f.idx()) < entry.faceColors.size()) {
        entry.faceColors[f.idx()] = color;
    }
}

void MeshRepository::rebuildRenderData(MeshEntry& entry) {
    SM& sm = entry.polyMesh;
    entry.renderVertices.clear();
    entry.renderIndices.clear();

    if (entry.faceColors.size() < sm.number_of_faces()) {
        entry.faceColors.resize(sm.number_of_faces(), entry.meshColor);
    }

    std::uint32_t baseIndex = 0;
    for (auto f : sm.faces()) {
        if (sm.is_removed(f)) continue;
        std::vector<V> loop;
        loop.reserve(8);
        for (auto v : CGAL::vertices_around_face(sm.halfedge(f), sm)) {
            loop.push_back(v);
        }
        if (loop.size() < 3) continue;

        QColor faceColor = entry.meshColor;
        if (static_cast<std::size_t>(f.idx()) < entry.faceColors.size()) {
            faceColor = entry.faceColors[f.idx()];
        }
        glm::vec3 p0 = toGlm(sm.point(loop[0]));
        glm::vec3 p1 = toGlm(sm.point(loop[1]));
        glm::vec3 p2 = toGlm(sm.point(loop[2]));
        glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z)) {
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        for (std::size_t i = 1; i + 1 < loop.size(); ++i) {
            glm::vec3 a = toGlm(sm.point(loop[0]));
            glm::vec3 b = toGlm(sm.point(loop[i]));
            glm::vec3 c = toGlm(sm.point(loop[i + 1]));

            // Recompute normal per triangle for robustness
            glm::vec3 nTri = glm::normalize(glm::cross(b - a, c - a));
            if (!std::isfinite(nTri.x) || glm::length(nTri) < 1e-6f) {
                nTri = normal;
            }

            entry.renderVertices.push_back(makeVertex(a, nTri, faceColor));
            entry.renderVertices.push_back(makeVertex(b, nTri, faceColor));
            entry.renderVertices.push_back(makeVertex(c, nTri, faceColor));

            entry.renderIndices.push_back(baseIndex);
            entry.renderIndices.push_back(baseIndex + 1);
            entry.renderIndices.push_back(baseIndex + 2);
            baseIndex += 3;
        }
    }
}

void MeshRepository::buildFlattened(std::vector<Vertex>& outVerts, std::vector<std::uint32_t>& outIndices) const {
    outVerts.clear();
    outIndices.clear();
    std::uint32_t offset = 0;
    for (const auto& m : m_meshes) {
        outVerts.insert(outVerts.end(), m.renderVertices.begin(), m.renderVertices.end());
        for (auto idx : m.renderIndices) {
            outIndices.push_back(offset + idx);
        }
        offset += static_cast<std::uint32_t>(m.renderVertices.size());
    }
}
