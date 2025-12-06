#include "AgentProjectionWindow.h"
#include <QPainter>
#include <QRandomGenerator>
#include <QDebug>
#include <QtMath>
#include <QMatrix4x4>
#include <QVector3D>

AgentProjectionWindow::AgentProjectionWindow(MeshViewerWidget *meshViewer, QWidget *parent)
    : QWidget(parent)
    , m_meshViewer(meshViewer)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &AgentProjectionWindow::updateAgents);
    setAttribute(Qt::WA_OpaquePaintEvent);
    
    m_agents.resize(m_agentCount);
    // Initialize random positions
    for (auto &agent : m_agents) {
        agent.pos = QVector2D(QRandomGenerator::global()->bounded(1000), QRandomGenerator::global()->bounded(1000));
        agent.active = false;
    }
}

void AgentProjectionWindow::setAgentCount(int n)
{
    m_agentCount = n;
    m_agents.resize(n);
}

void AgentProjectionWindow::setRunning(bool run)
{
    m_running = run;
    if (m_running) {
        m_timer->start(16); // ~60fps
    } else {
        m_timer->stop();
    }
}

void AgentProjectionWindow::resizeEvent(QResizeEvent *event)
{
    if (width() > 0 && height() > 0) {
        m_canvas = QImage(size(), QImage::Format_ARGB32_Premultiplied);
        m_canvas.fill(Qt::black);
    }
    QWidget::resizeEvent(event);
}

void AgentProjectionWindow::updateAgents()
{
    if (!m_meshViewer) return;
    
    // Clear canvas slightly for trail effect? Or just clear?
    // User said: "draw this point", implying standard rendering.
    // Let's clear to black every frame for now, or maybe fade?
    // "Draw this point by reverse calculation to the screen plane"
    
    m_canvas.fill(Qt::black);
    
    // Get Camera Matrices from Mesh Viewer
    // We need to implement getters in MeshViewerWidget or expose these.
    // For now assuming we can access them or replicate logic.
    // Ideally MeshViewerWidget should give us specific matrices.
    
    QPainter painter(&m_canvas);
    painter.setPen(Qt::NoPen);
    
    for (auto &agent : m_agents) {
        // Reshuffle undefined agents
        agent.pos = QVector2D(QRandomGenerator::global()->bounded(width()), QRandomGenerator::global()->bounded(height()));
        
        if (checkIntersection(agent.pos)) {
            painter.setBrush(Qt::green);
            painter.drawEllipse(QPointF(agent.pos.x(), agent.pos.y()), 2, 2);
        }
    }
    
    update();
}

bool AgentProjectionWindow::checkIntersection(const QVector2D &screenPos)
{
    // Ray Casting Logic
    // Need View and Projection matrices matching the 3D window
    // We'll need to ask MeshViewerWidget for its current MVP/Camera state
    
    // Placeholder until we link them: Always false
    return m_meshViewer->checkRayIntersection(screenPos.x(), screenPos.y(), width(), height());
}

void AgentProjectionWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.drawImage(0, 0, m_canvas);
}
