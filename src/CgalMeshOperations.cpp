#include "CgalMeshBuilder.h"

#include <random>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>

#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/stitch_borders.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/boost/graph/Euler_operations.h>

namespace PMP = CGAL::Polygon_mesh_processing;

using SM = CgalMeshBuilder::SurfaceMesh;
using V  = CgalMeshBuilder::V;
using F  = CgalMeshBuilder::F;
using Point_3 = CgalMeshBuilder::Point_3;
using Vector_3 = CgalMeshBuilder::Vector_3;

namespace {
inline void remove_isolated_vertices_safe(SM& sm) {
    std::vector<V> dead;
    for (auto v : sm.vertices())
        if (sm.halfedge(v) == SM::null_halfedge())
            dead.push_back(v);
    for (auto v : dead) sm.remove_vertex(v);
}

static inline Point_3 centroid_points(const std::vector<Point_3>& R){
    double x=0,y=0,z=0; for (auto& p: R){ x+=p.x(); y+=p.y(); z+=p.z(); }
    const double inv = (R.empty()? 0.0 : 1.0/double(R.size()));
    return Point_3(x*inv,y*inv,z*inv);
}

static Vector_3 newell_normal(const std::vector<Point_3>& P){
    Vector_3 n(0,0,0); const size_t m=P.size();
    for (size_t i=0;i<m;++i){
        const auto& a=P[i]; const auto& b=P[(i+1)%m];
        n = n + Vector_3( (a.y()-b.y())*(a.z()+b.z()),
                         (a.z()-b.z())*(a.x()+b.x()),
                         (a.x()-b.x())*(a.y()+b.y()) );
    }
    const double L = std::sqrt(n.squared_length());
    return (L>1e-12)? n/L : Vector_3(0,0,1);
}

static inline Point_3 mesh_centroid(const SM& sm){
    double x=0,y=0,z=0; std::size_t n=0;
    for (auto v : sm.vertices()){ const auto& p=sm.point(v); x+=p.x(); y+=p.y(); z+=p.z(); ++n; }
    if (!n) return Point_3(0,0,0);
    const double inv = 1.0/double(n); return Point_3(x*inv,y*inv,z*inv);
}
} // namespace

std::vector<F> CgalMeshBuilder::selectFacesRandom(const SurfaceMesh& sm, double p, std::uint32_t seed) {
    std::vector<F> out;
    if (p <= 0.0) return out;
    if (p >= 1.0) {
        for (auto f : sm.faces()) if (!sm.is_removed(f)) out.push_back(f);
        return out;
    }
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    for (auto f : sm.faces()) {
        if (!sm.is_removed(f) && U(rng) < p) out.push_back(f);
    }
    return out;
}

std::vector<F> CgalMeshBuilder::selectFaceByIndex(const SurfaceMesh& sm, std::uint32_t index) {
    std::vector<F> out;
    std::uint32_t c = 0;
    for (auto f : sm.faces()) {
        if (!sm.is_removed(f)) {
            if (c == index) {
                out.push_back(f);
                break;
            }
            c++;
        }
    }
    return out;
}

void CgalMeshBuilder::deleteFaces(SurfaceMesh& sm, const std::vector<F>& faces, bool split_kissing) {
    for (auto f : faces) {
        if (f != SM::null_face() && !sm.is_removed(f)) {
             CGAL::Euler::remove_face(sm.halfedge(f), sm);
        }
    }
    if(split_kissing) {
        // Placeholder for duplicate_non_manifold_vertices on newer CGAL
    }
    sm.collect_garbage();
}

void CgalMeshBuilder::cleanup_after_deletions(SurfaceMesh& sm) {
    sm.collect_garbage();
    PMP::stitch_borders(sm);
    remove_isolated_vertices_safe(sm);
    sm.collect_garbage();
}

std::vector<F> CgalMeshBuilder::extrudeFaces_collectBoth(
    SurfaceMesh& sm,
    const std::vector<F>& faces,
    double distance,
    double scale)
{
    std::vector<F> newFaces;
    std::vector<F> valid;
    for(auto f : faces) if(f!=SM::null_face() && !sm.is_removed(f)) valid.push_back(f);

    const Point_3 MC = mesh_centroid(sm);
    (void)MC;

    for(auto f : valid) {
         std::vector<V> ring;
         auto h0 = sm.halfedge(f);
         auto h = h0;
         do { ring.push_back(sm.target(h)); h = sm.next(h); } while(h!=h0);

         std::vector<Point_3> ringPoints;
         ringPoints.reserve(ring.size());
         for (auto v : ring) ringPoints.push_back(sm.point(v));
         Point_3 C = centroid_points(ringPoints);
         Vector_3 n = newell_normal(ringPoints);

         std::vector<V> topV;
         for(auto v : ring) {
             Vector_3 d = sm.point(v) - C;
             Point_3 p = C + d*scale + n*distance;
             topV.push_back(sm.add_vertex(p));
         }

         for(size_t i=0; i<ring.size(); ++i) {
             size_t j = (i+1)%ring.size();
             F w = sm.add_face(ring[i], ring[j], topV[j], topV[i]);
             if(w!=SM::null_face()) newFaces.push_back(w);
         }

         F cap = sm.add_face(topV);
         if(cap!=SM::null_face()) newFaces.push_back(cap);

         sm.remove_face(f);
    }
    return newFaces;
}

bool CgalMeshBuilder::checkMesh(SurfaceMesh& sm) {
    return CGAL::is_valid_polygon_mesh(sm);
}
