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
}

void AgentProjectionWindow::setAgentCount(int n)
{
    m_targetCount = n;
}

void AgentProjectionWindow::setRunning(bool run)
{
    m_running = run;
    if (m_running) {
        if (m_meshViewer) m_meshViewer->clearAgents();
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
    
    // Spawn / Refill
    int currentCount = m_meshViewer->getAgentCount();
    int needed = m_targetCount - currentCount;
    if (needed > 0) {
        // Try to spawn 'needed' agents, but limit per frame to avoid freeze if bad luck
        int attempts = needed * 2;
        int spawned = 0;
        QVector3D hit;
        
        for (int i = 0; i < attempts && spawned < needed; ++i) {
            float rx = QRandomGenerator::global()->bounded((double)width());
            float ry = QRandomGenerator::global()->bounded((double)height());
            
            if (m_meshViewer->checkRayIntersection(rx, ry, width(), height(), hit)) {
                spawned++;
            }
        }
    }
    
    // Physics Step
    m_meshViewer->updateAgents();
    
    // Render Step
    m_canvas.fill(Qt::black);
    QPainter painter(&m_canvas);
    painter.setPen(Qt::NoPen);
    
    auto projected = m_meshViewer->getProjectedAgents(width(), height());
    
    for (const auto &p : projected) {
        // Draw Trail Segments
        QColor trailColor = p.color;
        trailColor.setAlpha(150);
        painter.setPen(QPen(trailColor, 1));
        painter.setBrush(Qt::NoBrush);
        
        for (const auto &segment : p.trailSegments) {
            if (segment.size() > 1) {
                QPolygonF poly;
                for (const auto &tp : segment) {
                    poly << QPointF(tp.x(), tp.y());
                }
                painter.drawPolyline(poly);
            }
        }
        
        // Draw Head
        if (p.isVisible) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(p.color);
            painter.drawEllipse(QPointF(p.screenPos.x(), p.screenPos.y()), 2, 2);
        }
    }
    
    update();
}

void AgentProjectionWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.drawImage(0, 0, m_canvas);
}
