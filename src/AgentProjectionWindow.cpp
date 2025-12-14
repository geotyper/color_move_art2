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

void AgentProjectionWindow::setAgentLifetime(int ticks)
{
    if (m_meshViewer) m_meshViewer->setAgentLifetime(ticks);
}

void AgentProjectionWindow::setLineWidth(float w)
{
    m_lineWidth = w;
}

void AgentProjectionWindow::setRunning(bool run)
{
    m_running = run;
    if (m_running) {
        if (m_meshViewer) {
            m_meshViewer->clearAgents();
            m_meshViewer->setAgentsPaused(false);
        }
        m_timer->start(16); // ~60fps
    } else {
        if (m_meshViewer) {
            m_meshViewer->setAgentsPaused(true);
        }
        m_timer->stop();
    }
}

void AgentProjectionWindow::resizeEvent(QResizeEvent *event)
{
    if (width() > 0 && height() > 0) {
        m_canvas = QImage(size(), QImage::Format_ARGB32_Premultiplied);
        m_canvas.fill(QColor::fromRgbF(0.2f, 0.2f, 0.25f));
    }
    QWidget::resizeEvent(event);
}

void AgentProjectionWindow::updateAgents()
{
    if (!m_meshViewer) return;
    
    // Spawn / Refill
    int currentCount = m_meshViewer->getAgentCount();
    int needed = m_targetCount - currentCount;
    int viewW = m_meshViewer ? m_meshViewer->width() : 0;
    int viewH = m_meshViewer ? m_meshViewer->height() : 0;
    if (viewW <= 0 || viewH <= 0) return;
    if (needed > 0) {
        // Try to spawn 'needed' agents, but limit per frame to avoid freeze if bad luck
        int attempts = needed * 2;
        int spawned = 0;
        glm::vec3 hit;
        
        for (int i = 0; i < attempts && spawned < needed; ++i) {
            float rx = QRandomGenerator::global()->bounded((double)viewW);
            float ry = QRandomGenerator::global()->bounded((double)viewH);
            
            if (m_meshViewer->checkRayIntersection(rx, ry, viewW, viewH, hit)) {
                spawned++;
            }
        }
    }
    
    // Physics Step
    // m_meshViewer->updateAgents(); // Handled by MeshViewerWidget's timer
    
    // Render Step
    m_canvas.fill(QColor::fromRgbF(0.2f, 0.2f, 0.25f));
    QPainter painter(&m_canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    
    if (viewW <= 0 || viewH <= 0) {
        update();
        return;
    }
    auto projected = m_meshViewer->getProjectedAgents(viewW, viewH);
    const float scale = std::min(width()  / float(viewW),
                                 height() / float(viewH));
    const float ox = 0.5f * (width()  - viewW * scale);
    const float oy = 0.5f * (height() - viewH * scale);
    
    for (const auto &p : projected) {
        // Draw Trail Segments
        QColor trailColor = p.color;
        // Apply brightness factor per point if provided
        painter.setPen(QPen(trailColor, m_lineWidth));
        painter.setBrush(Qt::NoBrush);
        
        for (size_t si = 0; si < p.trailSegments.size(); ++si) {
            const auto &segment = p.trailSegments[si];
            const auto &bright  = (si < p.trailBrightness.size()) ? p.trailBrightness[si] : std::vector<float>();
            if (segment.size() > 1) {
                QPolygonF poly;
                for (size_t ti = 0; ti < segment.size(); ++ti) {
                    const auto &tp = segment[ti];
                    float b = (ti < bright.size()) ? bright[ti] : 1.0f;
                    QColor segCol = trailColor;
                    float alpha = 0.55f + 0.45f * b;
                    segCol.setAlphaF(std::clamp(alpha, 0.0f, 1.0f));
                    painter.setPen(QPen(segCol, m_lineWidth));
                    float yFlip = (float)viewH - tp.y();
                    poly << QPointF(ox + tp.x() * scale,
                                     height() - (oy + yFlip * scale));
                }
                painter.drawPolyline(poly);
            }
        }
        
        // Draw Head
        if (p.isVisible) {
            painter.setPen(Qt::NoPen);
            QColor head = p.color;
            // Softer shadow falloff: keep a higher minimum
            float alpha = 0.6f + 0.4f * std::clamp(p.headBrightness, 0.0f, 1.0f);
            head.setAlphaF(alpha);
            painter.setBrush(head);
            float headY = (float)viewH - p.screenPos.y();
            painter.drawEllipse(QPointF(ox + p.screenPos.x() * scale,
                                        height() - (oy + headY * scale)), 2, 2);
        }
    }
    
    update();
}

void AgentProjectionWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.drawImage(0, 0, m_canvas);
}
