#include "CgalMeshBuilder.h"

#include <array>
#include <unordered_set>
#include <cmath>
#include <algorithm>

#include <CGAL/centroid.h>
#include <CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/boost/graph/Euler_operations.h>
#include <CGAL/Subdivision_method_3/subdivision_methods_3.h>
#include <CGAL/Aff_transformation_3.h>

namespace PMP = CGAL::Polygon_mesh_processing;
namespace S3 = CGAL::Subdivision_method_3;

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

// Dualize a triangulated mesh to get hex/pentagons
void dualize_mesh(SM& sm) {
    SM dual_sm;
    std::vector<V> face_to_vert(sm.number_of_faces(), SM::null_vertex());

    for(auto f : sm.faces()) {
        if(f == SM::null_face() || sm.is_removed(f)) continue;
        auto h = sm.halfedge(f);
        double x=0,y=0,z=0; int c=0;
        for(auto v : sm.vertices_around_face(h)) {
            auto p = sm.point(v); x+=p.x(); y+=p.y(); z+=p.z(); c++;
        }
        if(c>0) {
             Point_3 cent(x/c, y/c, z/c);
             Vector_3 v(cent.x(), cent.y(), cent.z());
             double l = std::sqrt(v.squared_length());
             if(l>1e-9) cent = Point_3(v.x()/l, v.y()/l, v.z()/l);
             face_to_vert[f] = dual_sm.add_vertex(cent);
        }
    }

    for(auto v : sm.vertices()) {
        if(sm.is_removed(v)) continue;
        std::vector<V> ring;
        auto h_start = sm.halfedge(v);
        if(h_start == SM::null_halfedge()) continue;
        for(auto f : sm.faces_around_target(h_start)) {
            if(f != SM::null_face() && face_to_vert[f] != SM::null_vertex()) {
                ring.push_back(face_to_vert[f]);
            }
        }
        if(ring.size() >= 3) {
            dual_sm.add_face(ring);
        }
    }
    sm = dual_sm;
}
} // namespace

void CgalMeshBuilder::buildCube(SurfaceMesh& sm, double size) {
    sm.clear();
    const double h = 0.5 * size;
    V v0 = sm.add_vertex(Point_3(-h, -h, -h));
    V v1 = sm.add_vertex(Point_3( h, -h, -h));
    V v2 = sm.add_vertex(Point_3( h,  h, -h));
    V v3 = sm.add_vertex(Point_3(-h,  h, -h));
    V v4 = sm.add_vertex(Point_3(-h, -h,  h));
    V v5 = sm.add_vertex(Point_3( h, -h,  h));
    V v6 = sm.add_vertex(Point_3( h,  h,  h));
    V v7 = sm.add_vertex(Point_3(-h,  h,  h));

    sm.add_face(v0, v1, v5, v4);
    sm.add_face(v3, v2, v1, v0);
    sm.add_face(v4, v5, v6, v7);
    sm.add_face(v0, v4, v7, v3);
    sm.add_face(v1, v2, v6, v5);
    sm.add_face(v3, v7, v6, v2);
    sm.add_face(v0, v1, v5, v4);
}

void CgalMeshBuilder::buildHollowCuboid(SurfaceMesh& sm, int N, int M, int L, double cellSize) {
    sm.clear();
    if (N <= 0 || M <= 0 || L <= 0) return;
    const double totalW = N * cellSize;
    const double totalH = M * cellSize;
    const double totalD = L * cellSize;
    const Point_3 start(-totalW/2, -totalH/2, -totalD/2);

    auto idx = [&](int i, int j, int k) {
        return sm.add_vertex(start + Vector_3(i*cellSize, j*cellSize, k*cellSize));
    };

    std::vector<V> verts((N+1)*(M+1)*(L+1));
    for(int i=0; i<=N; ++i)
        for(int j=0; j<=M; ++j)
            for(int k=0; k<=L; ++k)
                verts[i*(M+1)*(L+1) + j*(L+1) + k] = idx(i,j,k);

    auto v = [&](int i, int j, int k) { return verts[i*(M+1)*(L+1) + j*(L+1) + k]; };

    for(int i=0; i<N; ++i) {
        for(int j=0; j<M; ++j) {
            for(int k=0; k<L; ++k) {
                if(j==0)   sm.add_face(v(i,0,k), v(i+1,0,k), v(i+1,0,k+1), v(i,0,k+1));
                if(j==M-1) sm.add_face(v(i,M,k+1), v(i+1,M,k+1), v(i+1,M,k), v(i,M,k));
                if(k==0)   sm.add_face(v(i,j,0), v(i,j+1,0), v(i+1,j+1,0), v(i+1,j,0));
                if(k==L-1) sm.add_face(v(i+1,j,L), v(i+1,j+1,L), v(i,j+1,L), v(i,j,L));
                if(i==0)   sm.add_face(v(0,j,k), v(0,j,k+1), v(0,j+1,k+1), v(0,j+1,k));
                if(i==N-1) sm.add_face(v(N,j+1,k), v(N,j+1,k+1), v(N,j,k+1), v(N,j,k));
            }
        }
    }
    remove_isolated_vertices_safe(sm);
}

void CgalMeshBuilder::buildPlaneXY(SurfaceMesh& sm, int N, int M, double cellSize) {
    sm.clear();
    const double W = N * cellSize;
    const double H = M * cellSize;
    Point_3 start(-W/2, -H/2, 0);
    std::vector<std::vector<V>> grid(N+1, std::vector<V>(M+1));
    for(int i=0; i<=N; ++i)
        for(int j=0; j<=M; ++j)
            grid[i][j] = sm.add_vertex(start + Vector_3(i*cellSize, j*cellSize, 0));

    for(int i=0; i<N; ++i)
        for(int j=0; j<M; ++j)
            sm.add_face(grid[i][j], grid[i+1][j], grid[i+1][j+1], grid[i][j+1]);
}

void CgalMeshBuilder::buildPlaneOriented(SurfaceMesh& sm, int N, int M, double cellSize, const Point_3& center, Vector_3 normal) {
    sm.clear();
    double len = std::sqrt(normal.squared_length());
    if(len < 1e-9) normal = Vector_3(0,0,1); else normal = normal/len;

    Vector_3 h = (std::abs(normal.x()) < 0.9) ? Vector_3(1,0,0) : Vector_3(0,1,0);
    Vector_3 u = CGAL::cross_product(h, normal);
    u = u / std::sqrt(u.squared_length());
    Vector_3 v = CGAL::cross_product(normal, u);

    const double W = N*cellSize, H = M*cellSize;
    Vector_3 off = (-0.5*W)*u + (-0.5*H)*v;

    std::vector<std::vector<V>> grid(N+1, std::vector<V>(M+1));
    for(int i=0; i<=N; ++i)
        for(int j=0; j<=M; ++j)
            grid[i][j] = sm.add_vertex(center + off + (double(i)*cellSize)*u + (double(j)*cellSize)*v);

    for(int i=0; i<N; ++i)
        for(int j=0; j<M; ++j)
            sm.add_face(grid[i][j], grid[i+1][j], grid[i+1][j+1], grid[i][j+1]);
}

void CgalMeshBuilder::buildCubeWithGrid(SurfaceMesh& sm, double size, int nx, int ny) {
    buildCube(sm, size);
    if (nx > 1 || ny > 1) {
        S3::Loop_subdivision(sm, CGAL::parameters::number_of_iterations(1));
    }
}

std::vector<F> CgalMeshBuilder::buildHexSphereOriented(
        SurfaceMesh& sm,
        int resolution,
        double radius,
        const Point_3& center,
        const Vector_3& axis_up,
        double seamRotate)
{
    sm.clear();
    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<Point_3> p = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1}
    };
    for(auto& pt : p) {
        Vector_3 v(pt.x(), pt.y(), pt.z());
        v = v / std::sqrt(v.squared_length());
        pt = Point_3(v.x(), v.y(), v.z());
    }

    std::vector<V> vh;
    vh.reserve(p.size());
    for(auto pt : p) vh.push_back(sm.add_vertex(pt));

    int tris[][3] = {
        {0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
        {1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
        {3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
        {4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1}
    };
    for(auto& tri : tris) sm.add_face(vh[tri[0]], vh[tri[1]], vh[tri[2]]);

    if(resolution > 0)
        S3::Loop_subdivision(sm, CGAL::parameters::number_of_iterations(resolution));

    dualize_mesh(sm);

    Vector_3 ax = axis_up;
    double l = std::sqrt(ax.squared_length());
    if(l>1e-9) ax = ax/l; else ax = Vector_3(0,1,0);

    Vector_3 rot_axis = CGAL::cross_product(Vector_3(0,1,0), ax);
    double sin_a = std::sqrt(rot_axis.squared_length());
    double cos_a = CGAL::scalar_product(Vector_3(0,1,0), ax);

    auto rotate_vec = [&](const Vector_3& v) -> Vector_3 {
         if (sin_a < 1e-9) {
             if (cos_a < 0) return Vector_3(v.x(), -v.y(), -v.z());
             return v;
         }
         rot_axis = rot_axis / sin_a;
         double angle = std::acos(std::max(-1.0, std::min(1.0, cos_a)));
         (void)angle;
         return v * cos_a + CGAL::cross_product(rot_axis, v) * sin_a + rot_axis * (CGAL::scalar_product(rot_axis, v)) * (1.0 - cos_a);
    };

    for(auto v : sm.vertices()) {
        Point_3 pt = sm.point(v);
        Vector_3 vec(pt.x(), pt.y(), pt.z());
        vec = rotate_vec(vec);
        if (std::abs(seamRotate) > 1e-9) {
             double sr_c = std::cos(seamRotate);
             double sr_s = std::sin(seamRotate);
             Vector_3 k = ax;
             vec = vec * sr_c + CGAL::cross_product(k, vec) * sr_s + k * (CGAL::scalar_product(k, vec)) * (1.0 - sr_c);
        }
        vec = vec * radius;
        sm.point(v) = center + vec;
    }

    std::vector<F> faces;
    faces.reserve(sm.number_of_faces());
    for(auto f : sm.faces()) faces.push_back(f);
    return faces;
}
