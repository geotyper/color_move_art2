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
    void onDensityChanged(int value);
    void onSizeChanged(int value);
    void onConcentricChanged(int value);
    void onOpacityChanged(int value);
    void onPreviewToggled(bool checked);
    void onToroidalToggled(bool checked);
    void onDynamicToggled(bool checked);
    void onPaletteChanged(int index);
    void onRegenerate();

private:
    SqueegeeWindow *m_squeegeeWindow;
    QWidget *m_container;
    
    QSlider *m_angleSlider;
    QSlider *m_widthSlider;
    QSlider *m_passesSlider;
    QSlider *m_stepsSlider;
    QSlider *m_densitySlider; // Number of drops
    QSlider *m_sizeSlider; // Drop Max Size
    QSlider *m_concentricSlider; // Nested Circles
    QSlider *m_opacitySlider; // Opacity Non-Linearity
    
    QCheckBox *m_previewCheckBox;
    QCheckBox *m_toroidalCheckBox;
    QCheckBox *m_dynamicCheckBox;
    QComboBox *m_paletteCombo;
    
    QLabel *m_angleLabel;
    QLabel *m_widthLabel;
    QLabel *m_passesLabel;
    QLabel *m_stepsLabel;
    QLabel *m_densityLabel;
    QLabel *m_sizeLabel;
    QLabel *m_concentricLabel;
    QLabel *m_opacityLabel;
};
