#pragma once

#include <vector>
#include <cstdint>

#include "CgalMeshTypes.h"

class CgalMeshBuilder {
public:
    using Kernel      = CgalMeshTypes::Kernel;
    using SurfaceMesh = CgalMeshTypes::SurfaceMesh;
    using V           = CgalMeshTypes::V;
    using F           = CgalMeshTypes::F;
    using Point_3     = CgalMeshTypes::Point_3;
    using Vector_3    = CgalMeshTypes::Vector_3;
    using Vertex      = CgalMeshTypes::Vertex;

    // --- Primitive Builders ---
    static void buildCube(SurfaceMesh& sm, double size);
    static void buildHollowCuboid(SurfaceMesh& sm, int N, int M, int L, double cellSize);
    static void buildPlaneXY(SurfaceMesh& sm, int N, int M, double cellSize);
    static void buildPlaneOriented(SurfaceMesh& sm, int N, int M, double cellSize, const Point_3& center, Vector_3 normal);
    static void buildCubeWithGrid(SurfaceMesh& sm, double size, int nx, int ny);

    // Hex/pent sphere
    static std::vector<F> buildHexSphereOriented(
        SurfaceMesh& sm,
        int resolution,
        double radius,
        const Point_3& center,
        const Vector_3& axis_up,
        double seamRotate = 0.0
    );

    // --- Selection / Deletion ---
    static std::vector<F> selectFacesRandom(const SurfaceMesh& sm, double p, std::uint32_t seed);
    static std::vector<F> selectFaceByIndex(const SurfaceMesh& sm, std::uint32_t index);
    static void deleteFaces(SurfaceMesh& sm, const std::vector<F>& faces, bool split_kissing = true);
    static void cleanup_after_deletions(SurfaceMesh& sm);

    // --- Extrusion ---
    static std::vector<F> extrudeFaces_collectBoth(
        SurfaceMesh& sm,
        const std::vector<F>& faces,
        double distance,
        double scale
    );

    // --- Verification / Export ---
    static bool checkMesh(SurfaceMesh& sm);
    static void triangulateAll(SurfaceMesh& sm);
    static void toVertexIndexFlat(const SurfaceMesh& sm, std::vector<Vertex>& outV, std::vector<std::uint32_t>& outI);
    static void buildHalfedgeArrows(const SurfaceMesh& sm, std::vector<Vertex>& outLines, float inset, float headSize, bool colorByBoundary = false);
};
