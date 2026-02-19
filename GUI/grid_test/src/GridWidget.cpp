#include "GridWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QtMath>

GridWidget::GridWidget(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
}

static QColor neonGreen(int alpha = 255) {
    QColor c(0, 255, 80);
    c.setAlpha(alpha);
    return c;
}

void GridWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, false);

    // Black background
    p.fillRect(rect(), Qt::black);

    // Square grid area centered, with padding
    const QRectF full = rect();
    const double pad = qMin(full.width(), full.height()) * 0.08;
    QRectF area = full.adjusted(pad, pad, -pad, -pad);

    // Force area to be a square (like calibration grids)
    const double side = qMin(area.width(), area.height());
    area = QRectF(area.center().x() - side/2.0,
                  area.center().y() - side/2.0,
                  side, side);

    // Clip to area so nothing bleeds outside
    p.save();
    p.setClipRect(area);

    const int N = m_divisions;
    const double step = area.width() / N;

    // Faux glow: draw a thicker, faint pass behind
    if (m_glow) {
        QPen glowPen(neonGreen(70));
        glowPen.setWidthF(m_lineW * 3.0);
        glowPen.setCapStyle(Qt::SquareCap);
        glowPen.setJoinStyle(Qt::MiterJoin);
        p.setPen(glowPen);

        // vertical lines
        for (int i = 0; i <= N; ++i) {
            const double x = area.left() + i * step;
            p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }
        // horizontal lines
        for (int j = 0; j <= N; ++j) {
            const double y = area.top() + j * step;
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }
    }

    // Main sharp grid
    QPen gridPen(neonGreen(255));
    gridPen.setWidthF(m_lineW);
    gridPen.setCapStyle(Qt::SquareCap);
    gridPen.setJoinStyle(Qt::MiterJoin);
    p.setPen(gridPen);

    for (int i = 0; i <= N; ++i) {
        const double x = area.left() + i * step;
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    }
    for (int j = 0; j <= N; ++j) {
        const double y = area.top() + j * step;
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    // Center reticle (circle + small cardinal ticks)
    const QPointF c = area.center();

    if (m_reticle) {
        if (m_glow) {
            QPen rGlow(neonGreen(90));
            rGlow.setWidthF(m_lineW * 3.0);
            p.setPen(rGlow);
            p.drawEllipse(c, step * 0.45, step * 0.45);
        }

        p.setPen(gridPen);
        p.drawEllipse(c, step * 0.40, step * 0.40);

        const double tick = step * 0.35;
        p.drawLine(QPointF(c.x() - tick, c.y()), QPointF(c.x() - tick*0.55, c.y()));
        p.drawLine(QPointF(c.x() + tick, c.y()), QPointF(c.x() + tick*0.55, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - tick), QPointF(c.x(), c.y() - tick*0.55));
        p.drawLine(QPointF(c.x(), c.y() + tick), QPointF(c.x(), c.y() + tick*0.55));

        // small center dot
        p.drawEllipse(c, m_lineW*1.3, m_lineW*1.3);
    }

    // Diagonal line from center to top-right corner
    if (m_diagonal) {
        if (m_glow) {
            QPen dGlow(neonGreen(80));
            dGlow.setWidthF(m_lineW * 3.0);
            p.setPen(dGlow);
            p.drawLine(c, QPointF(area.right(), area.top()));
        }
        p.setPen(gridPen);
        p.drawLine(c, QPointF(area.right(), area.top()));
    }

    p.restore();

    // Optional: small info text (off by default; keep projection clean)
    // p.setPen(Qt::white);
    // p.drawText(20, 30, QString("Grid %1x%1").arg(N));
}