#include "MainLoop.h"
#include "MeshViewerWidget.h"
#include "AgentProjectionWindow.h"

MainLoop::MainLoop(QObject* parent) : QObject(parent) {
    connect(&m_timer, &QTimer::timeout, this, &MainLoop::tick);
    m_timer.setInterval(16); // ~60 FPS
}

void MainLoop::setAgentCount(int n) {
    if (m_agentWindow) m_agentWindow->setAgentCount(n);
}

void MainLoop::setAgentLifetime(int ticks) {
    if (m_agentWindow) m_agentWindow->setAgentLifetime(ticks);
}

void MainLoop::setLineWidth(float w) {
    if (m_agentWindow) m_agentWindow->setLineWidth(w);
}

void MainLoop::setRandomTrailColors(bool enabled) {
    if (m_agentWindow) m_agentWindow->setRandomTrailColors(enabled);
}

void MainLoop::start() {
    m_running = true;
    if (m_viewer) m_viewer->setAgentsPaused(false);
    if (m_agentWindow) m_agentWindow->setRunning(true);
    m_timer.start();
}

void MainLoop::stop() {
    m_running = false;
    m_timer.stop();
    if (m_viewer) m_viewer->setAgentsPaused(true);
    if (m_agentWindow) m_agentWindow->setRunning(false);
}

void MainLoop::tick() {
    if (!m_running) return;
    if (m_viewer) {
        // MeshViewerWidget has its own small timer, but we explicitly kick an update to keep ordering clear.
        m_viewer->updateAgents();
    }
    if (m_agentWindow) {
        m_agentWindow->stepFrame();
    }
}
