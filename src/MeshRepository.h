#pragma once

#include <QColor>
#include <vector>
#include "CgalMeshBuilder.h"
#include "HelpStructures.h"

// MeshRepository stores polygonal meshes along with per-mesh and per-face colors
// and produces triangulated render buffers with per-triangle coloring.
class MeshRepository {
public:
    struct MeshEntry {
        CgalMeshBuilder::SurfaceMesh polyMesh;
        QColor meshColor = Qt::white;
        std::vector<QColor> faceColors;
        std::vector<Vertex> renderVertices;
        std::vector<std::uint32_t> renderIndices;
    };

    int addMesh(const CgalMeshBuilder::SurfaceMesh& mesh, const QColor& color = Qt::white);
    MeshEntry* active();
    const MeshEntry* active() const;
    void setActiveIndex(int idx);
    int activeIndex() const { return m_activeIndex; }
    int size() const { return static_cast<int>(m_meshes.size()); }
    void clear();
    MeshEntry* get(int idx);
    const std::vector<MeshEntry>& meshes() const { return m_meshes; }

    // Coloring helpers
    void setMeshColor(MeshEntry& entry, const QColor& color);
    void setFaceColor(MeshEntry& entry, CgalMeshBuilder::SurfaceMesh::Face_index f, const QColor& color);

    // Build renderVertices/renderIndices using a fan triangulation per polygon face.
    void rebuildRenderData(MeshEntry& entry);
    // Flatten all meshes into single buffers for rendering.
    void buildFlattened(std::vector<Vertex>& outVerts, std::vector<std::uint32_t>& outIndices) const;

private:
    std::vector<MeshEntry> m_meshes;
    int m_activeIndex = -1;
};

