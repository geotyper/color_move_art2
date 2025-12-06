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

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateAgents();
    bool checkIntersection(const QVector2D &screenPos);

    MeshViewerWidget *m_meshViewer;
    QTimer *m_timer;
    QImage m_canvas;
    
    struct Agent {
        QVector2D pos;
        QColor color;
        bool active = false;
    };
    
    std::vector<Agent> m_agents;
    int m_agentCount = 1000;
    bool m_running = false;
};
