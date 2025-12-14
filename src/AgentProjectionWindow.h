#pragma once

#include <QWidget>
#include <QImage>
#include <QVector2D>
#include <vector>
#include "MeshViewerWidget.h"

class AgentProjectionWindow : public QWidget
{
    Q_OBJECT

public:
    explicit AgentProjectionWindow(MeshViewerWidget *meshViewer, QWidget *parent = nullptr);
    
    void setAgentCount(int n);
    void setRunning(bool run);
    void setAgentLifetime(int ticks);
    void setLineWidth(float w);
    void setRandomTrailColors(bool enabled) { m_randomTrailColors = enabled; }
    void stepFrame(); // called by main loop

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateAgents();
    // bool checkRayIntersection(const QVector2D &screenPos); // Removed

    MeshViewerWidget *m_meshViewer;
    QImage m_canvas;
    
    int m_targetCount = 10;
    bool m_running = false;
    float m_lineWidth = 1.0f;
    bool m_randomTrailColors = false;
};
