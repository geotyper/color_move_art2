#include "CgalMeshBuilder.h"

#include <CGAL/Polygon_mesh_processing/triangulate_faces.h>

namespace PMP = CGAL::Polygon_mesh_processing;

using SurfaceMesh = CgalMeshBuilder::SurfaceMesh;
using V  = CgalMeshBuilder::V;
using F  = CgalMeshBuilder::F;
using Vector_3 = CgalMeshBuilder::Vector_3;
using Point_3 = CgalMeshBuilder::Point_3;

void CgalMeshBuilder::triangulateAll(SurfaceMesh& sm) {
     PMP::triangulate_faces(sm);
}

void CgalMeshBuilder::toVertexIndexFlat(const SurfaceMesh& sm, std::vector<Vertex>& outV, std::vector<std::uint32_t>& outI) {
    outV.clear(); outI.clear();
    for(auto f : sm.faces()) {
        if(sm.is_removed(f)) continue;
        auto h = sm.halfedge(f);
        V v0 = sm.target(h); h = sm.next(h);
        V v1 = sm.target(h); h = sm.next(h);
        V v2 = sm.target(h);

        uint32_t idx = (uint32_t)outV.size();

        auto pushV = [&](V v, const Vector_3& n) {
             auto p = sm.point(v);
             outV.push_back({{float(p.x()), float(p.y()), float(p.z())},
                             {float(n.x()), float(n.y()), float(n.z())},
                             {1,1,1}});
        };
        Point_3 p0 = sm.point(v0), p1 = sm.point(v1), p2 = sm.point(v2);
        Vector_3 n = CGAL::cross_product(p1-p0, p2-p0);
        double len = std::sqrt(n.squared_length());
        if (len > 1e-12) n = n / len; else n = Vector_3(0,1,0);

        pushV(v0, n); pushV(v1, n); pushV(v2, n);
        outI.push_back(idx); outI.push_back(idx+1); outI.push_back(idx+2);
    }
}

void CgalMeshBuilder::buildHalfedgeArrows(const SurfaceMesh& sm, std::vector<Vertex>& outLines, float inset, float headSize, bool colorByBoundary) {
    (void)sm; (void)outLines; (void)inset; (void)headSize; (void)colorByBoundary;
    // Debug visualizer placeholder
}
