#pragma once

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>

// Common CGAL mesh aliases shared across mesh utilities.
struct CgalMeshTypes {
    using Kernel      = CGAL::Exact_predicates_inexact_constructions_kernel;
    using SurfaceMesh = CGAL::Surface_mesh<Kernel::Point_3>;
    using V           = SurfaceMesh::Vertex_index;
    using F           = SurfaceMesh::Face_index;
    using Point_3     = Kernel::Point_3;
    using Vector_3    = Kernel::Vector_3;

    struct Vertex {
        float pos[3];
        float norm[3];
        float col[3];
    };
};
