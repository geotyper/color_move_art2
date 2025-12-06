#pragma once

#include <QWidget>
#include <QImage>
#include <QTimer>
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

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateAgents();
    // bool checkRayIntersection(const QVector2D &screenPos); // Removed

    MeshViewerWidget *m_meshViewer;
    QTimer *m_timer;
    QImage m_canvas;
    
    int m_targetCount = 1000;
    bool m_running = false;
};
