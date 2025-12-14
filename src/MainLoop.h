#pragma once

#include <QObject>
#include <QTimer>

class MeshViewerWidget;
class AgentProjectionWindow;

// Central place to drive per-frame updates so it's clear who ticks what.
class MainLoop : public QObject {
    Q_OBJECT
public:
    explicit MainLoop(QObject* parent = nullptr);
    void setMeshViewer(MeshViewerWidget* viewer) { m_viewer = viewer; }
    void setAgentWindow(AgentProjectionWindow* window) { m_agentWindow = window; }

    void setAgentCount(int n);
    void setAgentLifetime(int ticks);
    void setLineWidth(float w);
    void setRandomTrailColors(bool enabled);

    void start();
    void stop();
    bool isRunning() const { return m_running; }

private slots:
    void tick();

private:
    QTimer m_timer;
    MeshViewerWidget* m_viewer = nullptr;
    AgentProjectionWindow* m_agentWindow = nullptr;
    bool m_running = false;
};

