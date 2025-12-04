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
#include "SqueegeeWindow.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow();

private slots:
    void onAngleChanged(int value);
    void onWidthChanged(int value);
    void onPassesChanged(int value);
    void onStepsChanged(int value);
    // void onDensityChanged(int value); // This slot is removed
    void onSizeChanged(int value);
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

    void onRegenerate();
    void onRegenerateOverlay();
    void onRegenerateOverlaySqueegeeOnly();
    void onSaturate();
    void onCombFix();

private:
    SqueegeeWindow *m_squeegeeWindow;
    QWidget *m_container;
    
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
    
    QLabel *m_angleLabel;
    QLabel *m_widthLabel;
    QLabel *m_passesLabel;
    QLabel *m_stepsLabel;
    QLabel *m_opacityLabel;
    QLabel *m_gridStepLabel;
    QLabel *m_sizeLabel;
    QLabel *m_concentricLabel;
};
