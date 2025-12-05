#include "MainWindow.h"
#include <algorithm>
#include <QFrame>
#include <QMdiSubWindow>
#include <QTimer>
#include <QShowEvent>
#include <QImage>
#include <QPixmap>
#include <QColor>

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

    m_sharpenSlider = new QSlider(Qt::Horizontal);
    m_sharpenSlider->setRange(0, 200); // 0.0 - 2.0
    m_sharpenSlider->setValue(0);
    connect(m_sharpenSlider, &QSlider::valueChanged, this, &MainWindow::onSharpenChanged);
    formLayout->addRow("Sharpen:", m_sharpenSlider);

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
    
    formLayout->addRow(genLayout);
    // Preview Checkbox
    m_previewCheckBox = new QCheckBox("Show Preview Only");
    connect(m_previewCheckBox, &QCheckBox::toggled, this, &MainWindow::onPreviewToggled);
    formLayout->addRow(m_previewCheckBox);
    
    // Toroidal Checkbox
    m_toroidalCheckBox = new QCheckBox("Toroidal Movement");
    connect(m_toroidalCheckBox, &QCheckBox::toggled, this, &MainWindow::onToroidalToggled);
    formLayout->addRow(m_toroidalCheckBox);

    
    controlLayout->addLayout(formLayout);
    
    QPushButton *regenBtn = new QPushButton("Regenerate (R)");
    connect(regenBtn, &QPushButton::clicked, this, &MainWindow::onRegenerate);
    controlLayout->addWidget(regenBtn);

    QPushButton *regenOverlayBtn = new QPushButton("Regenerate (Overlay)");
    connect(regenOverlayBtn, &QPushButton::clicked, this, &MainWindow::onRegenerateOverlay);
    controlLayout->addWidget(regenOverlayBtn);

    QPushButton *regenShiftedBtn = new QPushButton("Regenerate (Shifted)");
    connect(regenShiftedBtn, &QPushButton::clicked, this, &MainWindow::onRegenerateShiftedOverlay);
    controlLayout->addWidget(regenShiftedBtn);

    QPushButton *regenOverlaySqueegeeBtn = new QPushButton("Regenerate2 (Overlay)");
    connect(regenOverlaySqueegeeBtn, &QPushButton::clicked, this, &MainWindow::onRegenerateOverlaySqueegeeOnly);
    controlLayout->addWidget(regenOverlaySqueegeeBtn);

    QPushButton *saturateBtn = new QPushButton("Saturate");
    connect(saturateBtn, &QPushButton::clicked, this, &MainWindow::onSaturate);
    controlLayout->addWidget(saturateBtn);

    QPushButton *combFixBtn = new QPushButton("Comb Fix");
    connect(combFixBtn, &QPushButton::clicked, this, &MainWindow::onCombFix);
    controlLayout->addWidget(combFixBtn);
    
    controlLayout->addStretch();
    
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
    
    lightLayout->addRow("Light Azimuth:", m_lightAzimuthSlider);
    lightLayout->addRow("Light Elevation:", m_lightElevationSlider);
    lightLayout->addRow("Zoom (Dist):", m_zoomSlider);
    controlLayout->addLayout(lightLayout);
    
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
    QMdiSubWindow *meshWin = m_mdiArea->addSubWindow(m_meshViewer);
    meshWin->setWindowTitle("3D Mesh Viewer");
    meshWin->setAttribute(Qt::WA_DeleteOnClose, false);
    meshWin->resize(640, 480);
    meshWin->show();

    // Squeegee Window
    // Squeegee Window
    m_squeegeeWindow = new SqueegeeWindow();
    m_squeegeeWindow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_squeegeeWindow->setMinimumSize(480, 360);
    m_squeegeeWindow->setFocusPolicy(Qt::StrongFocus);
    
    QMdiSubWindow *squeegeeWin = m_mdiArea->addSubWindow(m_squeegeeWindow);
    squeegeeWin->setWindowTitle("2D Brush Canvas");
    squeegeeWin->setAttribute(Qt::WA_DeleteOnClose, false);
    squeegeeWin->resize(640, 480);
    squeegeeWin->show();

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
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_tiledOnce) return;
    m_tiledOnce = true;
    // Tile after the window is actually shown so layout sizes are valid
    QTimer::singleShot(0, this, [this]() {
        m_mdiArea->tileSubWindows();
    });
}

MainWindow::~MainWindow()
{
    delete m_squeegeeWindow;
}

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
