#include "MainWindow.h"

MainWindow::MainWindow()
{
    setWindowTitle("Digital Squeegee Art - Controls");
    resize(1400, 800);

    QWidget *centralWidget = new QWidget;
    setCentralWidget(centralWidget);
    
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    
    // Control Panel
    QWidget *controls = new QWidget;
    controls->setFixedWidth(300);
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
    formLayout->addRow(m_stepsSlider);

    m_stepsPresetCombo = new QComboBox();
    for (int v = 25; v <= 250; v += 25) {
        m_stepsPresetCombo->addItem(QString::number(v) + " steps", v);
    }
    m_stepsPresetCombo->setCurrentIndex(3); // 100 steps
    connect(m_stepsPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onStepsPresetChanged);
    formLayout->addRow("Preset:", m_stepsPresetCombo);
    
    // Density Slider (100 - 2000)


    
    // Drop Max Size Slider (10 - 100)
    m_sizeSlider = new QSlider(Qt::Horizontal);
    m_sizeSlider->setRange(10, 100);
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

    // Squeegee Mode Combo
    m_squeegeeModeCombo = new QComboBox();
    m_squeegeeModeCombo->addItem("Solid");
    m_squeegeeModeCombo->addItem("Soft");
    m_squeegeeModeCombo->addItem("Accurate");
    connect(m_squeegeeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onSqueegeeModeChanged);
    formLayout->addRow("Squeegee:", m_squeegeeModeCombo);

    // Grid Step Slider (10 - 200 px)
    m_gridStepSlider = new QSlider(Qt::Horizontal);
    m_gridStepSlider->setRange(10, 200);
    m_gridStepSlider->setValue(50);
    m_gridStepLabel = new QLabel("50 px");
    connect(m_gridStepSlider, &QSlider::valueChanged, this, &MainWindow::onGridStepChanged);
    formLayout->addRow("Grid Step:", m_gridStepLabel);
    formLayout->addRow(m_gridStepSlider);
    
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

    QPushButton *regenOverlaySqueegeeBtn = new QPushButton("Regenerate2 (Overlay)");
    connect(regenOverlaySqueegeeBtn, &QPushButton::clicked, this, &MainWindow::onRegenerateOverlaySqueegeeOnly);
    controlLayout->addWidget(regenOverlaySqueegeeBtn);

    QPushButton *saturateBtn = new QPushButton("Saturate");
    connect(saturateBtn, &QPushButton::clicked, this, &MainWindow::onSaturate);
    controlLayout->addWidget(saturateBtn);
    
    controlLayout->addStretch();
    
    QLabel *info = new QLabel("Controls:\nSpace: Toggle Tool\n1-5: Colors\nWheel: Brush Size\nB: Blur");
    controlLayout->addWidget(info);
    
    mainLayout->addWidget(controls);
    
    // Squeegee Window
    m_squeegeeWindow = new SqueegeeWindow();
    m_container = QWidget::createWindowContainer(m_squeegeeWindow);
    m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_container->setFocusPolicy(Qt::StrongFocus);
    mainLayout->addWidget(m_container);
    
    // Initial values
    m_squeegeeWindow->setGenAngle(45.0f);
    m_squeegeeWindow->setGenWidth(2.0f);
    m_squeegeeWindow->setGenPasses(1);
    m_squeegeeWindow->setGenSteps(600);
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
    int snapped = (value / 25) * 25;
    if (snapped < 25) snapped = 25;
    if (snapped > 250) snapped = 250;
    if (snapped != value) {
        m_stepsSlider->blockSignals(true);
        m_stepsSlider->setValue(snapped);
        m_stepsSlider->blockSignals(false);
    }
    m_stepsLabel->setText(QString::number(snapped) + " steps");
    int presetIndex = (snapped / 25) - 1;
    if (presetIndex >= 0 && presetIndex < m_stepsPresetCombo->count()) {
        if (m_stepsPresetCombo->currentIndex() != presetIndex) {
            m_stepsPresetCombo->blockSignals(true);
            m_stepsPresetCombo->setCurrentIndex(presetIndex);
            m_stepsPresetCombo->blockSignals(false);
        }
    }
    m_squeegeeWindow->setGenSteps(snapped);
}

void MainWindow::onSizeChanged(int value)
{
    m_sizeLabel->setText(QString::number(value) + " px");
    m_squeegeeWindow->setDropMaxSize(value);
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

void MainWindow::onSqueegeeModeChanged(int index)
{
    m_squeegeeWindow->setSqueegeeMode((SqueegeeWindow::SqueegeeMode)index);
}

void MainWindow::onStepsPresetChanged(int index)
{
    int value = m_stepsPresetCombo->itemData(index).toInt();
    m_stepsSlider->setValue(value);
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

void MainWindow::onRegenerateOverlaySqueegeeOnly()
{
    m_squeegeeWindow->regenerateSqueegeeOnly();
}

void MainWindow::onSaturate()
{
    m_squeegeeWindow->applySaturation();
}
