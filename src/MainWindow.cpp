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
    m_stepsSlider->setRange(50, 2000); // Min 50 as requested
    m_stepsSlider->setValue(600);
    m_stepsLabel = new QLabel("600 steps");
    connect(m_stepsSlider, &QSlider::valueChanged, this, &MainWindow::onStepsChanged);
    formLayout->addRow("Simulation Steps:", m_stepsLabel);
    formLayout->addRow(m_stepsSlider);
    
    // Density Slider (100 - 2000)
    m_densitySlider = new QSlider(Qt::Horizontal);
    m_densitySlider->setRange(100, 2000);
    m_densitySlider->setValue(600);
    m_densityLabel = new QLabel("600 drops");
    connect(m_densitySlider, &QSlider::valueChanged, this, &MainWindow::onDensityChanged);
    formLayout->addRow("Drop Density:", m_densityLabel);
    formLayout->addRow(m_densitySlider);
    
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
    
    // Opacity Non-Linearity Slider (0 - 100)
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(100); // Default to Strong (1.0)
    m_opacityLabel = new QLabel("1.0 (Strong)");
    connect(m_opacitySlider, &QSlider::valueChanged, this, &MainWindow::onOpacityChanged);
    formLayout->addRow("Opacity Curve:", m_opacityLabel);
    formLayout->addRow(m_opacitySlider);
    
    // Palette Combo
    m_paletteCombo = new QComboBox;
    m_paletteCombo->addItem("Modern Art");
    m_paletteCombo->addItem("Modern Earth");
    m_paletteCombo->addItem("Deep Ocean");
    m_paletteCombo->addItem("Vibrant Sunset");
    m_paletteCombo->addItem("Forest & Berry");
    m_paletteCombo->addItem("Modern Pop Art");
    m_paletteCombo->addItem("Cyberpunk");
    connect(m_paletteCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onPaletteChanged);
    formLayout->addRow("Palette:", m_paletteCombo);
    
    // Preview Checkbox
    m_previewCheckBox = new QCheckBox("Show Preview Only");
    connect(m_previewCheckBox, &QCheckBox::toggled, this, &MainWindow::onPreviewToggled);
    formLayout->addRow(m_previewCheckBox);
    
    // Toroidal Checkbox
    m_toroidalCheckBox = new QCheckBox("Toroidal Movement");
    connect(m_toroidalCheckBox, &QCheckBox::toggled, this, &MainWindow::onToroidalToggled);
    formLayout->addRow(m_toroidalCheckBox);
    
    // Dynamic Intensity Checkbox
    m_dynamicCheckBox = new QCheckBox("Dynamic Intensity (Soft Brush)");
    connect(m_dynamicCheckBox, &QCheckBox::toggled, this, &MainWindow::onDynamicToggled);
    formLayout->addRow(m_dynamicCheckBox);
    
    controlLayout->addLayout(formLayout);
    
    QPushButton *regenBtn = new QPushButton("Regenerate (R)");
    connect(regenBtn, &QPushButton::clicked, this, &MainWindow::onRegenerate);
    controlLayout->addWidget(regenBtn);
    
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
    m_stepsLabel->setText(QString::number(value) + " steps");
    m_squeegeeWindow->setGenSteps(value);
}

void MainWindow::onDensityChanged(int value)
{
    m_densityLabel->setText(QString::number(value) + " drops");
    m_squeegeeWindow->setGenDensity(value);
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

void MainWindow::onOpacityChanged(int value)
{
    float val = value / 100.0f;
    m_opacityLabel->setText(QString::number(val, 'f', 2));
    m_squeegeeWindow->setOpacityNonLinearity(val);
}

void MainWindow::onPreviewToggled(bool checked)
{
    m_squeegeeWindow->setShowPreview(checked);
}

void MainWindow::onToroidalToggled(bool checked)
{
    m_squeegeeWindow->setToroidal(checked);
}

void MainWindow::onDynamicToggled(bool checked)
{
    m_squeegeeWindow->setDynamicIntensity(checked);
}

void MainWindow::onPaletteChanged(int index)
{
    m_squeegeeWindow->setPalette(index);
}

void MainWindow::onRegenerate()
{
    m_squeegeeWindow->regenerate();
}
