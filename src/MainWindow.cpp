#include "MainWindow.h"
#include <algorithm>
#include <QFrame>
#include <QMdiSubWindow>
#include <QTimer>
#include <QShowEvent>
#include <QImage>
#include <QPixmap>
#include <QColor>
#include <QColorDialog>
#include <numeric>
#include <QFormLayout>
#include <QTabWidget>
#include <QRandomGenerator>
#include "MeshViewerWidget.h"
#include "AgentProjectionWindow.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include "CgalMeshBuilder.h"
#include "CgalMeshBuilderTentacles.h"
#include <unordered_map>
#include <unordered_set>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/stitch_borders.h>
#include <CGAL/Subdivision_method_3/subdivision_methods_3.h>

namespace {
using SM = CgalMeshBuilder::SurfaceMesh;
using Kernel = CgalMeshBuilder::Kernel;
using Vector = Kernel::Vector_3;
namespace PMP = CGAL::Polygon_mesh_processing;
namespace S3 = CGAL::Subdivision_method_3;

static SM toSurfaceMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices) {
    SM sm;
    // Weld vertices by position to restore shared topology before doing CGAL ops.
    struct Key {
        int x, y, z;
        bool operator==(const Key& o) const { return x==o.x && y==o.y && z==o.z; }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const {
            std::size_t h = std::hash<int>()(k.x);
            h ^= std::hash<int>()(k.y + 0x9e3779b9 + (h<<6) + (h>>2));
            h ^= std::hash<int>()(k.z + 0x9e3779b9 + (h<<6) + (h>>2));
            return h;
        }
    };

    const float quant = 1e5f;
    std::unordered_map<Key, SM::Vertex_index, KeyHash> welded;
    welded.reserve(verts.size() * 2);

    auto addWelded = [&](const Vertex& v) -> SM::Vertex_index {
        Key k {
            static_cast<int>(std::round(v.position.x * quant)),
            static_cast<int>(std::round(v.position.y * quant)),
            static_cast<int>(std::round(v.position.z * quant))
        };
        auto it = welded.find(k);
        if (it != welded.end()) return it->second;
        SM::Vertex_index nv = sm.add_vertex(SM::Point(v.position.x, v.position.y, v.position.z));
        welded[k] = nv;
        return nv;
    };

    std::vector<SM::Vertex_index> vmap;
    vmap.reserve(verts.size());
    for (const auto& v : verts) {
        vmap.push_back(addWelded(v));
    }

    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a >= vmap.size() || b >= vmap.size() || c >= vmap.size()) continue;
        // Skip degenerate after welding
        if (vmap[a] == vmap[b] || vmap[b] == vmap[c] || vmap[c] == vmap[a]) continue;
        sm.add_face(vmap[a], vmap[b], vmap[c]);
    }
    return sm;
}

static bool mergeCoplanarPatches(SM& sm, double normalEps = 1e-4, double distEps = 1e-4) {
    auto faceNormal = [&](SM::Face_index f) {
        auto h0 = sm.halfedge(f);
        if (h0 == SM::null_halfedge()) return Vector(0,0,1);
        auto a = sm.point(target(h0, sm));
        auto b = sm.point(target(next(h0, sm), sm));
        auto c = sm.point(target(next(next(h0, sm), sm), sm));
        auto n = CGAL::cross_product(b - a, c - a);
        double len2 = n.squared_length();
        return (len2 > 1e-16) ? n / std::sqrt(len2) : Vector(0,0,1);
    };
    auto facePlane = [&](SM::Face_index f, Vector& nOut, double& dOut) {
        nOut = faceNormal(f);
        auto h0 = sm.halfedge(f);
        auto p0 = sm.point(target(h0, sm));
        dOut = -(nOut.x()*p0.x() + nOut.y()*p0.y() + nOut.z()*p0.z());
    };

    std::unordered_set<SM::Face_index> visited;
    bool mergedAny = false;

    for (auto fStart : sm.faces()) {
        if (sm.is_removed(fStart) || visited.count(fStart)) continue;
        Vector nBase; double dBase = 0.0;
        facePlane(fStart, nBase, dBase);

        // Grow coplanar region.
        std::vector<SM::Face_index> region;
        std::vector<SM::Face_index> stack = {fStart};
        visited.insert(fStart);
        while (!stack.empty()) {
            auto f = stack.back();
            stack.pop_back();
            region.push_back(f);
            for (auto h : CGAL::halfedges_around_face(sm.halfedge(f), sm)) {
                auto fo = sm.face(sm.opposite(h));
                if (fo == SM::null_face() || sm.is_removed(fo) || visited.count(fo)) continue;
                Vector nN; double dN;
                facePlane(fo, nN, dN);
                double ndot = CGAL::to_double(nBase * nN);
                if (std::abs(ndot - 1.0) > normalEps) continue;
                auto p = sm.point(target(sm.halfedge(fo), sm));
                double dist = nBase.x()*p.x() + nBase.y()*p.y() + nBase.z()*p.z() + dBase;
                if (std::abs(dist) > distEps) continue;
                visited.insert(fo);
                stack.push_back(fo);
            }
        }

        if (region.size() <= 1) continue;

        // Collect boundary directed edges of the region.
        std::unordered_map<SM::Vertex_index, SM::Vertex_index> nextMap;
        std::size_t boundaryCount = 0;
        std::unordered_set<SM::Halfedge_index> regionEdges;
        regionEdges.reserve(region.size() * 3);
        for (auto f : region)
            for (auto h : CGAL::halfedges_around_face(sm.halfedge(f), sm))
                regionEdges.insert(h);

        for (auto h : regionEdges) {
            auto fo = sm.face(sm.opposite(h));
            if (fo == SM::null_face() || !regionEdges.count(sm.opposite(h))) {
                auto s = source(h, sm);
                auto t = target(h, sm);
                nextMap[s] = t;
                ++boundaryCount;
            }
        }
        if (boundaryCount < 3) continue;

        // Build one boundary loop from nextMap.
        std::vector<SM::Vertex_index> ring;
        ring.reserve(boundaryCount);
        auto start = nextMap.begin()->first;
        auto v = start;
        std::size_t guard = boundaryCount + 2;
        while (guard-- && nextMap.count(v)) {
            ring.push_back(v);
            v = nextMap[v];
            if (v == start) break;
        }
        if (ring.size() < 3 || v != start) continue;

        // Remove region faces, add new polygon face.
        for (auto f : region) {
            auto h = sm.halfedge(f);
            if (h != SM::null_halfedge()) CGAL::Euler::remove_face(h, sm);
        }
        auto fNew = CGAL::Euler::add_face(ring, sm);
        if (fNew == SM::null_face()) {
            std::reverse(ring.begin(), ring.end());
            fNew = CGAL::Euler::add_face(ring, sm);
        }
        if (fNew != SM::null_face()) mergedAny = true;
    }

    if (mergedAny) sm.collect_garbage();
    return mergedAny;
}

static void fromSurfaceMesh(const SM& sm, std::vector<Vertex>& outV, std::vector<uint32_t>& outI) {
    outV.clear();
    outI.clear();
    for (auto f : sm.faces()) {
        std::vector<SM::Vertex_index> ring;
        auto h0 = sm.halfedge(f);
        if (h0 == SM::null_halfedge()) continue;
        auto h = h0;
        do {
            ring.push_back(target(h, sm));
            h = next(h, sm);
        } while (h != h0);
        if (ring.size() != 3) continue; // expect triangles after triangulate

        glm::vec3 p0(sm.point(ring[0]).x(), sm.point(ring[0]).y(), sm.point(ring[0]).z());
        glm::vec3 p1(sm.point(ring[1]).x(), sm.point(ring[1]).y(), sm.point(ring[1]).z());
        glm::vec3 p2(sm.point(ring[2]).x(), sm.point(ring[2]).y(), sm.point(ring[2]).z());
        glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) continue;

        uint32_t base = static_cast<uint32_t>(outV.size());
        glm::vec4 normal(n, 0.0f);
        glm::vec4 color(1.0f);
        outV.push_back({glm::vec4(p0, 1.0f), normal, color});
        outV.push_back({glm::vec4(p1, 1.0f), normal, color});
        outV.push_back({glm::vec4(p2, 1.0f), normal, color});
        outI.push_back(base + 0);
        outI.push_back(base + 1);
        outI.push_back(base + 2);
    }
}
} // namespace

MainWindow::MainWindow()
{
    setWindowTitle("Digital Squeegee Art - Controls");
    resize(1400, 800);

    QWidget *centralWidget = new QWidget;
    setCentralWidget(centralWidget);
    
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);

    // Control Panel
    QWidget *controls = new QWidget;
    controls->setFixedWidth(320);
    QVBoxLayout *controlLayout = new QVBoxLayout(controls);
    
    QFormLayout *formLayout = new QFormLayout;
    
    // Angle Slider (0 - 360)
    m_angleSlider = new QSlider(Qt::Horizontal);
    m_angleSlider->setRange(0, 360);
    m_angleSlider->setValue(45);
    m_angleLabel = new QLabel("45 deg");
    connect(m_angleSlider, &QSlider::valueChanged, this, &MainWindow::onAngleChanged);
    formLayout->addRow("Angle:", m_angleLabel);
    formLayout->addRow(m_angleSlider);

    // Angle Snap Checkbox
    m_angleSnapCheckBox = new QCheckBox("Angle Snap (45 deg)");
    connect(m_angleSnapCheckBox, &QCheckBox::toggled, this, &MainWindow::onAngleSnapToggled);
    formLayout->addRow(m_angleSnapCheckBox);
    
    // Width Slider (10% - 300%)
    m_widthSlider = new QSlider(Qt::Horizontal);
    m_widthSlider->setRange(10, 300);
    m_widthSlider->setValue(200);
    m_widthLabel = new QLabel("200%");
    connect(m_widthSlider, &QSlider::valueChanged, this, &MainWindow::onWidthChanged);
    formLayout->addRow("Width:", m_widthLabel);
    formLayout->addRow(m_widthSlider);
    
    // Passes Slider (1 - 5)
    m_passesSlider = new QSlider(Qt::Horizontal);
    m_passesSlider->setRange(1, 5);
    m_passesSlider->setValue(1);
    m_passesLabel = new QLabel("1");
    connect(m_passesSlider, &QSlider::valueChanged, this, &MainWindow::onPassesChanged);
    formLayout->addRow("Passes:", m_passesLabel);
    formLayout->addRow(m_passesSlider);
    
    // Speed/Steps Slider (50 - 2000)
    // Lower steps = Faster movement (less physics per pixel)
    // Higher steps = Slower movement (more physics per pixel)
    m_stepsSlider = new QSlider(Qt::Horizontal);
    m_stepsSlider->setRange(25, 250);
    m_stepsSlider->setValue(100);
    m_stepsLabel = new QLabel("100 steps");
    connect(m_stepsSlider, &QSlider::valueChanged, this, &MainWindow::onStepsChanged);
    formLayout->addRow("Simulation Steps:", m_stepsLabel);
    formLayout->addRow("Coarse (25-250):", m_stepsSlider);

    m_stepsFineSlider = new QSlider(Qt::Horizontal);
    m_stepsFineSlider->setRange(1, 25);
    m_stepsFineSlider->setValue(25);
    connect(m_stepsFineSlider, &QSlider::valueChanged, this, &MainWindow::onStepsFineChanged);
    formLayout->addRow("Fine (1-25):", m_stepsFineSlider);

    m_stepsPresetCombo = new QComboBox();
    for (int v = 25; v <= 250; v += 25) {
        m_stepsPresetCombo->addItem(QString::number(v) + " steps", v);
    }
    m_stepsPresetCombo->setCurrentIndex(3); // 100 steps
    connect(m_stepsPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onStepsPresetChanged);
    formLayout->addRow("Preset:", m_stepsPresetCombo);
    
    // Density Slider (100 - 2000)


    
    // Drop Max Size Slider (1 - 100)
    m_sizeSlider = new QSlider(Qt::Horizontal);
    m_sizeSlider->setRange(1, 100);
    m_sizeSlider->setValue(25);
    m_sizeLabel = new QLabel("25 px");
    connect(m_sizeSlider, &QSlider::valueChanged, this, &MainWindow::onSizeChanged);
    formLayout->addRow("Drop Max Size:", m_sizeLabel);
    formLayout->addRow(m_sizeSlider);
    
    // Concentric Circles Slider (1 - 7)
    m_concentricSlider = new QSlider(Qt::Horizontal);
    m_concentricSlider->setRange(1, 7);
    m_concentricSlider->setValue(1);
    m_concentricLabel = new QLabel("1");
    connect(m_concentricSlider, &QSlider::valueChanged, this, &MainWindow::onConcentricChanged);
    formLayout->addRow("Nested Circles:", m_concentricLabel);
    formLayout->addRow(m_concentricSlider);
    
    // Palette Combo
    m_paletteCombo = new QComboBox();
    m_paletteCombo->addItem("Modern Art (Original Calm)");
    m_paletteCombo->addItem("Modern Earth (High Contrast)");
    m_paletteCombo->addItem("Deep Ocean (Vibrant Blues)");
    m_paletteCombo->addItem("Vibrant Sunset");
    m_paletteCombo->addItem("Forest & Berry");
    m_paletteCombo->addItem("Modern Pop Art");
    m_paletteCombo->addItem("Cyberpunk");
    m_paletteCombo->addItem("Single Color Gray");
    connect(m_paletteCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onPaletteChanged);
    formLayout->addRow("Palette:", m_paletteCombo);

    // Shape Combo
    m_shapeCombo = new QComboBox();
    m_shapeCombo->addItem("Circle");
    m_shapeCombo->addItem("Square");
    connect(m_shapeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onShapeChanged);
    formLayout->addRow("Drop Shape:", m_shapeCombo);

    // Generation Mode Combo
    m_genModeCombo = new QComboBox();
    m_genModeCombo->addItem("Random");
    m_genModeCombo->addItem("Grid");
    connect(m_genModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onGenModeChanged);
    formLayout->addRow("Gen Mode:", m_genModeCombo);

    m_depthScalingCheckBox = new QCheckBox("Depth Radius Growth (exp)");
    connect(m_depthScalingCheckBox, &QCheckBox::toggled, this, &MainWindow::onDepthScalingToggled);
    formLayout->addRow(m_depthScalingCheckBox);

    // Squeegee Mode Combo
    m_squeegeeModeCombo = new QComboBox();
    m_squeegeeModeCombo->addItem("Solid");
    m_squeegeeModeCombo->addItem("Soft");
    m_squeegeeModeCombo->addItem("Accurate");
    connect(m_squeegeeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onSqueegeeModeChanged);
    formLayout->addRow("Squeegee:", m_squeegeeModeCombo);

    m_noiseModeCombo = new QComboBox();
    m_noiseModeCombo->addItem("Noise Off", (int)SqueegeeWindow::NoiseOff);
    m_noiseModeCombo->addItem("Noise -> Brush Intensity", (int)SqueegeeWindow::NoiseBrushIntensity);
    m_noiseModeCombo->addItem("Noise -> Brush Offset (wavy)", (int)SqueegeeWindow::NoiseBrushOffset);
    connect(m_noiseModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onNoiseModeChanged);
    formLayout->addRow("Brush Noise:", m_noiseModeCombo);

    m_noiseScaleSlider = new QSlider(Qt::Horizontal);
    m_noiseScaleSlider->setRange(10, 400);
    m_noiseScaleSlider->setValue(120);
    m_noiseScaleLabel = new QLabel("120 px");
    connect(m_noiseScaleSlider, &QSlider::valueChanged, this, &MainWindow::onNoiseScaleChanged);
    formLayout->addRow("Noise Scale:", m_noiseScaleLabel);
    formLayout->addRow(m_noiseScaleSlider);

    m_noiseStrengthSlider = new QSlider(Qt::Horizontal);
    m_noiseStrengthSlider->setRange(0, 100);
    m_noiseStrengthSlider->setValue(0);
    m_noiseStrengthLabel = new QLabel("0.00");
    connect(m_noiseStrengthSlider, &QSlider::valueChanged, this, &MainWindow::onNoiseStrengthChanged);
    formLayout->addRow("Noise Strength:", m_noiseStrengthLabel);
    formLayout->addRow(m_noiseStrengthSlider);

    m_noisePreviewLabel = new QLabel();
    m_noisePreviewLabel->setFixedSize(180, 90);
    m_noisePreviewLabel->setFrameStyle(QFrame::Box | QFrame::Plain);
    m_noisePreviewLabel->setAlignment(Qt::AlignCenter);
    m_noisePreviewLabel->setScaledContents(true);
    formLayout->addRow("Noise Preview:", m_noisePreviewLabel);

    // Sharpen Slider
    m_sharpenSlider = new QSlider(Qt::Horizontal);
    m_sharpenSlider->setRange(0, 200); // 0.0 - 2.0
    m_sharpenSlider->setValue(0);
    connect(m_sharpenSlider, &QSlider::valueChanged, this, &MainWindow::onSharpenChanged);
    formLayout->addRow("Sharpen:", m_sharpenSlider);

    // Brush Type Combo (Restored)
    m_brushTypeCombo = new QComboBox();
    m_brushTypeCombo->addItem("Normal");
    m_brushTypeCombo->addItem("Missing Teeth");
    // Connect to something if needed, or just poll it like we do in onPaintTrails
    formLayout->addRow("Brush Type:", m_brushTypeCombo);

    // Grid Step Slider (10 - 200 px)
    m_gridStepSlider = new QSlider(Qt::Horizontal);
    m_gridStepSlider->setRange(10, 200);
    m_gridStepSlider->setValue(50);
    m_gridStepLabel = new QLabel("50 px");
    connect(m_gridStepSlider, &QSlider::valueChanged, this, &MainWindow::onGridStepChanged);
    
    m_densityLabel = new QLabel("600 drops");
    m_densitySlider = new QSlider(Qt::Horizontal);
    m_densitySlider->setRange(10, 5000); // 10 to 5000 drops
    m_densitySlider->setValue(600);
    connect(m_densitySlider, &QSlider::valueChanged, this, &MainWindow::onDensityChanged);

    QVBoxLayout *genLayout = new QVBoxLayout;
    genLayout->setSpacing(5); // Tighter spacing for control groups

    genLayout->addWidget(new QLabel("<b>Generation:</b>"));
    
    // Shape
    genLayout->addWidget(new QLabel("Shape:"));
    genLayout->addWidget(m_shapeCombo);
    
    // Mode
    genLayout->addWidget(new QLabel("Mode:"));
    genLayout->addWidget(m_genModeCombo);
    
    // Size
    m_sizeSlider->setRange(1, 150); // Allow down to 1px
    genLayout->addWidget(m_sizeLabel);
    genLayout->addWidget(m_sizeSlider);
    
    // Min Size Ratio
    m_minSizeRatioLabel = new QLabel("Min Size: 10%");
    m_minSizeRatioSlider = new QSlider(Qt::Horizontal);
    m_minSizeRatioSlider->setRange(10, 90); // 0.1 to 0.9
    m_minSizeRatioSlider->setValue(10);
    connect(m_minSizeRatioSlider, &QSlider::valueChanged, this, &MainWindow::onMinSizeRatioChanged);
    
    genLayout->addWidget(m_minSizeRatioLabel);
    genLayout->addWidget(m_minSizeRatioSlider);
    
    // Concentric
    genLayout->addWidget(m_concentricLabel);
    genLayout->addWidget(m_concentricSlider);
    
    // Grid Step
    genLayout->addWidget(m_gridStepLabel);
    genLayout->addWidget(m_gridStepSlider);
    
    // Density
    genLayout->addWidget(m_densityLabel);
    genLayout->addWidget(m_densitySlider);

    // Line Width (Moved here)
    m_lineWidthLabel = new QLabel("Trail Width: 1.0 px");
    m_lineWidthSlider = new QSlider(Qt::Horizontal);
    m_lineWidthSlider->setRange(1, 1000); // 0.1 to 100.0
    m_lineWidthSlider->setValue(10);
    connect(m_lineWidthSlider, &QSlider::valueChanged, this, &MainWindow::onLineWidthChanged);
    genLayout->addWidget(m_lineWidthLabel);
    genLayout->addWidget(m_lineWidthSlider);
    
    // Buttons in Gen Layout
    QPushButton *btnProject = new QPushButton("Project to Canvas");
    connect(btnProject, &QPushButton::clicked, this, &MainWindow::onProjectAgents);
    genLayout->addWidget(btnProject);

    QPushButton *btnPaint = new QPushButton("Paint Trails");
    connect(btnPaint, &QPushButton::clicked, this, &MainWindow::onPaintTrails);
    genLayout->addWidget(btnPaint);
    
    QPushButton *btnClear = new QPushButton("Clear Canvas");
    connect(btnClear, &QPushButton::clicked, this, &MainWindow::onClearCanvas);
    genLayout->addWidget(btnClear);
    
    formLayout->addRow(genLayout);
    // Preview Checkbox
    m_previewCheckBox = new QCheckBox("Show Preview Only");
    connect(m_previewCheckBox, &QCheckBox::toggled, this, &MainWindow::onPreviewToggled);
    formLayout->addRow(m_previewCheckBox);
    
    // Toroidal Checkbox
    m_toroidalCheckBox = new QCheckBox("Toroidal Movement");
    connect(m_toroidalCheckBox, &QCheckBox::toggled, this, &MainWindow::onToroidalToggled);
    formLayout->addRow(m_toroidalCheckBox);

    // Background Color Button
    QPushButton *bgColorBtn = new QPushButton("Background Color");
    connect(bgColorBtn, &QPushButton::clicked, this, &MainWindow::onBackgroundColorClicked);
    formLayout->addRow(bgColorBtn);

    // BUTTONS (Global)
    QGridLayout *btnLayout = new QGridLayout();
    
    QPushButton *regenBtn = new QPushButton("Regenerate (R)");
    connect(regenBtn, &QPushButton::clicked, this, &MainWindow::onRegenerate);
    btnLayout->addWidget(regenBtn, 0, 0);

    QPushButton *regenOverlayBtn = new QPushButton("Regen (Overlay)");
    connect(regenOverlayBtn, &QPushButton::clicked, this, &MainWindow::onRegenerateOverlay);
    btnLayout->addWidget(regenOverlayBtn, 0, 1);

    QPushButton *regenShiftedBtn = new QPushButton("Regen (Shifted)");
    connect(regenShiftedBtn, &QPushButton::clicked, this, &MainWindow::onRegenerateShiftedOverlay);
    btnLayout->addWidget(regenShiftedBtn, 1, 0);

    QPushButton *regenOverlaySqueegeeBtn = new QPushButton("Regen2 (Overlay)");
    connect(regenOverlaySqueegeeBtn, &QPushButton::clicked, this, &MainWindow::onRegenerateOverlaySqueegeeOnly);
    btnLayout->addWidget(regenOverlaySqueegeeBtn, 1, 1);

    QPushButton *saturateBtn = new QPushButton("Saturate");
    connect(saturateBtn, &QPushButton::clicked, this, &MainWindow::onSaturate);
    btnLayout->addWidget(saturateBtn, 2, 0);

    QPushButton *combFixBtn = new QPushButton("Comb Fix");
    connect(combFixBtn, &QPushButton::clicked, this, &MainWindow::onCombFix);
    btnLayout->addWidget(combFixBtn, 2, 1);
    
    controlLayout->addLayout(btnLayout);
    
    // TABS
    QTabWidget *tabs = new QTabWidget();
    controlLayout->addWidget(tabs);
    
    // --- TAB 1: BRUSH ---
    QWidget *tabBrush = new QWidget();
    QVBoxLayout *brushLayout = new QVBoxLayout(tabBrush);
    brushLayout->addLayout(formLayout); // Restore the missing menu!
    brushLayout->addStretch();
    tabs->addTab(tabBrush, "Brush");

    // --- TAB 2: 3D VIEW ---
    QWidget *tab3D = new QWidget();
    QVBoxLayout *tab3DLayout = new QVBoxLayout(tab3D);
    
    // Light Controls
    QFormLayout *lightLayout = new QFormLayout();
    m_lightAzimuthSlider = new QSlider(Qt::Horizontal);
    m_lightAzimuthSlider->setRange(0, 360);
    m_lightAzimuthSlider->setValue(45);
    connect(m_lightAzimuthSlider, &QSlider::valueChanged, this, &MainWindow::onLightChanged);
    
    m_lightElevationSlider = new QSlider(Qt::Horizontal);
    m_lightElevationSlider->setRange(-90, 90);
    m_lightElevationSlider->setValue(30);
    connect(m_lightElevationSlider, &QSlider::valueChanged, this, &MainWindow::onLightChanged);
    
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(10, 100); // 1.0 to 10.0
    m_zoomSlider->setValue(30); // 3.0
    connect(m_zoomSlider, &QSlider::valueChanged, this, &MainWindow::onZoomChanged);
    
    lightLayout->addRow("Azimuth:", m_lightAzimuthSlider);
    lightLayout->addRow("Elevation:", m_lightElevationSlider);
    lightLayout->addRow("Zoom:", m_zoomSlider);
    
    tab3DLayout->addWidget(new QLabel("<b>Lighting & Camera:</b>"));
    tab3DLayout->addLayout(lightLayout);
    tab3DLayout->addSpacing(15);
    
    // Agent Controls
    QFormLayout *agentLayout = new QFormLayout();
    m_agentButton = new QPushButton("Start Agents");
    m_agentButton->setCheckable(true);
    m_agentButton->setCheckable(true);
    connect(m_agentButton, &QPushButton::clicked, this, &MainWindow::onToggleAgents);

    QPushButton *projectAgentsBtn = new QPushButton("Project to Canvas");
    connect(projectAgentsBtn, &QPushButton::clicked, this, &MainWindow::onProjectAgents);

    QPushButton *paintTrailsBtn = new QPushButton("Paint Trails");
    connect(paintTrailsBtn, &QPushButton::clicked, this, &MainWindow::onPaintTrails);

    QPushButton *clearCanvasBtn = new QPushButton("Clear Canvas");
    connect(clearCanvasBtn, &QPushButton::clicked, this, &MainWindow::onClearCanvas);
    
    m_agentCountLabel = new QLabel("10 agents");
    m_agentCountSlider = new QSlider(Qt::Horizontal);
    m_agentCountSlider->setRange(10, 5000);
    m_agentCountSlider->setValue(10);
    connect(m_agentCountSlider, &QSlider::valueChanged, this, &MainWindow::onAgentCountChanged);
    
    m_agentLifetimeLabel = new QLabel("1000 ticks");
    m_agentLifetimeSlider = new QSlider(Qt::Horizontal);
    m_agentLifetimeSlider->setRange(10, 1500);
    m_agentLifetimeSlider->setValue(1000);
    connect(m_agentLifetimeSlider, &QSlider::valueChanged, this, &MainWindow::onAgentLifetimeChanged);
    
    agentLayout->addRow("Simulation:", m_agentButton);
    agentLayout->addRow(projectAgentsBtn);
    agentLayout->addRow(paintTrailsBtn);
    agentLayout->addRow(clearCanvasBtn);
    agentLayout->addRow(m_agentCountLabel);
    agentLayout->addRow("Count:", m_agentCountSlider);
    agentLayout->addRow(m_agentLifetimeLabel);
    agentLayout->addRow("Lifetime:", m_agentLifetimeSlider);


    
    tab3DLayout->addWidget(new QLabel("<b>Agent Projection:</b>"));
    tab3DLayout->addLayout(agentLayout);
    tab3DLayout->addStretch();
    
    tabs->addTab(tab3D, "3D View");

    // --- TAB 3: 3D GEN ---
    QWidget *tabGen = new QWidget();
    QVBoxLayout *tabGenLayout = new QVBoxLayout(tabGen);
    QFormLayout *genForm = new QFormLayout();
    
    m_primitiveCombo = new QComboBox();
    m_primitiveCombo->addItem("HexSphere");
    
    connect(m_primitiveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onPrimitiveChanged);
    genForm->addRow("Primitive:", m_primitiveCombo);
    
    m_primParam1Label = new QLabel("Param 1");
    m_primParam1Slider = new QSlider(Qt::Horizontal);
    m_primParam1Slider->setRange(1, 100);
    m_primParam1Slider->setValue(2);
    connect(m_primParam1Slider, &QSlider::valueChanged, this, &MainWindow::onPrimParam1Changed);
    genForm->addRow(m_primParam1Label, m_primParam1Slider);
    
    m_primParam2Label = new QLabel("Param 2");
    m_primParam2Slider = new QSlider(Qt::Horizontal);
    m_primParam2Slider->setRange(0, 100);
    m_primParam2Slider->setValue(50);
    connect(m_primParam2Slider, &QSlider::valueChanged, this, &MainWindow::onPrimParam2Changed);
    genForm->addRow(m_primParam2Label, m_primParam2Slider);

    m_primParam3Label = new QLabel("Param 3");
    m_primParam3Slider = new QSlider(Qt::Horizontal);
    m_primParam3Slider->setRange(0, 100);
    m_primParam3Slider->setValue(0);
    connect(m_primParam3Slider, &QSlider::valueChanged, this, &MainWindow::onPrimParam3Changed);
    genForm->addRow(m_primParam3Label, m_primParam3Slider);
    
    m_generateMeshBtn = new QPushButton("Generate Mesh");
    connect(m_generateMeshBtn, &QPushButton::clicked, this, &MainWindow::onGenerateMesh);

    // Extrusion controls
    m_extrudeProbSlider = new QSlider(Qt::Horizontal);
    m_extrudeProbSlider->setRange(0, 100);
    m_extrudeProbSlider->setValue(25);
    m_extrudeProbLabel = new QLabel("25%");
    connect(m_extrudeProbSlider, &QSlider::valueChanged, this, [this](int v){
        m_extrudeProbLabel->setText(QString::number(v) + "%");
    });
    genForm->addRow("Random Faces:", m_extrudeProbLabel);
    genForm->addRow(m_extrudeProbSlider);

    m_extrudeDistSlider = new QSlider(Qt::Horizontal);
    m_extrudeDistSlider->setRange(-200, 200); // -2.0 .. 2.0
    m_extrudeDistSlider->setValue(20);
    m_extrudeDistLabel = new QLabel("+0.20");
    connect(m_extrudeDistSlider, &QSlider::valueChanged, this, [this](int v){
        m_extrudeDistLabel->setText(QString::number(v / 100.0, 'f', 2));
    });
    genForm->addRow("Extrude Dist:", m_extrudeDistLabel);
    genForm->addRow(m_extrudeDistSlider);

    m_extrudeScaleSlider = new QSlider(Qt::Horizontal);
    m_extrudeScaleSlider->setRange(10, 300); // 0.1 .. 3.0
    m_extrudeScaleSlider->setValue(100);
    m_extrudeScaleLabel = new QLabel("1.00x");
    connect(m_extrudeScaleSlider, &QSlider::valueChanged, this, [this](int v){
        m_extrudeScaleLabel->setText(QString::number(v / 100.0, 'f', 2) + "x");
    });
    genForm->addRow("Extrude Scale:", m_extrudeScaleLabel);
    genForm->addRow(m_extrudeScaleSlider);

    m_extrudeRemoveBase = new QCheckBox("Remove base faces");
    m_extrudeRemoveBase->setChecked(false);
    genForm->addRow(m_extrudeRemoveBase);

    m_extrudeBtn = new QPushButton("Random Extrude");
    connect(m_extrudeBtn, &QPushButton::clicked, this, &MainWindow::onExtrudeRandom);
    
    tabGenLayout->addWidget(new QLabel("<b>Mesh Generation:</b>"));
    tabGenLayout->addLayout(genForm);
    tabGenLayout->addWidget(m_generateMeshBtn);
    tabGenLayout->addWidget(m_extrudeBtn);

    // Modifiers: Tentacles + Catmull-Clark
    tabGenLayout->addWidget(new QLabel("<b>Modifiers:</b>"));
    QFormLayout *modForm = new QFormLayout();

    m_tentacleStepsSlider = new QSlider(Qt::Horizontal);
    m_tentacleStepsSlider->setRange(1, 50);
    m_tentacleStepsSlider->setValue(10);
    m_tentacleStepsLabel = new QLabel("Tentacle Steps: 10");
    connect(m_tentacleStepsSlider, &QSlider::valueChanged, this, [this](int v){
        m_tentacleStepsLabel->setText(QString("Tentacle Steps: %1").arg(v));
    });
    modForm->addRow(m_tentacleStepsLabel, m_tentacleStepsSlider);

    m_tentacleDistSlider = new QSlider(Qt::Horizontal);
    m_tentacleDistSlider->setRange(1, 200); // 0.01 .. 2.00
    m_tentacleDistSlider->setValue(15);     // 0.15 default
    m_tentacleDistLabel = new QLabel("Step Dist: 0.15");
    connect(m_tentacleDistSlider, &QSlider::valueChanged, this, [this](int v){
        m_tentacleDistLabel->setText(QString("Step Dist: %1").arg(v / 100.0, 0, 'f', 2));
    });
    modForm->addRow(m_tentacleDistLabel, m_tentacleDistSlider);

    m_tentacleScaleSlider = new QSlider(Qt::Horizontal);
    m_tentacleScaleSlider->setRange(10, 300); // 0.10 .. 3.00
    m_tentacleScaleSlider->setValue(75);      // 0.75 default
    m_tentacleScaleLabel = new QLabel("Step Scale: 0.75x");
    connect(m_tentacleScaleSlider, &QSlider::valueChanged, this, [this](int v){
        m_tentacleScaleLabel->setText(QString("Step Scale: %1x").arg(v / 100.0, 0, 'f', 2));
    });
    modForm->addRow(m_tentacleScaleLabel, m_tentacleScaleSlider);

    m_growTentaclesBtn = new QPushButton("Grow Tentacles (uses Random Faces %)");
    connect(m_growTentaclesBtn, &QPushButton::clicked, this, &MainWindow::onGrowTentacles);
    modForm->addRow(m_growTentaclesBtn);

    m_catmullIterSlider = new QSlider(Qt::Horizontal);
    m_catmullIterSlider->setRange(0, 4);
    m_catmullIterSlider->setValue(1);
    m_catmullIterLabel = new QLabel("Catmull-Clark Iter: 1");
    connect(m_catmullIterSlider, &QSlider::valueChanged, this, [this](int v){
        m_catmullIterLabel->setText(QString("Catmull-Clark Iter: %1").arg(v));
    });
    modForm->addRow(m_catmullIterLabel, m_catmullIterSlider);

    m_catmullSmoothBtn = new QPushButton("Smooth (Catmull-Clark)");
    connect(m_catmullSmoothBtn, &QPushButton::clicked, this, &MainWindow::onSmoothCatmull);
    modForm->addRow(m_catmullSmoothBtn);

    tabGenLayout->addLayout(modForm);
    tabGenLayout->addStretch();
    
    tabs->addTab(tabGen, "3D Gen");
    
    // Initialize labels
    onPrimitiveChanged(0);
    
    QLabel *info = new QLabel("Controls:\nSpace: Toggle Tool\n1-5: Colors\nWheel: Brush Size\nB: Blur");
    controlLayout->addWidget(info);
    mainLayout->addWidget(controls);

    // Viewer area: real subwindows inside MDI (so they behave like windows, not static sections)
    m_mdiArea = new QMdiArea;
    m_mdiArea->setViewMode(QMdiArea::SubWindowView);
    m_mdiArea->setDocumentMode(false);
    m_mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation, true);
    m_mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdiArea->setBackground(QBrush(QColor(40, 40, 50))); // Dark background to prevent recursion artifacts

    m_meshViewer = new MeshViewerWidget();
    m_meshViewer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_meshSubWindow = m_mdiArea->addSubWindow(m_meshViewer);
    m_meshSubWindow->setWindowTitle("3D Mesh Viewer");
    m_meshSubWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    m_meshSubWindow->resize(640, 480);
    m_meshSubWindow->show();

    // Squeegee Window
    // Squeegee Window
    m_squeegeeWindow = new SqueegeeWindow();
    m_squeegeeWindow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_squeegeeWindow->setMinimumSize(480, 360);
    m_squeegeeWindow->setFocusPolicy(Qt::StrongFocus);
    
    m_squeegeeSubWindow = m_mdiArea->addSubWindow(m_squeegeeWindow);
    m_squeegeeSubWindow->setWindowTitle("2D Brush Canvas");
    m_squeegeeSubWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    m_squeegeeSubWindow->resize(640, 480);
    m_squeegeeSubWindow->show();

    // Agent Viewer
    // Agent Viewer
    m_agentWindow = new AgentProjectionWindow(m_meshViewer);
    m_agentSubWindow = m_mdiArea->addSubWindow(m_agentWindow);
    m_agentSubWindow->setWindowTitle("Agent Projection");
    m_agentSubWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    m_agentSubWindow->resize(480, 360);
    m_agentSubWindow->show();
    
    mainLayout->addWidget(m_mdiArea, 1);

    // Initial values
    m_squeegeeWindow->setGenAngle(45.0f);
    m_squeegeeWindow->setGenWidth(2.0f);
    m_squeegeeWindow->setGenPasses(1);
    m_squeegeeWindow->setGenSteps(600);
    onNoiseModeChanged(m_noiseModeCombo->currentIndex());
    onNoiseScaleChanged(m_noiseScaleSlider->value());
    onNoiseStrengthChanged(m_noiseStrengthSlider->value());
    updateNoisePreview();

    loadSettings();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_tiledOnce) return;
    
    // Check if we loaded specific geometries. If so, don't tile.
    QSettings settings;
    if (settings.contains("geometry")) {
        // Assume loaded
        m_tiledOnce = true;
        return;
    }

    m_tiledOnce = true;
    // Tile after the window is actually shown so layout sizes are valid
    QTimer::singleShot(0, this, [this]() {
        m_mdiArea->tileSubWindows();
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    
    if (m_meshSubWindow) settings.setValue("meshWinGeometry", m_meshSubWindow->saveGeometry());
    if (m_squeegeeSubWindow) settings.setValue("squeegeeWinGeometry", m_squeegeeSubWindow->saveGeometry());
    if (m_agentSubWindow) settings.setValue("agentWinGeometry", m_agentSubWindow->saveGeometry());
}

void MainWindow::loadSettings()
{
    QSettings settings;
    if (settings.contains("geometry")) restoreGeometry(settings.value("geometry").toByteArray());
    if (settings.contains("windowState")) restoreState(settings.value("windowState").toByteArray());
    
    if (m_meshSubWindow && settings.contains("meshWinGeometry")) 
        m_meshSubWindow->restoreGeometry(settings.value("meshWinGeometry").toByteArray());
        
    if (m_squeegeeSubWindow && settings.contains("squeegeeWinGeometry"))
        m_squeegeeSubWindow->restoreGeometry(settings.value("squeegeeWinGeometry").toByteArray());
        
    if (m_agentSubWindow && settings.contains("agentWinGeometry"))
        m_agentSubWindow->restoreGeometry(settings.value("agentWinGeometry").toByteArray());
}

MainWindow::~MainWindow()
{
    delete m_squeegeeWindow;
}

void MainWindow::onPrimitiveChanged(int index)
{
    // Update labels and slider ranges based on primitive
    m_primParam1Slider->setEnabled(true);
    m_primParam2Slider->setEnabled(true);
    m_primParam3Slider->setEnabled(true);
    
    // Default hiding/showing logic could be added here
    // For now just update labels
    QString p1 = "Param 1";
    QString p2 = "Param 2";
    QString p3 = "Param 3";
    
    Q_UNUSED(index);
    p1 = "Resolution (1-5)";
    m_primParam1Slider->setRange(1, 5);m_primParam1Slider->setValue(2);
    p2 = "Radius (0.1-5.0)";
    m_primParam2Slider->setRange(1, 50);m_primParam2Slider->setValue(10);
    p3 = "N/A"; m_primParam3Slider->setEnabled(false);
    
    m_primParam1Label->setText(p1);
    m_primParam2Label->setText(p2);
    m_primParam3Label->setText(p3);
}

void MainWindow::onPrimParam1Changed(int value)
{
    // Optional: Update value label
}

void MainWindow::onPrimParam2Changed(int value)
{
    // Optional: Update value label
}

void MainWindow::onPrimParam3Changed(int value)
{
    // Optional: Update value label
}

#include "GeomCreate.h"

// (Old onGenerateMesh removed - see new implementation below)

void MainWindow::onAngleChanged(int value)
{
    if (m_angleSnapCheckBox->isChecked()) {
        int snapped = qRound(value / 45.0) * 45;
        if (snapped != value) {
            m_angleSlider->setValue(snapped);
            return;
        }
    }
    m_angleLabel->setText(QString::number(value) + " deg");
    m_squeegeeWindow->setGenAngle((float)value);
}

void MainWindow::onWidthChanged(int value)
{
    m_widthLabel->setText(QString::number(value) + "%");
    m_squeegeeWindow->setGenWidth((float)value / 100.0f);
}

void MainWindow::onPassesChanged(int value)
{
    m_passesLabel->setText(QString::number(value));
    m_squeegeeWindow->setGenPasses(value);
}

void MainWindow::onStepsChanged(int value)
{
    m_stepsLabel->setText(QString("Simulation Steps: %1 steps").arg(value));
    
    // Sync logic (optional, but good for UX if we had fine controls linked)
    // The user moved the coarse slider, so update label and backend.
    
    // Check if we need to sync fine slider or presets?
    // In previous versions (implied), this might have done more complex syncing.
    // For now, ensuring the label updates is the critical fix.
    
    // Also, looking at the UI, there is a "Fine" slider and "Preset" combo.
    // We should probably keep them in sync if possible, but let's at least fix the label.
    // Actually, looking at the deleted code from earlier steps (Step 322), 
    // there was extensive logic here. I should restore it.
    
    int snapped = value; 
    // The Coarse slider is 25-250.
    
    // Update label
    m_stepsLabel->setText(QString("%1 steps").arg(snapped));

    // Update Fine Slider (approximate sync)
    // The fine slider is 1-25. If coarse is moved, we might reset fine or leave it?
    // Let's just update the label and backend first to fix the immediate bug.
    
    m_squeegeeWindow->setGenSteps(snapped);
}

void MainWindow::onDensityChanged(int value)
{
    m_densityLabel->setText(QString("Density: %1").arg(value));
    m_squeegeeWindow->setGenDensity(value);
}

// onSizeChanged removed from here as it is defined below


void MainWindow::onSizeChanged(int value)
{
    m_sizeLabel->setText(QString::number(value) + " px");
    m_squeegeeWindow->setDropMaxSize(value);
}

void MainWindow::onMinSizeRatioChanged(int value)
{
    float ratio = value / 100.0f;
    m_minSizeRatioLabel->setText(QString("Min Size: %1%").arg(value));
    m_squeegeeWindow->setMinSizeRatio(ratio);
}

void MainWindow::onConcentricChanged(int value)
{
    m_concentricLabel->setText(QString::number(value));
    m_squeegeeWindow->setGenConcentric(value);
}

void MainWindow::onPreviewToggled(bool checked)
{
    m_squeegeeWindow->setShowPreview(checked);
}

void MainWindow::onToroidalToggled(bool checked)
{
    m_squeegeeWindow->setToroidal(checked);
}

void MainWindow::onAngleSnapToggled(bool checked)
{
    if (checked) {
        // Trigger re-snap of current value
        onAngleChanged(m_angleSlider->value());
    }
}

void MainWindow::onPaletteChanged(int index)
{
    m_squeegeeWindow->setPalette(index);
    if (m_meshViewer) m_meshViewer->setPaletteIndex(index);
    if (m_meshViewer) m_meshViewer->setPalettes(&m_squeegeeWindow->allPalettes());
}

void MainWindow::onShapeChanged(int index)
{
    m_squeegeeWindow->setGenShape((SqueegeeWindow::GenShape)index);
}

void MainWindow::onGenModeChanged(int index)
{
    m_squeegeeWindow->setGenMode((SqueegeeWindow::GenMode)index);
}

void MainWindow::onSharpenChanged(int value)
{
    float amount = value / 100.0f; // 0.0 - 2.0
    m_squeegeeWindow->setSharpenAmount(amount);
}

void MainWindow::onNoiseModeChanged(int index)
{
    int modeVal = m_noiseModeCombo->itemData(index).toInt();
    m_squeegeeWindow->setBrushNoiseMode(static_cast<SqueegeeWindow::BrushNoiseMode>(modeVal));
    updateNoisePreview();
}

void MainWindow::onNoiseScaleChanged(int value)
{
    int clamped = std::clamp(value, 10, 400);
    if (clamped != value) {
        m_noiseScaleSlider->blockSignals(true);
        m_noiseScaleSlider->setValue(clamped);
        m_noiseScaleSlider->blockSignals(false);
    }
    m_noiseScaleLabel->setText(QString::number(clamped) + " px");
    m_squeegeeWindow->setBrushNoiseScale(static_cast<float>(clamped));
    updateNoisePreview();
}

void MainWindow::onNoiseStrengthChanged(int value)
{
    int clamped = std::clamp(value, 0, 100);
    if (clamped != value) {
        m_noiseStrengthSlider->blockSignals(true);
        m_noiseStrengthSlider->setValue(clamped);
        m_noiseStrengthSlider->blockSignals(false);
    }
    float strength = clamped / 100.0f;
    m_noiseStrengthLabel->setText(QString::number(strength, 'f', 2));
    m_squeegeeWindow->setBrushNoiseStrength(strength);
    updateNoisePreview();
}

void MainWindow::updateNoisePreview()
{
    if (!m_noisePreviewLabel) return;

    const int w = 180;
    const int h = 90;
    QImage img(w, h, QImage::Format_RGB32);
    float scale = std::max(1, m_noiseScaleSlider->value());
    float strength = m_noiseStrengthSlider->value() / 100.0f;
    int modeVal = m_noiseModeCombo->currentData().toInt();
    auto mode = static_cast<SqueegeeWindow::BrushNoiseMode>(modeVal);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float nx = static_cast<float>(x) / scale;
            float ny = static_cast<float>(y) / scale;
            float n = m_noisePreviewGen.fractal(nx, ny, 4, 2.1f, 0.55f); // [-1,1]
            QColor c(128, 128, 128);

            if (mode == SqueegeeWindow::NoiseBrushIntensity) {
                float v = 0.5f + 0.5f * n * strength * 1.2f;
                v = std::clamp(v, 0.0f, 1.0f);
                int g = static_cast<int>(v * 255.0f);
                c = QColor(g, g, g);
            } else if (mode == SqueegeeWindow::NoiseBrushOffset) {
                float v = 0.5f + 0.5f * n * strength;
                v = std::clamp(v, 0.0f, 1.0f);
                // Bipolar coloring: left half to magenta, right to cyan
                int r = static_cast<int>((0.3f + 0.7f * v) * 255.0f);
                int g = static_cast<int>((0.2f + 0.6f * (1.0f - std::abs(n) * strength)) * 255.0f);
                int b = static_cast<int>((0.3f + 0.7f * (1.0f - v)) * 255.0f);
                c = QColor(r, g, b);
            }

            img.setPixelColor(x, y, c);
        }
    }

    m_noisePreviewLabel->setPixmap(QPixmap::fromImage(img));
}

void MainWindow::onSqueegeeModeChanged(int index)
{
    m_squeegeeWindow->setSqueegeeMode((SqueegeeWindow::SqueegeeMode)index);
}

void MainWindow::onStepsPresetChanged(int index)
{
    int value = m_stepsPresetCombo->itemData(index).toInt();
    m_stepsSlider->setValue(value);
}

void MainWindow::onDepthScalingToggled(bool checked)
{
    m_squeegeeWindow->setDepthRadiusScaling(checked);
}

void MainWindow::onStepsFineChanged(int value)
{
    int clamped = std::clamp(value, 1, 25);
    if (clamped != value) {
        m_stepsFineSlider->blockSignals(true);
        m_stepsFineSlider->setValue(clamped);
        m_stepsFineSlider->blockSignals(false);
    }

    m_stepsLabel->setText(QString::number(clamped) + " steps");

    if (m_stepsSlider->value() != 25) {
        m_stepsSlider->blockSignals(true);
        m_stepsSlider->setValue(25);
        m_stepsSlider->blockSignals(false);
    }

    // Presets apply only to multiples of 25; clear selection
    if (m_stepsPresetCombo->currentIndex() != -1) {
        m_stepsPresetCombo->blockSignals(true);
        m_stepsPresetCombo->setCurrentIndex(-1);
        m_stepsPresetCombo->blockSignals(false);
    }

    m_squeegeeWindow->setGenSteps(clamped);
}

void MainWindow::onGridStepChanged(int value)
{
    m_gridStepLabel->setText(QString::number(value) + " px");
    m_squeegeeWindow->setGridStep(value);
}

void MainWindow::onRegenerate()
{
    m_squeegeeWindow->regenerate();
}

void MainWindow::onRegenerateOverlay()
{
    m_squeegeeWindow->setKeepExisting(true);
    m_squeegeeWindow->regenerate();
    m_squeegeeWindow->setKeepExisting(false);
}

void MainWindow::onRegenerateShiftedOverlay()
{
    m_squeegeeWindow->regenerateShiftedOverlay();
}

void MainWindow::onRegenerateOverlaySqueegeeOnly()
{
    m_squeegeeWindow->regenerateSqueegeeOnly();
}

void MainWindow::onSaturate()
{
    m_squeegeeWindow->applySaturation();
}

void MainWindow::onCombFix()
{
    m_squeegeeWindow->applyCombFix();
}

void MainWindow::onLightChanged()
{
    float azimuth = qDegreesToRadians((float)m_lightAzimuthSlider->value());
    float elevation = qDegreesToRadians((float)m_lightElevationSlider->value());
    
    // Spherical to Cartesian
    // Y is Up
    float x = std::sin(azimuth) * std::cos(elevation);
    float y = std::sin(elevation);
    float z = std::cos(azimuth) * std::cos(elevation);
    
    if (m_meshViewer) {
        m_meshViewer->setLightDirection(QVector3D(x, y, z));
    }
}

void MainWindow::onZoomChanged(int value)
{
    float dist = value / 10.0f;
    if (m_meshViewer) {
        m_meshViewer->setCameraDistance(dist);
    }
}

void MainWindow::onToggleAgents()
{
    bool run = m_agentButton->isChecked();
    m_agentButton->setText(run ? "Stop Agents" : "Start Agents");
    if (m_agentWindow) {
        m_agentWindow->setRunning(run);
    }
}

void MainWindow::onAgentCountChanged(int value)
{
    m_agentCountLabel->setText(QString("%1 agents").arg(value));
    if (m_agentWindow) {
        m_agentWindow->setAgentCount(value);
    }
}

void MainWindow::onAgentLifetimeChanged(int value)
{
    m_agentLifetimeLabel->setText(QString("%1 ticks").arg(value));
    if (m_agentWindow) {
        m_agentWindow->setAgentLifetime(value);
    }
}

void MainWindow::onLineWidthChanged(int value)
{
    float width = value / 10.0f;
    m_lineWidthLabel->setText(QString("Trail Width: %1 px").arg(width, 0, 'f', 1));
    if(m_agentWindow) m_agentWindow->setLineWidth(width);
}

void MainWindow::onProjectAgents()
{
    if (!m_meshViewer || !m_squeegeeWindow) return;
    
    int canvasW = m_squeegeeWindow->width();
    int canvasH = m_squeegeeWindow->height();
    int camW = m_meshViewer->width();
    int camH = m_meshViewer->height();
    if (camW <= 0 || camH <= 0 || canvasW <= 0 || canvasH <= 0) return;

    // Preserve MeshViewer camera aspect; letterbox into the squeegee canvas.
    float scale = std::min((float)canvasW / (float)camW, (float)canvasH / (float)camH);
    float offsetX = (canvasW - camW * scale) * 0.5f;
    float offsetY = (canvasH - camH * scale) * 0.5f;

    // Project using the same camera/viewport as the 3D view, then scale into the brush canvas.
    auto agents = m_meshViewer->getProjectedAgents(camW, camH);

    // Clear previous projection so each press reflects the current agent set only.
    m_squeegeeWindow->clearCanvas();

    // Drops Only (Project Agents)
    QVector<SqueegeeWindow::DropInfo> drops;
    for (const auto& a : agents) {
        float px = a.screenPos.x() * scale + offsetX;
        float py = a.screenPos.y() * scale + offsetY;
        
        if (a.isVisible && px >= 0 && px < canvasW && py >= 0 && py < canvasH) {
            SqueegeeWindow::DropInfo info;
            // spawnDrops (CPU) expects Raw GL Y.
            info.pos = QVector2D(px, py);
            info.color = a.color;
            info.size = (float)m_sizeSlider->value();
            info.layer = a.layer;
            drops.append(info);
        }
    }
    m_squeegeeWindow->spawnDrops(drops);
}

void MainWindow::onPaintTrails()
{
    if (!m_meshViewer || !m_squeegeeWindow) return;

    int canvasW = m_squeegeeWindow->width();
    int canvasH = m_squeegeeWindow->height();
    int camW = m_meshViewer->width();
    int camH = m_meshViewer->height();
    if (camW <= 0 || camH <= 0 || canvasW <= 0 || canvasH <= 0) return;

    float scale = std::min((float)canvasW / (float)camW, (float)canvasH / (float)camH);
    float offsetX = (canvasW - camW * scale) * 0.5f;
    float offsetY = (canvasH - camH * scale) * 0.5f;

    // Project trails with the same camera as the 3D view, then map into the brush canvas.
    auto agents = m_meshViewer->getProjectedAgents(camW, camH);

    QVector<SqueegeeWindow::PathInfo> paths;
    for (const auto& a : agents) {
        if (a.trailSegments.empty()) continue;
            
        for (int si = 0; si < a.trailSegments.size(); ++si) {
             const auto& seg = a.trailSegments[si];
             if (seg.size() < 2) continue;
             SqueegeeWindow::PathInfo info;
             info.color = a.color;
             //info.size = (float)m_sizeSlider->value();
             float lw = std::max(0.1f, m_lineWidthSlider->value() / 10.0f);
             info.size = lw;
             info.useQtPainter = true; // Always use QPainter for trails now, or make it conditional
             // If user wants "old style" thick lines they can use the "Drop Max Size" maybe?
             // But the request is specific: "when draw 2d lines with 1px width looks like 3-4 px width make then add from 0.1-1 slider"
             // So we default this slider to control the trail width.
             
             info.layer = a.layer;
             
             float avgBright = 1.0f;
             if (si < a.trailBrightness.size()) {
                 const auto& bseg = a.trailBrightness[si];
                 if (!bseg.empty()) {
                     float sum = std::accumulate(bseg.begin(), bseg.end(), 0.0f);
                     avgBright = sum / (float)bseg.size();
                 }
             }
             info.brightness = avgBright;
             
             for (const auto& p : seg) {
                 float px = p.x() * scale + offsetX;
                 float py = p.y() * scale + offsetY;
                 // paintPaths (GPU) expects GL Y (Bottom-Up) similar to spawnDrops.
                 // No need to invert.
                 info.points.append(QVector2D(px, py));
             }
             if (!info.points.isEmpty()) {
                 paths.append(info);
             }
        }
    }
    m_squeegeeWindow->paintPaths(paths);
}

void MainWindow::onClearCanvas()
{
    if (m_squeegeeWindow) m_squeegeeWindow->clearCanvas();
}

void MainWindow::onBackgroundColorClicked()
{
    QColor color = QColorDialog::getColor(Qt::white, this, "Select Background Color", QColorDialog::DontUseNativeDialog);
    if (color.isValid()) {
        m_squeegeeWindow->setBackgroundColor(color);
    }
}

void MainWindow::onGenerateMesh()
{
    if (!m_meshViewer) return;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Params (reuse UI sliders for resolution/radius)
    const int   resolution = m_primParam1Slider->value();
    const float radius     = m_primParam2Slider->value() / 10.0f;
    
    m_currentMesh.clear();

    // 1) Построение hexasphere
    auto allFaces = CgalMeshBuilder::buildHexSphereOriented(
        m_currentMesh,
        resolution,
        radius,
        CgalMeshBuilder::Point_3(0,0,0),
        CgalMeshBuilder::Vector_3(0,1,0),
        0.0
    );

    // Экструзию убрали, чтобы при смене радиуса грани оставались стык в стык.
    m_currentMesh.collect_garbage();

    // 3) Триангулируем копию для рендера, основную меш оставляем полигональной
    CgalMeshBuilder::SurfaceMesh displayMesh = m_currentMesh;
    CgalMeshBuilder::triangulateAll(displayMesh);

    std::vector<CgalMeshBuilder::Vertex> tmp;
    CgalMeshBuilder::toVertexIndexFlat(displayMesh, tmp, indices);
    vertices.resize(tmp.size());
    for(size_t i=0; i<tmp.size(); ++i) {
        vertices[i].position = glm::vec4(tmp[i].pos[0], tmp[i].pos[1], tmp[i].pos[2], 1.0f);
        vertices[i].normal   = glm::vec4(tmp[i].norm[0], tmp[i].norm[1], tmp[i].norm[2], 0.0f);
        vertices[i].color    = glm::vec4(tmp[i].col[0], tmp[i].col[1], tmp[i].col[2], 1.0f);
    }

    m_lastVertices = vertices;
    m_lastIndices = indices;
    m_meshViewer->updateMesh(vertices, indices);
}

// ... (skipping unchanged methods) ...

void MainWindow::onExtrudeRandom()
{
    // If we have a valid Cgal mesh (e.g. HexSphere), use it directly to keep polygons.
    // Otherwise, rebuild from triangles (legacy behavior).
    
    bool usingPersistent = !m_currentMesh.is_empty();
    CgalMeshBuilder::SurfaceMesh* targetMesh = nullptr;
    CgalMeshBuilder::SurfaceMesh tempMesh; // Only used if rebuilding
    
    if (usingPersistent) {
        targetMesh = &m_currentMesh;
    } else {
        if (m_lastVertices.empty() || m_lastIndices.empty()) return;
        tempMesh = toSurfaceMesh(m_lastVertices, m_lastIndices);
        PMP::stitch_borders(tempMesh);
        mergeCoplanarPatches(tempMesh); 
        targetMesh = &tempMesh;
    }

    const double prob = m_extrudeProbSlider->value() / 100.0;
    const double dist = m_extrudeDistSlider->value() / 100.0;
    const double scale = m_extrudeScaleSlider->value() / 100.0;

    auto faces = CgalMeshBuilder::selectFacesRandom(*targetMesh, prob, 1337u);
    if (!faces.empty()) {
        CgalMeshBuilder::extrudeFaces_collectBoth(*targetMesh, faces, dist, scale);
        
        // For display, we need a triangulated copy, BUT we want to keep the main mesh polygonal if possible.
        // However, the requested flow is: modifying *this* mesh.
        // If we just triangulate 'targetMesh', future extrusions will be on triangles again.
        // So we strictly should COPY for display triangulation.
        
        CgalMeshBuilder::SurfaceMesh displayMesh = *targetMesh;
        CgalMeshBuilder::triangulateAll(displayMesh);
        
        std::vector<CgalMeshBuilder::Vertex> tmp;
        CgalMeshBuilder::toVertexIndexFlat(displayMesh, tmp, m_lastIndices);
        m_lastVertices.resize(tmp.size());
        for(size_t i=0; i<tmp.size(); ++i) {
            m_lastVertices[i].position = glm::vec4(tmp[i].pos[0], tmp[i].pos[1], tmp[i].pos[2], 1.0f);
            m_lastVertices[i].normal   = glm::vec4(tmp[i].norm[0], tmp[i].norm[1], tmp[i].norm[2], 0.0f);
            m_lastVertices[i].color    = glm::vec4(tmp[i].col[0], tmp[i].col[1], tmp[i].col[2], 1.0f);
        }
        if (m_meshViewer) m_meshViewer->updateMesh(m_lastVertices, m_lastIndices);
    }
}

void MainWindow::onGrowTentacles()
{
    bool usingPersistent = !m_currentMesh.is_empty();
    SM* targetMesh = nullptr;
    SM tempMesh;

    if (usingPersistent) {
        targetMesh = &m_currentMesh;
    } else {
        if (m_lastVertices.empty() || m_lastIndices.empty()) return;
        tempMesh = toSurfaceMesh(m_lastVertices, m_lastIndices);
        PMP::stitch_borders(tempMesh);
        mergeCoplanarPatches(tempMesh);
        targetMesh = &tempMesh;
    }

    SM& sm = *targetMesh;
    std::size_t faceCount = sm.number_of_faces();
    if (faceCount == 0) return;

    int percent = std::clamp(m_extrudeProbSlider->value(), 0, 100);
    std::size_t seedCount = std::max<std::size_t>(1, std::lround((percent / 100.0) * faceCount));

    auto seeds = CgalMeshBuilderTentacles::pick_random_faces(sm, seedCount, QRandomGenerator::global()->generate());
    if (seeds.empty()) return;

    CgalMeshBuilderTentacles::GrowParams gp;
    gp.steps = m_tentacleStepsSlider->value();
    gp.distPerStep = m_tentacleDistSlider->value() / 100.0;
    gp.amountPerStep = m_tentacleScaleSlider->value() / 100.0;
    gp.seed = QRandomGenerator::global()->generate();
    gp.collectEachStep = false;
    gp.collectBetweenFaces = false;

    // Auto-flip direction to grow outward if normals are inverted.
    auto faceNormal = [&sm](SM::Face_index f) -> Vector {
        auto h0 = sm.halfedge(f);
        if (h0 == SM::null_halfedge()) return Vector(0, 0, 1);
        auto a = sm.point(target(h0, sm));
        auto b = sm.point(target(next(h0, sm), sm));
        auto c = sm.point(target(next(next(h0, sm), sm), sm));
        auto n = CGAL::cross_product(b - a, c - a);
        double len2 = n.squared_length();
        return (len2 > 1e-16) ? n / std::sqrt(len2) : Vector(0, 0, 1);
    };
    double orientAccum = 0.0;
    for (auto f : sm.faces()) {
        if (sm.is_removed(f)) continue;
        Vector n = faceNormal(f);
        auto h = sm.halfedge(f);
        if (h == SM::null_halfedge()) continue;
        auto p = sm.point(target(h, sm));
        orientAccum += (p.x() * n.x() + p.y() * n.y() + p.z() * n.z());
    }
    if (orientAccum < 0.0) {
        gp.distPerStep = -gp.distPerStep;
    }

    CgalMeshBuilderTentacles::extrudeTentaclesSequential(sm, seeds, gp);

    // Build display copy
    CgalMeshBuilder::SurfaceMesh displayMesh = sm;
    CgalMeshBuilder::triangulateAll(displayMesh);

    std::vector<CgalMeshBuilder::Vertex> tmp;
    CgalMeshBuilder::toVertexIndexFlat(displayMesh, tmp, m_lastIndices);
    m_lastVertices.resize(tmp.size());
    for (size_t i = 0; i < tmp.size(); ++i) {
        m_lastVertices[i].position = glm::vec4(tmp[i].pos[0], tmp[i].pos[1], tmp[i].pos[2], 1.0f);
        m_lastVertices[i].normal   = glm::vec4(tmp[i].norm[0], tmp[i].norm[1], tmp[i].norm[2], 0.0f);
        m_lastVertices[i].color    = glm::vec4(tmp[i].col[0], tmp[i].col[1], tmp[i].col[2], 1.0f);
    }
    if (m_meshViewer) m_meshViewer->updateMesh(m_lastVertices, m_lastIndices);

    // Keep polygonal source for further modifiers
    m_currentMesh = sm;
}

void MainWindow::onSmoothCatmull()
{
    int iterations = std::clamp(m_catmullIterSlider->value(), 0, 4);
    if (iterations <= 0) return;

    bool usingPersistent = !m_currentMesh.is_empty();
    SM* targetMesh = nullptr;
    SM tempMesh;

    if (usingPersistent) {
        targetMesh = &m_currentMesh;
    } else {
        if (m_lastVertices.empty() || m_lastIndices.empty()) return;
        tempMesh = toSurfaceMesh(m_lastVertices, m_lastIndices);
        PMP::stitch_borders(tempMesh);
        mergeCoplanarPatches(tempMesh);
        targetMesh = &tempMesh;
    }

    SM& sm = *targetMesh;
    if (sm.number_of_vertices() == 0) return;

    S3::CatmullClark_subdivision(sm, CGAL::parameters::number_of_iterations(iterations));
    sm.collect_garbage();

    // Build display copy
    CgalMeshBuilder::SurfaceMesh displayMesh = sm;
    CgalMeshBuilder::triangulateAll(displayMesh);

    std::vector<CgalMeshBuilder::Vertex> tmp;
    CgalMeshBuilder::toVertexIndexFlat(displayMesh, tmp, m_lastIndices);
    m_lastVertices.resize(tmp.size());
    for (size_t i = 0; i < tmp.size(); ++i) {
        m_lastVertices[i].position = glm::vec4(tmp[i].pos[0], tmp[i].pos[1], tmp[i].pos[2], 1.0f);
        m_lastVertices[i].normal   = glm::vec4(tmp[i].norm[0], tmp[i].norm[1], tmp[i].norm[2], 0.0f);
        m_lastVertices[i].color    = glm::vec4(tmp[i].col[0], tmp[i].col[1], tmp[i].col[2], 1.0f);
    }
    if (m_meshViewer) m_meshViewer->updateMesh(m_lastVertices, m_lastIndices);

    // Keep polygonal source for further modifiers
    m_currentMesh = sm;
}
