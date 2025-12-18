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
#include <QCoreApplication>
#include <QEventLoop>
#include <limits>
#include "MeshViewerWidget.h"
#include "AgentProjectionWindow.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include "CgalMeshBuilder.h"
#include "CgalMeshBuilderTentacles.h"
#include "MeshRepository.h"
#include <unordered_map>
#include <unordered_set>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/stitch_borders.h>
#include <CGAL/Subdivision_method_3/subdivision_methods_3.h>
#include <CGAL/boost/graph/Euler_operations.h>

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
    
    // TABS

    QTabWidget *tabs = new QTabWidget();
    controlLayout->addWidget(tabs);
    
    // --- TAB: Render ---
    QWidget *tabRender = new QWidget();
    QFormLayout *renderLayout = new QFormLayout(tabRender);

    m_lineWidthLabel = new QLabel("Trail Width: 1.0 px");
    m_lineWidthSlider = new QSlider(Qt::Horizontal);
    m_lineWidthSlider->setRange(1, 1000); // 0.1 to 100.0
    m_lineWidthSlider->setValue(10);
    connect(m_lineWidthSlider, &QSlider::valueChanged, this, &MainWindow::onLineWidthChanged);
    renderLayout->addRow(m_lineWidthLabel, m_lineWidthSlider);

    m_brushAlphaLabel = new QLabel("Brush Opacity: 0.80");
    m_brushAlphaSlider = new QSlider(Qt::Horizontal);
    m_brushAlphaSlider->setRange(0, 100);
    m_brushAlphaSlider->setValue(80);
    connect(m_brushAlphaSlider, &QSlider::valueChanged, this, [this](int v){
        float a = v / 100.0f;
        m_brushAlphaLabel->setText(QString("Brush Opacity: %1").arg(a, 0, 'f', 2));
        if (m_squeegeeWindow) m_squeegeeWindow->setBrushAlpha(a);
    });
    renderLayout->addRow(m_brushAlphaLabel, m_brushAlphaSlider);

    m_trailBrightnessLabel = new QLabel("Trail lightness: 1.00");
    m_trailBrightnessSlider = new QSlider(Qt::Horizontal);
    m_trailBrightnessSlider->setRange(10, 200); // 0.1 .. 2.0
    m_trailBrightnessSlider->setValue(100);
    connect(m_trailBrightnessSlider, &QSlider::valueChanged, this, [&](int v){
        float scale = v / 100.0f;
        m_trailBrightnessLabel->setText(QString("Trail lightness: %1").arg(scale, 0, 'f', 2));
        if (m_squeegeeWindow) m_squeegeeWindow->setTrailBrightnessScale(scale);
    });
    renderLayout->addRow(m_trailBrightnessLabel, m_trailBrightnessSlider);

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
    renderLayout->addRow("Palette:", m_paletteCombo);

    m_noiseModeCombo = new QComboBox();
    m_noiseModeCombo->addItem("Noise Off", (int)SqueegeeWindow::NoiseOff);
    m_noiseModeCombo->addItem("Noise -> Brush Intensity", (int)SqueegeeWindow::NoiseBrushIntensity);
    m_noiseModeCombo->addItem("Noise -> Brush Offset (wavy)", (int)SqueegeeWindow::NoiseBrushOffset);
    connect(m_noiseModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onNoiseModeChanged);
    renderLayout->addRow("Brush Noise:", m_noiseModeCombo);

    m_noiseScaleSlider = new QSlider(Qt::Horizontal);
    m_noiseScaleSlider->setRange(10, 400);
    m_noiseScaleSlider->setValue(120);
    m_noiseScaleLabel = new QLabel("120 px");
    connect(m_noiseScaleSlider, &QSlider::valueChanged, this, &MainWindow::onNoiseScaleChanged);
    renderLayout->addRow("Noise Scale:", m_noiseScaleLabel);
    renderLayout->addRow(m_noiseScaleSlider);

    m_noiseStrengthSlider = new QSlider(Qt::Horizontal);
    m_noiseStrengthSlider->setRange(0, 100);
    m_noiseStrengthSlider->setValue(0);
    m_noiseStrengthLabel = new QLabel("0.00");
    connect(m_noiseStrengthSlider, &QSlider::valueChanged, this, &MainWindow::onNoiseStrengthChanged);
    renderLayout->addRow("Noise Strength:", m_noiseStrengthLabel);
    renderLayout->addRow(m_noiseStrengthSlider);

    m_segmentCountSlider = new QSlider(Qt::Horizontal);
    m_segmentCountSlider->setRange(1, 32);
    m_segmentCountSlider->setValue(1);
    m_segmentCountLabel = new QLabel("Segments: 1");
    connect(m_segmentCountSlider, &QSlider::valueChanged, this, &MainWindow::onSegmentCountChanged);
    renderLayout->addRow(m_segmentCountLabel, m_segmentCountSlider);

    m_segmentVisibilitySlider = new QSlider(Qt::Horizontal);
    m_segmentVisibilitySlider->setRange(0, 100);
    m_segmentVisibilitySlider->setValue(100);
    m_segmentVisibilityLabel = new QLabel("Segment fill: 1.00");
    connect(m_segmentVisibilitySlider, &QSlider::valueChanged, this, &MainWindow::onSegmentVisibilityChanged);
    renderLayout->addRow(m_segmentVisibilityLabel, m_segmentVisibilitySlider);

    m_bristleJitterBox = new QCheckBox("Bristle jitter (per strand)");
    m_bristleJitterBox->setChecked(false);
    connect(m_bristleJitterBox, &QCheckBox::toggled, this, [this](bool on){
        m_bristleJitterEnabled = on;
        if (m_bristleJitterSlider) m_bristleJitterSlider->setEnabled(on);
        if (m_bristleJitterLabel) m_bristleJitterLabel->setEnabled(on);
    });
    renderLayout->addRow(m_bristleJitterBox);

    m_bristleJitterLabel = new QLabel("Bristle jitter strength: 0.35");
    m_bristleJitterSlider = new QSlider(Qt::Horizontal);
    m_bristleJitterSlider->setRange(0, 100);
    m_bristleJitterSlider->setValue(35);
    m_bristleJitterSlider->setEnabled(false);
    m_bristleJitterLabel->setEnabled(false);
    connect(m_bristleJitterSlider, &QSlider::valueChanged, this, [this](int v){
        m_bristleJitterStrength = std::clamp(v / 100.0f, 0.0f, 1.0f);
        if (m_bristleJitterLabel) m_bristleJitterLabel->setText(QString("Bristle jitter strength: %1").arg(m_bristleJitterStrength, 0, 'f', 2));
    });
    renderLayout->addRow(m_bristleJitterLabel, m_bristleJitterSlider);

    m_squeegeeLiteBox = new QCheckBox("Squeegee-lite drag (smudge)");
    m_squeegeeLiteBox->setChecked(false);
    connect(m_squeegeeLiteBox, &QCheckBox::toggled, this, [this](bool on){
        m_squeegeeLiteEnabled = on;
        if (m_squeegeeWindow) m_squeegeeWindow->setSqueegeeLiteEnabled(on);
    });
    renderLayout->addRow(m_squeegeeLiteBox);

    m_surfaceBrushBox = new QCheckBox("Surface brush orientation (mesh-aware)");
    m_surfaceBrushBox->setChecked(true);
    connect(m_surfaceBrushBox, &QCheckBox::toggled, this, [this](bool on){
        m_surfaceBrushEnabled = on;
        if (m_surfaceForeshSlider) m_surfaceForeshSlider->setEnabled(on);
        if (m_surfaceForeshLabel) m_surfaceForeshLabel->setEnabled(on);
    });
    renderLayout->addRow(m_surfaceBrushBox);

    m_surfaceForeshLabel = new QLabel("Surface foreshorten: 1.00");
    m_surfaceForeshSlider = new QSlider(Qt::Horizontal);
    m_surfaceForeshSlider->setRange(0, 100); // 0..1
    m_surfaceForeshSlider->setValue(100);
    connect(m_surfaceForeshSlider, &QSlider::valueChanged, this, [this](int v){
        m_surfaceForeshStrength = std::clamp(v / 100.0f, 0.0f, 1.0f);
        if (m_surfaceForeshLabel) m_surfaceForeshLabel->setText(QString("Surface foreshorten: %1").arg(m_surfaceForeshStrength, 0, 'f', 2));
    });
    renderLayout->addRow(m_surfaceForeshLabel, m_surfaceForeshSlider);

    m_noisePreviewLabel = new QLabel();
    m_noisePreviewLabel->setFixedSize(180, 90);
    m_noisePreviewLabel->setFrameStyle(QFrame::Box | QFrame::Plain);
    m_noisePreviewLabel->setAlignment(Qt::AlignCenter);
    m_noisePreviewLabel->setScaledContents(true);
    renderLayout->addRow("Noise Preview:", m_noisePreviewLabel);

    m_sharpenSlider = new QSlider(Qt::Horizontal);
    m_sharpenSlider->setRange(0, 200); // 0.0 - 2.0
    m_sharpenSlider->setValue(0);
    connect(m_sharpenSlider, &QSlider::valueChanged, this, &MainWindow::onSharpenChanged);
    renderLayout->addRow("Sharpen:", m_sharpenSlider);

    QPushButton *bgColorBtn = new QPushButton("Background Color");
    connect(bgColorBtn, &QPushButton::clicked, this, &MainWindow::onBackgroundColorClicked);
    renderLayout->addRow(bgColorBtn);
    tabs->addTab(tabRender, "Render");

    // --- TAB 3D VIEW ---
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

    m_debugNormalsBox = new QCheckBox("Debug normals");
    m_debugNormalsBox->setChecked(false);
    connect(m_debugNormalsBox, &QCheckBox::toggled, this, [this](bool on){
        if (m_meshViewer) m_meshViewer->setDebugNormals(on);
    });
    m_ambientLabel = new QLabel("Ambient: 0.20");
    m_ambientSlider = new QSlider(Qt::Horizontal);
    m_ambientSlider->setRange(0, 200); // 0.0 .. 2.0
    m_ambientSlider->setValue(20);
    connect(m_ambientSlider, &QSlider::valueChanged, this, [this](int v){
        float a = v / 100.0f;
        m_ambientLabel->setText(QString("Ambient: %1").arg(a, 0, 'f', 2));
        if (m_meshViewer) m_meshViewer->setAmbient(a);
    });
    
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(5, 270); // 0.5 to 10.0
    m_zoomSlider->setValue(30); // 3.0
    connect(m_zoomSlider, &QSlider::valueChanged, this, &MainWindow::onZoomChanged);
    
    lightLayout->addRow("Azimuth:", m_lightAzimuthSlider);
    lightLayout->addRow("Elevation:", m_lightElevationSlider);
    lightLayout->addRow(m_ambientLabel, m_ambientSlider);
    lightLayout->addRow(m_debugNormalsBox);
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

    QPushButton *paintZoomStepsBtn = new QPushButton("Paint Zoom Steps Trails");
    connect(paintZoomStepsBtn, &QPushButton::clicked, this, &MainWindow::onPaintZoomStepsTrails);

    QPushButton *clearCanvasBtn = new QPushButton("Clear Canvas");
    connect(clearCanvasBtn, &QPushButton::clicked, this, &MainWindow::onClearCanvas);
    
    m_agentCountLabel = new QLabel("10 agents");
    m_agentCountSlider = new QSlider(Qt::Horizontal);
    m_agentCountSlider->setRange(5, 500);
    m_agentCountSlider->setValue(10);
    connect(m_agentCountSlider, &QSlider::valueChanged, this, &MainWindow::onAgentCountChanged);
    
    m_agentLifetimeLabel = new QLabel("1000 ticks");
    m_agentLifetimeSlider = new QSlider(Qt::Horizontal);
    m_agentLifetimeSlider->setRange(10, 1500);
    m_agentLifetimeSlider->setValue(1000);
    connect(m_agentLifetimeSlider, &QSlider::valueChanged, this, &MainWindow::onAgentLifetimeChanged);

    m_agentSpeedLabel = new QLabel("Speed: 0.05");
    m_agentSpeedSlider = new QSlider(Qt::Horizontal);
    m_agentSpeedSlider->setRange(1, 200); // 0.01 .. 2.00
    m_agentSpeedSlider->setValue(50);     // 0.50 -> 0.05 after scale
    connect(m_agentSpeedSlider, &QSlider::valueChanged, this, &MainWindow::onAgentSpeedChanged);
    
    m_paintZoomCountSlider = new QSlider(Qt::Horizontal);
    m_paintZoomCountSlider->setRange(1, 20);
    m_paintZoomCountSlider->setValue(5);
    m_paintZoomCountLabel = new QLabel("Zoom Steps: 5");
    connect(m_paintZoomCountSlider, &QSlider::valueChanged, this, [this](int v){
        m_paintZoomCountLabel->setText(QString("Zoom Steps: %1").arg(v));
    });

    m_paintZoomStepSlider = new QSlider(Qt::Horizontal);
    m_paintZoomStepSlider->setRange(-100, 100); // -100% .. +100% of current zoom
    m_paintZoomStepSlider->setValue(10);        // +10% default
    m_paintZoomStepLabel = new QLabel("Zoom Step: +10%");
    connect(m_paintZoomStepSlider, &QSlider::valueChanged, this, [this](int v){
        m_paintZoomStepLabel->setText(QString("Zoom Step: %1%").arg(v));
    });

    m_randomTrailColorBox = new QCheckBox("Random trail color per step");
    connect(m_randomTrailColorBox, &QCheckBox::toggled, this, &MainWindow::onRandomTrailColorsToggled);
    
    agentLayout->addRow("Simulation:", m_agentButton);
    agentLayout->addRow(projectAgentsBtn);
    agentLayout->addRow(paintTrailsBtn);
    agentLayout->addRow(paintZoomStepsBtn);
    agentLayout->addRow(clearCanvasBtn);
    agentLayout->addRow(m_agentCountLabel);
    agentLayout->addRow("Count:", m_agentCountSlider);
    agentLayout->addRow(m_agentLifetimeLabel);
    agentLayout->addRow("Lifetime:", m_agentLifetimeSlider);
    agentLayout->addRow(m_agentSpeedLabel);
    agentLayout->addRow("Speed:", m_agentSpeedSlider);
    agentLayout->addRow(m_paintZoomCountLabel, m_paintZoomCountSlider);
    agentLayout->addRow(m_paintZoomStepLabel, m_paintZoomStepSlider);
    agentLayout->addRow(m_randomTrailColorBox);


    
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
    m_primitiveCombo->addItem("Cube");
    
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

    m_cubeQuadLabel = new QLabel("Quad size: 1.0");
    m_cubeQuadSlider = new QSlider(Qt::Horizontal);
    m_cubeQuadSlider->setRange(1, 100); // 0.1 .. 10.0
    m_cubeQuadSlider->setValue(10);
    connect(m_cubeQuadSlider, &QSlider::valueChanged, this, [this](int v){
        m_cubeQuadLabel->setText(QString("Quad size: %1").arg(v / 10.0f, 0, 'f', 1));
    });
    genForm->addRow(m_cubeQuadLabel, m_cubeQuadSlider);
    
    m_generateMeshBtn = new QPushButton("Generate Mesh");
    connect(m_generateMeshBtn, &QPushButton::clicked, this, &MainWindow::onGenerateMesh);
    m_clearMeshesBtn = new QPushButton("Delete All Meshes");
    connect(m_clearMeshesBtn, &QPushButton::clicked, this, &MainWindow::onClearAllMeshes);
    m_addPlaneBtn = new QPushButton("Add Backdrop Plane");
    connect(m_addPlaneBtn, &QPushButton::clicked, this, &MainWindow::onAddPlaneBehindSphere);

    // Plane controls
    m_planeSizeSlider = new QSlider(Qt::Horizontal);
    m_planeSizeSlider->setRange(50, 2000); // 5.0 .. 200.0 units
    m_planeSizeSlider->setValue(400);      // 40.0 default
    m_planeSizeLabel = new QLabel("Plane size: 40.0");
    connect(m_planeSizeSlider, &QSlider::valueChanged, this, [this](int v){
        m_planeSizeLabel->setText(QString("Plane size: %1").arg(v / 10.0, 0, 'f', 1));
    });

    m_planeSubdivXSlider = new QSlider(Qt::Horizontal);
    m_planeSubdivXSlider->setRange(1, 128);
    m_planeSubdivXSlider->setValue(15);
    m_planeSubdivXLabel = new QLabel("Subdiv X: 15");
    connect(m_planeSubdivXSlider, &QSlider::valueChanged, this, [this](int v){
        m_planeSubdivXLabel->setText(QString("Subdiv X: %1").arg(v));
    });

    m_planeSubdivYSlider = new QSlider(Qt::Horizontal);
    m_planeSubdivYSlider->setRange(1, 128);
    m_planeSubdivYSlider->setValue(15);
    m_planeSubdivYLabel = new QLabel("Subdiv Y: 15");
    connect(m_planeSubdivYSlider, &QSlider::valueChanged, this, [this](int v){
        m_planeSubdivYLabel->setText(QString("Subdiv Y: %1").arg(v));
    });

    m_planeDepthSlider = new QSlider(Qt::Horizontal);
    m_planeDepthSlider->setRange(10, 300); // percent of diameter
    m_planeDepthSlider->setValue(100);     // 100% of diameter
    m_planeDepthLabel = new QLabel("Depth: 1.00x diameter");
    connect(m_planeDepthSlider, &QSlider::valueChanged, this, [this](int v){
        m_planeDepthLabel->setText(QString("Depth: %1x diameter").arg(v / 100.0, 0, 'f', 2));
    });

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
    tabGenLayout->addWidget(m_clearMeshesBtn);
    tabGenLayout->addWidget(m_planeSizeLabel);
    tabGenLayout->addWidget(m_planeSizeSlider);
    tabGenLayout->addWidget(m_planeSubdivXLabel);
    tabGenLayout->addWidget(m_planeSubdivXSlider);
    tabGenLayout->addWidget(m_planeSubdivYLabel);
    tabGenLayout->addWidget(m_planeSubdivYSlider);
    tabGenLayout->addWidget(m_planeDepthLabel);
    tabGenLayout->addWidget(m_planeDepthSlider);
    tabGenLayout->addWidget(m_addPlaneBtn);
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

    m_tentacleRotationSlider = new QSlider(Qt::Horizontal);
    m_tentacleRotationSlider->setRange(-180, 180); // degrees per step
    m_tentacleRotationSlider->setValue(0);
    m_tentacleRotationLabel = new QLabel("Section Rotation: 0 deg");
    connect(m_tentacleRotationSlider, &QSlider::valueChanged, this, [this](int v){
        m_tentacleRotationLabel->setText(QString("Section Rotation: %1 deg").arg(v));
    });
    modForm->addRow(m_tentacleRotationLabel, m_tentacleRotationSlider);

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
    connect(m_meshViewer, &MeshViewerWidget::cameraDistanceChanged, this, [this](float dist){
        if (!m_zoomSlider) return;
        int sliderVal = static_cast<int>(std::round(dist * 10.0f));
        QSignalBlocker b(m_zoomSlider);
        m_zoomSlider->setValue(sliderVal);
    });
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

    // Main loop orchestrator
    m_mainLoop = new MainLoop(this);
    m_mainLoop->setMeshViewer(m_meshViewer);
    m_mainLoop->setAgentWindow(m_agentWindow);
    m_mainLoop->setAgentCount(m_agentCountSlider->value());
    m_mainLoop->setAgentLifetime(m_agentLifetimeSlider->value());
    m_mainLoop->setLineWidth(m_lineWidthSlider->value() / 10.0f);
    m_mainLoop->setRandomTrailColors(false);
    
    mainLayout->addWidget(m_mdiArea, 1);

    // Initial values
    m_squeegeeWindow->setGenAngle(45.0f);
    m_squeegeeWindow->setGenWidth(2.0f);
    m_squeegeeWindow->setGenPasses(1);
    m_squeegeeWindow->setGenSteps(600);
    if (m_brushAlphaSlider) {
        float a = m_brushAlphaSlider->value() / 100.0f;
        m_squeegeeWindow->setBrushAlpha(a);
    }
    if (m_squeegeeLiteBox) {
        m_squeegeeWindow->setSqueegeeLiteEnabled(m_squeegeeLiteBox->isChecked());
    }
    if (m_surfaceBrushBox) {
        m_surfaceBrushEnabled = m_surfaceBrushBox->isChecked();
    }
    if (m_surfaceForeshSlider) {
        m_surfaceForeshStrength = std::clamp(m_surfaceForeshSlider->value() / 100.0f, 0.0f, 1.0f);
        if (m_surfaceForeshLabel) m_surfaceForeshLabel->setText(QString("Surface foreshorten: %1").arg(m_surfaceForeshStrength, 0, 'f', 2));
        m_surfaceForeshSlider->setEnabled(m_surfaceBrushEnabled);
        if (m_surfaceForeshLabel) m_surfaceForeshLabel->setEnabled(m_surfaceBrushEnabled);
    }
    if (m_bristleJitterSlider) {
        m_bristleJitterStrength = std::clamp(m_bristleJitterSlider->value() / 100.0f, 0.0f, 1.0f);
        if (m_bristleJitterLabel) m_bristleJitterLabel->setText(QString("Bristle jitter strength: %1").arg(m_bristleJitterStrength, 0, 'f', 2));
        m_bristleJitterSlider->setEnabled(m_bristleJitterEnabled);
        if (m_bristleJitterLabel) m_bristleJitterLabel->setEnabled(m_bristleJitterEnabled);
    }
    if (m_ambientSlider) {
        float a = m_ambientSlider->value() / 100.0f;
        if (m_meshViewer) m_meshViewer->setAmbient(a);
    }
    if (m_debugNormalsBox && m_meshViewer) {
        m_meshViewer->setDebugNormals(m_debugNormalsBox->isChecked());
    }
    onPrimitiveChanged(m_primitiveCombo->currentIndex());
    onNoiseModeChanged(m_noiseModeCombo->currentIndex());
    onNoiseScaleChanged(m_noiseScaleSlider->value());
    onNoiseStrengthChanged(m_noiseStrengthSlider->value());
    onSegmentCountChanged(m_segmentCountSlider->value());
    onSegmentVisibilityChanged(m_segmentVisibilitySlider->value());
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
    
    if (index == 1) { // Cube
        p1 = "Quads X (1-200)";
        p2 = "Quads Y (1-200)";
        p3 = "Quads Z (1-200)";
        m_primParam1Slider->setRange(1, 200); m_primParam1Slider->setValue(10);
        m_primParam2Slider->setRange(1, 200); m_primParam2Slider->setValue(10);
        m_primParam3Slider->setRange(1, 200); m_primParam3Slider->setValue(10);
        m_primParam3Slider->setEnabled(true);
        if (m_cubeQuadSlider) m_cubeQuadSlider->setEnabled(true);
        if (m_cubeQuadLabel) m_cubeQuadLabel->setEnabled(true);
    } else { // HexSphere default
        p1 = "Resolution (1-5)";
        m_primParam1Slider->setRange(1, 5);m_primParam1Slider->setValue(2);
        p2 = "Radius (0.1-5.0)";
        m_primParam2Slider->setRange(1, 50);m_primParam2Slider->setValue(10);
        p3 = "N/A"; m_primParam3Slider->setEnabled(false);
        if (m_cubeQuadSlider) m_cubeQuadSlider->setEnabled(false);
        if (m_cubeQuadLabel) m_cubeQuadLabel->setEnabled(false);
    }
    
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
    if (m_angleSnapCheckBox && m_angleSnapCheckBox->isChecked()) {
        int snapped = qRound(value / 45.0) * 45;
        if (snapped != value) {
            if (m_angleSlider) m_angleSlider->setValue(snapped);
            return;
        }
    }
    if (m_angleLabel) m_angleLabel->setText(QString::number(value) + " deg");
    m_squeegeeWindow->setGenAngle((float)value);
}

void MainWindow::onWidthChanged(int value)
{
    if (m_widthLabel) m_widthLabel->setText(QString::number(value) + "%");
    m_squeegeeWindow->setGenWidth((float)value / 100.0f);
}

void MainWindow::onPassesChanged(int value)
{
    if (m_passesLabel) m_passesLabel->setText(QString::number(value));
    m_squeegeeWindow->setGenPasses(value);
}

void MainWindow::onStepsChanged(int value)
{
    if (m_stepsLabel) m_stepsLabel->setText(QString("Simulation Steps: %1 steps").arg(value));
    
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
    if (m_stepsLabel) m_stepsLabel->setText(QString("%1 steps").arg(snapped));

    // Update Fine Slider (approximate sync)
    // The fine slider is 1-25. If coarse is moved, we might reset fine or leave it?
    // Let's just update the label and backend first to fix the immediate bug.
    
    m_squeegeeWindow->setGenSteps(snapped);
}

void MainWindow::onDensityChanged(int value)
{
    if (m_densityLabel) m_densityLabel->setText(QString("Density: %1").arg(value));
    m_squeegeeWindow->setGenDensity(value);
}

// onSizeChanged removed from here as it is defined below


void MainWindow::onSizeChanged(int value)
{
    if (m_sizeLabel) m_sizeLabel->setText(QString::number(value) + " px");
    m_squeegeeWindow->setDropMaxSize(value);
}

void MainWindow::onMinSizeRatioChanged(int value)
{
    float ratio = value / 100.0f;
    if (m_minSizeRatioLabel) m_minSizeRatioLabel->setText(QString("Min Size: %1%").arg(value));
    m_squeegeeWindow->setMinSizeRatio(ratio);
}

void MainWindow::onConcentricChanged(int value)
{
    if (m_concentricLabel) m_concentricLabel->setText(QString::number(value));
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
    if (checked && m_angleSlider) {
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

void MainWindow::onSegmentCountChanged(int value)
{
    int clamped = std::clamp(value, 1, 32);
    if (clamped != value) {
        m_segmentCountSlider->blockSignals(true);
        m_segmentCountSlider->setValue(clamped);
        m_segmentCountSlider->blockSignals(false);
    }
    m_segmentCountLabel->setText(QString("Segments: %1").arg(clamped));
    if (m_squeegeeWindow) m_squeegeeWindow->setBrushSegments(clamped);
}

void MainWindow::onSegmentVisibilityChanged(int value)
{
    int clamped = std::clamp(value, 0, 100);
    if (clamped != value) {
        m_segmentVisibilitySlider->blockSignals(true);
        m_segmentVisibilitySlider->setValue(clamped);
        m_segmentVisibilitySlider->blockSignals(false);
    }
    float v = clamped / 100.0f;
    m_segmentVisibilityLabel->setText(QString("Segment fill: %1").arg(v, 0, 'f', 2));
    if (m_squeegeeWindow) m_squeegeeWindow->setSegmentVisibility(v);
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
    if (m_mainLoop) {
        if (run) m_mainLoop->start();
        else m_mainLoop->stop();
    }
}

void MainWindow::onAgentCountChanged(int value)
{
    m_agentCountLabel->setText(QString("%1 agents").arg(value));
    if (m_mainLoop) m_mainLoop->setAgentCount(value);
}

void MainWindow::onAgentLifetimeChanged(int value)
{
    m_agentLifetimeLabel->setText(QString("%1 ticks").arg(value));
    if (m_mainLoop) m_mainLoop->setAgentLifetime(value);
}

void MainWindow::onAgentSpeedChanged(int value)
{
    // Slider 1..200 -> 0.01 .. 2.00
    float speed = value / 1000.0f;
    m_agentSpeedLabel->setText(QString("Speed: %1").arg(speed, 0, 'f', 3));
    if (m_meshViewer) m_meshViewer->setAgentBaseSpeed(speed);
}

void MainWindow::onRandomTrailColorsToggled(bool checked)
{
    m_randomTrailStepColors = checked;
    if (m_mainLoop) m_mainLoop->setRandomTrailColors(checked);
}

void MainWindow::onLineWidthChanged(int value)
{
    float width = value / 10.0f;
    m_lineWidthLabel->setText(QString("Trail Width: %1 px").arg(width, 0, 'f', 1));
    if(m_mainLoop) m_mainLoop->setLineWidth(width);
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
    std::sort(agents.begin(), agents.end(), [](const auto& a, const auto& b){
        return a.viewDepth < b.viewDepth; // far first, near last
    });

    // Clear previous projection so each press reflects the current agent set only.
    m_squeegeeWindow->clearCanvas();

    // Drops Only (Project Agents)
    QVector<SqueegeeWindow::DropInfo> drops;
    // Scale drop size along with coordinates so brush canvas matches the projection view.
    float dropSize = m_sizeSlider ? (m_sizeSlider->value() * scale) : (10.0f * scale);
    for (const auto& a : agents) {
        float px = a.screenPos.x() * scale + offsetX;
        float py = a.screenPos.y() * scale + offsetY; // pass camera-space Y; painter will flip
        
        if (a.isVisible && px >= 0 && px < canvasW && py >= 0 && py < canvasH) {
            SqueegeeWindow::DropInfo info;
            // spawnDrops (CPU) expects Raw GL Y.
            info.pos = QVector2D(px, py);
            info.color = a.color;
            float f = std::clamp(a.headForeshorten, 0.05f, 1.0f);
            float fUsed = m_surfaceBrushEnabled ? ((1.0f - m_surfaceForeshStrength) + m_surfaceForeshStrength * f) : 1.0f;
            info.size = dropSize * fUsed;
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
    auto randomColor = []() {
        return QColor::fromRgbF(
            QRandomGenerator::global()->generateDouble(),
            QRandomGenerator::global()->generateDouble(),
            QRandomGenerator::global()->generateDouble());
    };
    auto buildPerp = [](const std::vector<QVector2D>& pts) {
        std::vector<QVector2D> perps(pts.size(), QVector2D(0, 0));
        for (int i = 0; i < (int)pts.size(); ++i) {
            QVector2D prev = pts[std::max(0, i - 1)];
            QVector2D next = pts[std::min<int>(pts.size() - 1, i + 1)];
            QVector2D t = next - prev;
            if (t.lengthSquared() < 1e-6f) t = QVector2D(1.0f, 0.0f);
            t.normalize();
            perps[i] = QVector2D(-t.y(), t.x());
        }
        return perps;
    };

    int segCount = std::max(1, m_segmentCountSlider ? m_segmentCountSlider->value() : 1);
    float fillProb = m_segmentVisibilitySlider ? (m_segmentVisibilitySlider->value() / 100.0f) : 1.0f;

    for (int ai = 0; ai < agents.size(); ++ai) {
        const auto& a = agents[ai];
        // Keep trails even if the current head is occluded; visibility was already checked per-point when segments were built.
        if (a.trailSegments.empty()) continue;
            
        for (int si = 0; si < a.trailSegments.size(); ++si) {
             const auto& seg = a.trailSegments[si];
             if (seg.size() < 2) continue;
             const std::vector<float>* foreshPtr = (si < a.trailForeshorten.size()) ? &a.trailForeshorten[si] : nullptr;
             const std::vector<QVector2D>* widthDirPtr = (si < a.trailWidthDirs.size()) ? &a.trailWidthDirs[si] : nullptr;
             float foreshAvg = 1.0f;
             if (foreshPtr && !foreshPtr->empty()) {
                 float acc = 0.0f;
                 for (float v : *foreshPtr) acc += v;
                 foreshAvg = std::clamp(acc / (float)foreshPtr->size(), 0.05f, 1.0f);
             }
             float lw = std::max(0.1f, m_lineWidthSlider->value() / 10.0f);
             float perSegmentWidth = std::max(0.1f, lw / (float)segCount);
             // Prefer projected surface width directions if enabled; fallback to 2D perpendiculars.
             std::vector<QVector2D> perps;
             if (m_surfaceBrushEnabled && widthDirPtr && widthDirPtr->size() == seg.size()) {
                 perps.assign(widthDirPtr->begin(), widthDirPtr->end());
             } else {
                 perps = buildPerp(seg);
             }

             auto applyOffsetAndAppend = [&](const QColor& baseColor, const std::vector<float>* brightnessSrc) {
                 auto bandCenter = [&](int k) {
                     return (-0.5f + (k + 0.5f) / (float)segCount) * lw;
                 };

                 struct BandState {
                     bool open = false;
                     SqueegeeWindow::PathInfo path;
                 };
                 std::vector<BandState> bands(segCount);

                 auto seedFor = [&](int k) -> quint32 {
                     // Stable-ish seed per band/segment/layer so jitter doesn't change within one paint call.
                     quint32 s = 2166136261u;
                     s ^= (quint32)ai     + 0x9e3779b9u + (s<<6) + (s>>2);
                     s ^= (quint32)a.layer + 0x9e3779b9u + (s<<6) + (s>>2);
                     s ^= (quint32)si      + 0x9e3779b9u + (s<<6) + (s>>2);
                     s ^= (quint32)k       + 0x9e3779b9u + (s<<6) + (s>>2);
                     return s;
                 };

                 std::vector<std::vector<QVector2D>> jitterPts;
                 std::vector<float> widthMul(segCount, 1.0f);
                 std::vector<float> alphaMul(segCount, 1.0f);
                 if (m_bristleJitterEnabled && m_bristleJitterStrength > 0.0f) {
                     jitterPts.resize(segCount);
                     for (int k = 0; k < segCount; ++k) {
                         QRandomGenerator gen(seedFor(k));
                         float wRand = 0.75f + 0.55f * (float)gen.generateDouble(); // 0.75..1.30
                         float aRand = 0.35f + 0.55f * (float)gen.generateDouble(); // 0.35..0.90
                         widthMul[k] = (1.0f - m_bristleJitterStrength) + m_bristleJitterStrength * wRand;
                         alphaMul[k] = (1.0f - m_bristleJitterStrength) + m_bristleJitterStrength * aRand;
                         jitterPts[k].resize(seg.size(), QVector2D(0, 0));
                         // Jitter amplitude scales with line width, but keep it low-frequency along the stroke
                         // so it reads like "bristles" instead of noisy zig-zag.
                         float amp = (0.12f * lw) * m_bristleJitterStrength;
                         float ampAlong = (0.03f * lw) * m_bristleJitterStrength;
                         float smooth = std::clamp(0.04f + 0.10f * m_bristleJitterStrength, 0.02f, 0.20f);
                         int stride = std::max(1, (int)std::lround(3.0f + 6.0f * (1.0f - m_bristleJitterStrength)));
                         QVector2D accum(0, 0);
                         QVector2D target(0, 0);
                         for (int pi = 0; pi < (int)seg.size(); ++pi) {
                             if (pi % stride == 0) {
                                 float jn = (float)(gen.generateDouble() * 2.0 - 1.0);
                                 float jt = (float)(gen.generateDouble() * 2.0 - 1.0);
                                 QVector2D perp = perps[pi];
                                 QVector2D tan(-perp.y(), perp.x());
                                 target = perp * (jn * amp) + tan * (jt * ampAlong);
                             }
                             accum = accum * (1.0f - smooth) + target * smooth;
                             jitterPts[k][pi] = accum;
                         }
                     }
                 }

                 for (int pi = 1; pi < (int)seg.size(); ++pi) {
                     // сэмпл активных полос на каждом шаге
                     std::vector<int> activeBands(segCount, 1);
                     if (fillProb < 0.999f) {
                         for (int k = 0; k < segCount; ++k) {
                             double r = QRandomGenerator::global()->generateDouble();
                             activeBands[k] = (r <= fillProb) ? 1 : 0;
                         }
                         if (std::accumulate(activeBands.begin(), activeBands.end(), 0) == 0) {
                             int idx = QRandomGenerator::global()->bounded(segCount);
                             activeBands[idx] = 1;
                         }
                     }

                     for (int k = 0; k < segCount; ++k) {
                         if (!activeBands[k]) continue;
                         float bc = bandCenter(k);
                         float f0 = foreshPtr && (pi - 1) < (int)foreshPtr->size() ? (*foreshPtr)[pi - 1] : foreshAvg;
                         float f1 = foreshPtr && pi < (int)foreshPtr->size() ? (*foreshPtr)[pi] : foreshAvg;
                         if (m_surfaceBrushEnabled) {
                             f0 = (1.0f - m_surfaceForeshStrength) + m_surfaceForeshStrength * f0;
                             f1 = (1.0f - m_surfaceForeshStrength) + m_surfaceForeshStrength * f1;
                         } else {
                             f0 = 1.0f;
                             f1 = 1.0f;
                         }
                         // Brush footprint: if surface mode is on, perps[] is mesh-aware orientation; otherwise it's screen-perp.
                         QVector2D p0 = seg[pi - 1] + perps[pi - 1] * (bc * (m_surfaceBrushEnabled ? f0 : 1.0f));
                         QVector2D p1 = seg[pi]     + perps[pi]     * (bc * (m_surfaceBrushEnabled ? f1 : 1.0f));
                         if (m_bristleJitterEnabled) {
                             p0 += jitterPts[k][pi - 1];
                             p1 += jitterPts[k][pi];
                         }
                         float px0 = p0.x() * scale + offsetX;
                         float py0 = p0.y() * scale + offsetY;
                         float px1 = p1.x() * scale + offsetX;
                         float py1 = p1.y() * scale + offsetY;
                         float b0 = 1.0f;
                         float b1 = 1.0f;
                         if (brightnessSrc) {
                             if (pi - 1 < brightnessSrc->size()) b0 = (*brightnessSrc)[pi - 1];
                             if (pi < brightnessSrc->size())     b1 = (*brightnessSrc)[pi];
                         }

                         auto& st = bands[k];
                         if (!st.open) {
                             st.open = true;
                             st.path = SqueegeeWindow::PathInfo{};
                             QColor c = baseColor;
                             if (m_bristleJitterEnabled) c.setAlphaF(std::clamp(alphaMul[k], 0.0f, 1.0f));
                             st.path.color = c;
                             float widthScale = 1.0f;
                             if (m_surfaceBrushEnabled) {
                                 widthScale = (1.0f - m_surfaceForeshStrength) + m_surfaceForeshStrength * foreshAvg;
                             }
                             st.path.size = perSegmentWidth * widthScale * (m_bristleJitterEnabled ? widthMul[k] : 1.0f);
                             st.path.useQtPainter = true;
                             st.path.layer = a.layer;
                             st.path.points.append(QVector2D(px0, py0));
                             st.path.brightnessPerPoint.append(b0);
                         }
                         st.path.points.append(QVector2D(px1, py1));
                         st.path.brightnessPerPoint.append(b1);
                     }

                     // Close any bands that were not active at this step (so gaps appear).
                     for (int k = 0; k < segCount; ++k) {
                         if (activeBands[k]) continue;
                         auto& st = bands[k];
                         if (st.open) {
                             if (st.path.points.size() >= 2) paths.append(st.path);
                             st.open = false;
                         }
                     }
                 }

                 // Flush open bands.
                 for (int k = 0; k < segCount; ++k) {
                     auto& st = bands[k];
                     if (st.open) {
                         if (st.path.points.size() >= 2) paths.append(st.path);
                         st.open = false;
                     }
                 }
             };

             const auto* bsegPtr = (si < a.trailBrightness.size()) ? &a.trailBrightness[si] : nullptr;
             QColor useColor = m_randomTrailStepColors ? randomColor() : a.color;
             applyOffsetAndAppend(useColor, bsegPtr);
        }
    }
    m_squeegeeWindow->paintPaths(paths);
}

void MainWindow::onPaintZoomStepsTrails()
{
    if (!m_zoomSlider || !m_paintZoomCountSlider || !m_paintZoomStepSlider) return;

    const int originalZoom = m_zoomSlider->value();
    const int steps = std::max(1, m_paintZoomCountSlider->value());
    const double stepPercent = m_paintZoomStepSlider->value() / 100.0; // relative to current zoom
    const int minZoom = m_zoomSlider->minimum();
    const int maxZoom = m_zoomSlider->maximum();

    // Precompute zoom sequence so the count is exact even if clamped.
    QVector<int> zoomSequence;
    zoomSequence.reserve(steps);

    int currentZoom = originalZoom;
    for (int i = 0; i < steps; ++i) {
        int delta = static_cast<int>(std::round(currentZoom * stepPercent));
        if (delta == 0 && stepPercent != 0.0) {
            delta = (stepPercent > 0.0) ? 1 : -1; // guarantee progress
        }
        int target = std::clamp(currentZoom + delta, minZoom, maxZoom);
        zoomSequence.push_back(target);
        currentZoom = target;
    }

    for (int z : zoomSequence) {
        if (m_zoomSlider->value() != z) {
            m_zoomSlider->setValue(z); // triggers onZoomChanged
        } else {
            onZoomChanged(z); // ensure camera updates even if unchanged
        }

        onPaintTrails();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    if (m_zoomSlider->value() != originalZoom) {
        m_zoomSlider->setValue(originalZoom);
    }
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
    
    SM polyMesh;
    if (m_primitiveCombo->currentIndex() == 1) { // Cube
        int segX = std::max(1, m_primParam1Slider->value());
        int segY = std::max(1, m_primParam2Slider->value());
        int segZ = std::max(1, m_primParam3Slider->value());
        float quadSize = m_cubeQuadSlider ? std::max(0.1f, m_cubeQuadSlider->value() / 10.0f) : 1.0f;
        float sizeX = quadSize * segX;
        float sizeY = quadSize * segY;
        float sizeZ = quadSize * segZ;

        auto addPatch = [&](const glm::vec3& origin, const glm::vec3& uDir, const glm::vec3& vDir, int nu, int nv) {
            std::vector<SM::Vertex_index> grid;
            grid.reserve((nu + 1) * (nv + 1));
            for (int j = 0; j <= nv; ++j) {
                for (int i = 0; i <= nu; ++i) {
                    glm::vec3 p = origin + uDir * (float)i + vDir * (float)j;
                    grid.push_back(polyMesh.add_vertex(SM::Point(p.x, p.y, p.z)));
                }
            }
            auto idx = [&](int i, int j) { return grid[j * (nu + 1) + i]; };
            for (int j = 0; j < nv; ++j) {
                for (int i = 0; i < nu; ++i) {
                    std::vector<SM::Vertex_index> quad = {
                        idx(i, j),
                        idx(i + 1, j),
                        idx(i + 1, j + 1),
                        idx(i, j + 1)
                    };
                    CGAL::Euler::add_face(quad, polyMesh);
                }
            }
        };

        float hx = sizeX * 0.5f;
        float hy = sizeY * 0.5f;
        float hz = sizeZ * 0.5f;

        // +X
        addPatch(glm::vec3(hx, -hy, -hz), glm::vec3(0, sizeY / segY, 0), glm::vec3(0, 0, sizeZ / segZ), segY, segZ);
        // -X
        addPatch(glm::vec3(-hx, -hy, hz), glm::vec3(0, sizeY / segY, 0), glm::vec3(0, 0, -sizeZ / segZ), segY, segZ);
        // +Y
        addPatch(glm::vec3(-hx, hy, -hz), glm::vec3(0, 0, sizeZ / segZ), glm::vec3(sizeX / segX, 0, 0), segZ, segX);
        // -Y
        addPatch(glm::vec3(-hx, -hy, hz), glm::vec3(0, 0, -sizeZ / segZ), glm::vec3(sizeX / segX, 0, 0), segZ, segX);
        // +Z
        addPatch(glm::vec3(-hx, -hy, hz), glm::vec3(sizeX / segX, 0, 0), glm::vec3(0, sizeY / segY, 0), segX, segY);
        // -Z
        addPatch(glm::vec3(hx, -hy, -hz), glm::vec3(-sizeX / segX, 0, 0), glm::vec3(0, sizeY / segY, 0), segX, segY);
    } else {
        // Params (reuse UI sliders for resolution/radius)
        const int   resolution = m_primParam1Slider->value();
        const float radius     = m_primParam2Slider->value() / 10.0f;
        
        CgalMeshBuilder::buildHexSphereOriented(
            polyMesh,
            resolution,
            radius,
            CgalMeshBuilder::Point_3(0,0,0),
            CgalMeshBuilder::Vector_3(0,1,0),
            0.0
        );
    }

    polyMesh.collect_garbage();

    // Store in repository with default color
    m_meshRepo.clear();
    m_activeMeshIndex = m_meshRepo.addMesh(polyMesh, Qt::white);
    auto* entry = m_meshRepo.active();
    if (!entry) return;
    m_meshRepo.rebuildRenderData(*entry);
    m_meshRepo.buildFlattened(vertices, indices);

    m_lastVertices = vertices;
    m_lastIndices = indices;
    m_meshViewer->updateMesh(vertices, indices);
}

void MainWindow::onClearAllMeshes()
{
    m_meshRepo.clear();
    m_activeMeshIndex = -1;
    m_lastVertices.clear();
    m_lastIndices.clear();
    if (m_meshViewer) {
        m_meshViewer->clearAgents();
        m_meshViewer->updateMesh(m_lastVertices, m_lastIndices);
    }
}

void MainWindow::onAddPlaneBehindSphere()
{
    // Derive a reasonable plane size/depth from current mesh bounds.
    glm::vec3 minB(std::numeric_limits<float>::max());
    glm::vec3 maxB(-std::numeric_limits<float>::max());
    if (!m_lastVertices.empty()) {
        for (const auto& v : m_lastVertices) {
            glm::vec3 p(v.position);
            minB = glm::min(minB, p);
            maxB = glm::max(maxB, p);
        }
    } else {
        minB = glm::vec3(-1.0f);
        maxB = glm::vec3(1.0f);
    }
    glm::vec3 extent = glm::abs(maxB - minB);
    float sceneRadius = 0.5f * std::max(std::max(extent.x, extent.y), extent.z);
    sceneRadius = std::max(sceneRadius, 1.0f);
    float planeSize = m_planeSizeSlider ? (m_planeSizeSlider->value() / 10.0f) : (sceneRadius * 4.0f);
    int subdivX = m_planeSubdivXSlider ? std::max(1, m_planeSubdivXSlider->value()) : 15;
    int subdivY = m_planeSubdivYSlider ? std::max(1, m_planeSubdivYSlider->value()) : 15;
    float depthFactor = m_planeDepthSlider ? (m_planeDepthSlider->value() / 100.0f) : 1.0f;
    float planeZ = minB.z - std::max(0.1f, sceneRadius * 2.0f * depthFactor);

    SM plane;
    // Build a grid (subdivX x subdivY) as quads split into two triangles each.
    std::vector<SM::Vertex_index> grid;
    grid.reserve((subdivX + 1) * (subdivY + 1));
    for (int iy = 0; iy <= subdivY; ++iy) {
        float fy = -planeSize + (2.0f * planeSize) * (float)iy / (float)subdivY;
        for (int ix = 0; ix <= subdivX; ++ix) {
            float fx = -planeSize + (2.0f * planeSize) * (float)ix / (float)subdivX;
            grid.push_back(plane.add_vertex(SM::Point(fx, fy, planeZ)));
        }
    }
    auto idx = [&](int x, int y) { return grid[y * (subdivX + 1) + x]; };
    for (int y = 0; y < subdivY; ++y) {
        for (int x = 0; x < subdivX; ++x) {
            auto v00 = idx(x, y);
            auto v10 = idx(x + 1, y);
            auto v11 = idx(x + 1, y + 1);
            auto v01 = idx(x, y + 1);
            std::vector<SM::Vertex_index> quad = {v00, v10, v11, v01};
            CGAL::Euler::add_face(quad, plane); // keep as quad; triangulated later for rendering
        }
    }
    plane.collect_garbage();

    int prevActive = m_meshRepo.activeIndex();
    QColor planeColor = QColor::fromRgbF(0.12f, 0.12f, 0.15f);
    m_meshRepo.addMesh(plane, planeColor);
    if (prevActive >= 0 && prevActive < m_meshRepo.size()) {
        m_meshRepo.setActiveIndex(prevActive); // keep sphere active for edits
    }

    // Rebuild flattened buffers with the new plane included.
    m_meshRepo.buildFlattened(m_lastVertices, m_lastIndices);
    if (m_meshViewer) m_meshViewer->updateMesh(m_lastVertices, m_lastIndices);
}

// ... (skipping unchanged methods) ...

void MainWindow::onExtrudeRandom()
{
    // If we have a valid Cgal mesh (e.g. HexSphere), use it directly to keep polygons.
    // Otherwise, rebuild from triangles (legacy behavior).
    
    bool usingPersistent = m_meshRepo.active() != nullptr;
    SM* targetMesh = nullptr;
    SM tempMesh; // Only used if rebuilding
    
    if (usingPersistent) {
        targetMesh = &m_meshRepo.active()->polyMesh;
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
        
        if (!usingPersistent) {
            m_activeMeshIndex = m_meshRepo.addMesh(*targetMesh, Qt::white);
        }
        auto* entry = m_meshRepo.active();
        if (entry) {
            entry->polyMesh = *targetMesh;
            m_meshRepo.rebuildRenderData(*entry);
            m_meshRepo.buildFlattened(m_lastVertices, m_lastIndices);
            if (m_meshViewer) m_meshViewer->updateMesh(m_lastVertices, m_lastIndices);
        }
    }
}

void MainWindow::onGrowTentacles()
{
    bool usingPersistent = m_meshRepo.active() != nullptr;
    SM* targetMesh = nullptr;
    SM tempMesh;

    if (usingPersistent) {
        targetMesh = &m_meshRepo.active()->polyMesh;
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
    const double degToRad = 3.14159265358979323846 / 180.0;
    const double sectionTwistRad = m_tentacleRotationSlider->value() * degToRad;
    gp.twistMinRad = gp.twistMaxRad = sectionTwistRad; // fixed rotation per step
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
    if (!usingPersistent) {
        m_activeMeshIndex = m_meshRepo.addMesh(sm, Qt::white);
    }
    auto* entry = m_meshRepo.active();
    if (entry) {
        entry->polyMesh = sm;
        m_meshRepo.rebuildRenderData(*entry);
        m_meshRepo.buildFlattened(m_lastVertices, m_lastIndices);
        if (m_meshViewer) m_meshViewer->updateMesh(m_lastVertices, m_lastIndices);
    }
}

void MainWindow::onSmoothCatmull()
{
    int iterations = std::clamp(m_catmullIterSlider->value(), 0, 4);
    if (iterations <= 0) return;

    bool usingPersistent = m_meshRepo.active() != nullptr;
    SM* targetMesh = nullptr;
    SM tempMesh;

    if (usingPersistent) {
        targetMesh = &m_meshRepo.active()->polyMesh;
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
    if (!usingPersistent) {
        m_activeMeshIndex = m_meshRepo.addMesh(sm, Qt::white);
    }
    auto* entry = m_meshRepo.active();
    if (entry) {
        entry->polyMesh = sm;
        m_meshRepo.rebuildRenderData(*entry);
        m_meshRepo.buildFlattened(m_lastVertices, m_lastIndices);
        if (m_meshViewer) m_meshViewer->updateMesh(m_lastVertices, m_lastIndices);
    }
}
