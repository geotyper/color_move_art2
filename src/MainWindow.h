#pragma once

#include <QMainWindow>
#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMdiArea>
#include <QCloseEvent>
#include <QSettings>
#include "SqueegeeWindow.h"
#include "MeshViewerWidget.h"
#include "Noise2D.h"
#include "MeshRepository.h"
#include "CgalMeshBuilder.h"
#include "CgalMeshBuilderTentacles.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow();

private slots:
    void onAgentLifetimeChanged(int value);
    void onAngleChanged(int value);
    void onWidthChanged(int value);
    void onPassesChanged(int value);
    void onStepsChanged(int value);
    void onDensityChanged(int value); // Restored
    void onSizeChanged(int value);
    void onMinSizeRatioChanged(int value);
    void onConcentricChanged(int value);
    void onPreviewToggled(bool checked);
    void onToroidalToggled(bool checked);
    void onAngleSnapToggled(bool checked);
    void onPaletteChanged(int index);
    void onShapeChanged(int index);
    void onGenModeChanged(int index);
    void onGridStepChanged(int value);
    void onSqueegeeModeChanged(int index);
    void onStepsPresetChanged(int index);
    void onDepthScalingToggled(bool checked);
    void onStepsFineChanged(int value);
    void onSharpenChanged(int value);
    void onNoiseModeChanged(int index);
    void onNoiseScaleChanged(int value);
    void onNoiseStrengthChanged(int value);
    void updateNoisePreview();

    void onRegenerate();
    void onRegenerateOverlay();
    void onRegenerateShiftedOverlay();
    void onRegenerateOverlaySqueegeeOnly();
    void onSaturate();
    void onCombFix();
    void onLightChanged();
    void onZoomChanged(int value);
    void onAgentCountChanged(int value);
    void onProjectAgents();
    void onPaintTrails();
    void onLineWidthChanged(int value);
    void onClearCanvas();
    void onBackgroundColorClicked();
    void onExtrudeRandom();
    void onGrowTentacles();
    void onSmoothCatmull();
    void onAgentSpeedChanged(int value);

private:
    SqueegeeWindow *m_squeegeeWindow;
    MeshViewerWidget *m_meshViewer;
    QMdiArea *m_mdiArea;
    bool m_tiledOnce = false;
    
    QSlider *m_lightAzimuthSlider;
    QSlider *m_lightElevationSlider;
    QSlider *m_zoomSlider;
    
    class AgentProjectionWindow *m_agentWindow;
    void onToggleAgents();
    QPushButton *m_agentButton;
    QSlider *m_agentCountSlider;
    QLabel *m_agentCountLabel;
    QSlider *m_agentLifetimeSlider;
    QLabel *m_agentLifetimeLabel;
    QSlider *m_agentSpeedSlider;
    QLabel *m_agentSpeedLabel;

    // Subwindows
    QMdiSubWindow *m_meshSubWindow;
    QMdiSubWindow *m_squeegeeSubWindow;
    QMdiSubWindow *m_agentSubWindow;

    QSlider *m_lineWidthSlider;
    QLabel *m_lineWidthLabel;

    void saveSettings();
    void loadSettings();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    
    QSlider *m_angleSlider;
    QSlider *m_widthSlider;
    QSlider *m_passesSlider;
    QSlider *m_stepsSlider;
    QSlider *m_stepsFineSlider;
    QSlider *m_sharpenSlider;
    QSlider *m_opacitySlider; // Opacity Non-Linearity
    QSlider *m_gridStepSlider; // Grid Step Size of drops
    QSlider *m_sizeSlider; // Drop Max Size
    QSlider *m_concentricSlider; // Nested Circles
    
    QCheckBox *m_previewCheckBox;
    QCheckBox *m_toroidalCheckBox;
    QCheckBox *m_angleSnapCheckBox; // New Checkbox
    QComboBox *m_brushTypeCombo;
    QComboBox *m_paletteCombo;
    QComboBox *m_shapeCombo;
    QComboBox *m_genModeCombo;
    QComboBox *m_squeegeeModeCombo;
    QComboBox *m_stepsPresetCombo;
    QCheckBox *m_depthScalingCheckBox;
    QComboBox *m_noiseModeCombo;
    
    QSlider *m_densitySlider; // New Density Slider
    
    QLabel *m_angleLabel;
    QLabel *m_widthLabel;
    QLabel *m_passesLabel;
    QLabel *m_stepsLabel;
    QLabel *m_densityLabel; // New Density Label
    QLabel *m_opacityLabel;
    QLabel *m_gridStepLabel;
    QLabel *m_sizeLabel;
    
    QSlider *m_minSizeRatioSlider;
    QLabel *m_minSizeRatioLabel;
    
    QLabel *m_concentricLabel;
    QLabel *m_noiseScaleLabel;
    QLabel *m_noiseStrengthLabel;
    QSlider *m_noiseScaleSlider;
    QSlider *m_noiseStrengthSlider;
    QLabel *m_noisePreviewLabel;
    Noise2D m_noisePreviewGen;
    
    // 3D Generation Tab
    QComboBox *m_primitiveCombo;
    QSlider *m_primParam1Slider;
    QLabel *m_primParam1Label;
    QSlider *m_primParam2Slider;
    QLabel *m_primParam2Label;
    QSlider *m_primParam3Slider;
    QLabel *m_primParam3Label;
    QPushButton *m_generateMeshBtn;
    QSlider *m_extrudeProbSlider;
    QLabel  *m_extrudeProbLabel;
    QSlider *m_extrudeDistSlider;
    QLabel  *m_extrudeDistLabel;
    QSlider *m_extrudeScaleSlider;
    QLabel  *m_extrudeScaleLabel;
    QCheckBox *m_extrudeRemoveBase;
    QPushButton *m_extrudeBtn;
    QSlider *m_tentacleStepsSlider;
    QLabel  *m_tentacleStepsLabel;
    QSlider *m_tentacleDistSlider;
    QLabel  *m_tentacleDistLabel;
    QSlider *m_tentacleScaleSlider;
    QLabel  *m_tentacleScaleLabel;
    QPushButton *m_growTentaclesBtn;
    QSlider *m_catmullIterSlider;
    QLabel  *m_catmullIterLabel;
    QPushButton *m_catmullSmoothBtn;
    
    void onPrimitiveChanged(int index);
    void onPrimParam1Changed(int value);
    void onPrimParam2Changed(int value);
    void onPrimParam3Changed(int value);
    void onGenerateMesh();

    // Cached last generated mesh in app vertex/index format
    std::vector<Vertex> m_lastVertices;
    std::vector<uint32_t> m_lastIndices;
    
    // Mesh storage with per-mesh/per-face colors
    MeshRepository m_meshRepo;
    int m_activeMeshIndex = -1;
};
