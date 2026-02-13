#include "HudWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

HudWidget::HudWidget(QWidget *parent) : QWidget(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
}

double HudWidget::wrap360(double deg)
{
    deg = std::fmod(deg, 360.0);
    if (deg < 0) deg += 360.0;
    return deg;
}

void HudWidget::setHeadingDeg(double deg)  { m_headingDeg = wrap360(deg); update(); }
void HudWidget::setRollDeg(double deg)     { m_rollDeg = deg; update(); }
void HudWidget::setPitchDeg(double deg)    { m_pitchDeg = deg; update(); }
void HudWidget::setAltitudeFt(double ft)   { m_altitudeFt = ft; update(); }
void HudWidget::setVSpeedFpm(double fpm)   { m_vspeedFpm = fpm; update(); }

void HudWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // Background
    p.fillRect(rect(), Qt::black);

    // Layout (relative to window size)
    const QRectF full = rect();
    const double W = full.width();
    const double H = full.height();

    QRectF headingRect(W*0.30, H*0.05, W*0.40, H*0.10);
    QRectF attitudeRect(W*0.37, H*0.24, W*0.26, H*0.42);
    QRectF altitudeRect(W*0.67, H*0.24, W*0.10, H*0.42);
    QRectF bottomRect(W*0.35, H*0.75, W*0.30, H*0.10);

    drawHeadingTape(p, headingRect);
    drawAttitude(p, attitudeRect);
    drawAltitudeTape(p, altitudeRect);
    drawBottomReadouts(p, bottomRect);

    // little buttons in bottom-right (optional)
    QRectF iconArea(W*0.90, H*0.84, W*0.08, H*0.10);
    drawIconButtons(p, iconArea);
}

static QPen hudPen(double w = 2.0)
{
    QPen pen(QColor(230, 230, 230));
    pen.setWidthF(w);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

void HudWidget::drawHeadingTape(QPainter &p, const QRectF &r)
{
    p.save();
    p.setPen(hudPen(2.0));
    p.setBrush(Qt::NoBrush);

    // Outer box
    p.drawRoundedRect(r, 2, 2);

    // Center marker (top)
    const QPointF topMid(r.center().x(), r.top());
    p.drawLine(QPointF(topMid.x(), r.top()-10), QPointF(topMid.x(), r.top()+8));

    // Tick line region (inside)
    QRectF inner = r.adjusted(10, 10, -10, -10);
    const double pxPerDeg = inner.width() / 60.0; // show ~60 degrees across

    // Base heading shown at center
    const double centerHdg = m_headingDeg;
    const double startDeg = centerHdg - 30.0;

    // ticks every 5 degrees, longer every 10
    for (int i = 0; i <= 60; i += 5) {
        double deg = startDeg + i;
        double x = inner.left() + (deg - startDeg) * pxPerDeg;

        double tickH = ( (int)qRound(deg) % 10 == 0 ) ? inner.height()*0.55 : inner.height()*0.35;
        p.drawLine(QPointF(x, inner.bottom()), QPointF(x, inner.bottom() - tickH));

        // labels for cardinal-ish around (simple)
        if (((int)qRound(deg) % 30) == 0) {
            QString label;
            double d = wrap360(deg);
            if (qFuzzyCompare(d, 0.0) || qFuzzyCompare(d, 360.0)) label = "N";
            else if (qFuzzyCompare(d, 90.0)) label = "E";
            else if (qFuzzyCompare(d, 180.0)) label = "S";
            else if (qFuzzyCompare(d, 270.0)) label = "W";
            else label = QString::number((int)qRound(d));

            QFont f = p.font();
            f.setPointSizeF(r.height()*0.18);
            p.setFont(f);
            p.drawText(QRectF(x-20, inner.top(), 40, inner.height()*0.6),
                       Qt::AlignHCenter | Qt::AlignVCenter, label);
        }
    }

    // Center numeric readout box
    QRectF readout(r.center().x() - r.width()*0.07, r.center().y() - r.height()*0.12,
                   r.width()*0.14, r.height()*0.24);
    p.drawRect(readout);

    QFont f = p.font();
    f.setPointSizeF(r.height()*0.22);
    p.setFont(f);
    p.drawText(readout, Qt::AlignCenter, QString::number(m_headingDeg, 'f', 1) + "°");

    // "HEADING" label
    QFont f2 = p.font();
    f2.setPointSizeF(r.height()*0.14);
    p.setFont(f2);
    p.drawText(QRectF(r.left(), r.bottom()+2, r.width(), r.height()*0.30),
               Qt::AlignHCenter | Qt::AlignTop, "HEADING");

    p.restore();
}

void HudWidget::drawAttitude(QPainter &p, const QRectF &r)
{
    p.save();
    p.setPen(hudPen(2.0));
    p.setBrush(Qt::NoBrush);

    // Circle bounds
    QRectF circle = r;
    p.drawEllipse(circle);

    // Clip to circle so the horizon doesn't draw outside
    QPainterPath clip;
    clip.addEllipse(circle);
    p.setClipPath(clip);

    // Horizon / pitch ladder:
    // Map pitch degrees to pixels; positive pitch means nose up => horizon moves down.
    const double pxPerDeg = circle.height() / 30.0; // ~30° visible vertically
    const double horizonY = circle.center().y() + (-m_pitchDeg * pxPerDeg);

    // Draw "sky" and "ground"
    QRectF skyRect(circle.left(), circle.top(), circle.width(), horizonY - circle.top());
    QRectF groundRect(circle.left(), horizonY, circle.width(), circle.bottom() - horizonY);

    p.fillRect(skyRect, QColor(20, 80, 140));     // blue
    p.fillRect(groundRect, QColor(45, 45, 45));   // dark gray

    // Roll rotation around center for ladder lines
    p.save();
    p.translate(circle.center());
    p.rotate(-m_rollDeg); // negative to match typical aircraft convention
    p.translate(-circle.center());

    // Horizon line
    p.setPen(hudPen(3.0));
    p.drawLine(QPointF(circle.left(), horizonY), QPointF(circle.right(), horizonY));

    // Pitch ladder lines every 5 degrees (above and below horizon)
    p.setPen(hudPen(2.0));
    QFont f = p.font();
    f.setPointSizeF(circle.height()*0.06);
    p.setFont(f);

    for (int deg = -30; deg <= 30; deg += 5) {
        if (deg == 0) continue;
        double y = horizonY - (deg * pxPerDeg);
        if (y < circle.top()-20 || y > circle.bottom()+20) continue;

        double halfLen = (qAbs(deg) % 10 == 0) ? circle.width()*0.22 : circle.width()*0.16;
        QPointF L(circle.center().x() - halfLen, y);
        QPointF R(circle.center().x() + halfLen, y);

        p.drawLine(L, R);

        // Labels for 10-degree marks (like your screenshot)
        if (qAbs(deg) % 10 == 0) {
            QString t = QString::number(qAbs(deg));
            QRectF leftText(L.x() - 30, y - 10, 28, 20);
            QRectF rightText(R.x() + 2, y - 10, 28, 20);
            p.drawText(leftText, Qt::AlignRight | Qt::AlignVCenter, t);
            p.drawText(rightText, Qt::AlignLeft  | Qt::AlignVCenter, t);
        }
    }

    p.restore(); // roll transform

    // Unclip
    p.setClipping(false);

    // Center little reference marker (fixed, not rolling)
    p.setPen(hudPen(2.5));
    const QPointF c = circle.center();
    p.drawLine(QPointF(c.x() - circle.width()*0.10, c.y()),
               QPointF(c.x() - circle.width()*0.02, c.y()));
    p.drawLine(QPointF(c.x() + circle.width()*0.02, c.y()),
               QPointF(c.x() + circle.width()*0.10, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - circle.height()*0.02),
               QPointF(c.x(), c.y() + circle.height()*0.02));

    // "ATTITUDE" label below
    QFont f3 = p.font();
    f3.setPointSizeF(r.height()*0.06);
    p.setFont(f3);
    p.drawText(QRectF(r.left(), r.bottom()+4, r.width(), r.height()*0.20),
               Qt::AlignHCenter | Qt::AlignTop, "ATTITUDE");

    p.restore();
}

void HudWidget::drawAltitudeTape(QPainter &p, const QRectF &r)
{
    p.save();
    p.setPen(hudPen(2.0));
    p.setBrush(Qt::NoBrush);

    // Outer rect
    p.drawRoundedRect(r, 2, 2);

    // Inner scale region
    QRectF inner = r.adjusted(r.width()*0.12, r.height()*0.08, -r.width()*0.12, -r.height()*0.08);

    // Vertical mapping: show +- 500 ft around current altitude
    const double spanFt = 1000.0;
    const double pxPerFt = inner.height() / spanFt;

    const double centerAlt = m_altitudeFt;
    const double startAlt = centerAlt + spanFt/2.0;

    // ticks every 50 ft, long every 100/200
    p.setPen(hudPen(2.0));
    QFont f = p.font();
    f.setPointSizeF(r.height()*0.07);
    p.setFont(f);

    for (int ft = -500; ft <= 500; ft += 50) {
        double alt = centerAlt + ft;
        double y = inner.center().y() + (-ft * pxPerFt);

        bool major = ((int)qRound(alt) % 200 == 0);
        bool med   = ((int)qRound(alt) % 100 == 0);

        double tickLen = major ? inner.width()*0.55 : (med ? inner.width()*0.40 : inner.width()*0.25);

        p.drawLine(QPointF(inner.right() - tickLen, y), QPointF(inner.right(), y));

        if (med) {
            QString label = QString::number((int)qRound(alt/10.0)*10);
            p.drawText(QRectF(inner.left(), y-10, inner.width()*0.60, 20),
                       Qt::AlignLeft | Qt::AlignVCenter, label);
        }
    }

    // Current altitude readout box
    QRectF box(r.left() + r.width()*0.20, r.center().y() - r.height()*0.07,
               r.width()*0.60, r.height()*0.14);
    p.setPen(hudPen(2.0));
    p.drawRect(box);

    QFont f2 = p.font();
    f2.setPointSizeF(r.height()*0.12);
    p.setFont(f2);
    p.drawText(box, Qt::AlignCenter, QString::number((int)qRound(m_altitudeFt)));

    // "ALTITUDE" and vspeed text under (like screenshot)
    QFont f3 = p.font();
    f3.setPointSizeF(r.height()*0.07);
    p.setFont(f3);
    p.drawText(QRectF(r.left(), r.bottom()+4, r.width(), r.height()*0.22),
               Qt::AlignHCenter | Qt::AlignTop, "ALTITUDE");

    QFont f4 = p.font();
    f4.setPointSizeF(r.height()*0.07);
    p.setFont(f4);
    p.drawText(QRectF(r.left(), r.bottom()+r.height()*0.18, r.width(), r.height()*0.22),
               Qt::AlignHCenter | Qt::AlignTop,
               QString("%1 FPM").arg((int)qRound(m_vspeedFpm)));

    p.restore();
}

void HudWidget::drawBottomReadouts(QPainter &p, const QRectF &r)
{
    p.save();
    p.setPen(hudPen(2.0));
    p.setBrush(Qt::NoBrush);

    p.drawRoundedRect(r, 2, 2);

    // vertical divider
    p.drawLine(QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.bottom()));

    QFont label = p.font();
    label.setPointSizeF(r.height()*0.18);

    QFont value = p.font();
    value.setPointSizeF(r.height()*0.26);

    // Left: Roll
    p.setFont(label);
    p.drawText(QRectF(r.left(), r.top()+6, r.width()/2, r.height()*0.35),
               Qt::AlignHCenter | Qt::AlignVCenter, "ROLL");
    p.setFont(value);
    p.drawText(QRectF(r.left(), r.top()+r.height()*0.35, r.width()/2, r.height()*0.55),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QString("%1°").arg(m_rollDeg, 0, 'f', 1));

    // Right: Pitch
    p.setFont(label);
    p.drawText(QRectF(r.center().x(), r.top()+6, r.width()/2, r.height()*0.35),
               Qt::AlignHCenter | Qt::AlignVCenter, "PITCH");
    p.setFont(value);
    p.drawText(QRectF(r.center().x(), r.top()+r.height()*0.35, r.width()/2, r.height()*0.55),
               Qt::AlignHCenter | Qt::AlignVCenter,
               QString("%1°").arg(m_pitchDeg, 0, 'f', 1));

    p.restore();
}

void HudWidget::drawIconButtons(QPainter &p, const QRectF &r)
{
    p.save();
    p.setPen(hudPen(2.0));
    p.setBrush(Qt::NoBrush);

    // Two small squares like the screenshot buttons
    QRectF a(r.left(), r.top(), r.width()*0.45, r.height()*0.60);
    QRectF b(r.left() + r.width()*0.52, r.top(), r.width()*0.45, r.height()*0.60);

    p.drawRoundedRect(a, 4, 4);
    p.drawRoundedRect(b, 4, 4);

    // Simple glyphs (you can replace with icons later)
    p.drawArc(a.adjusted(10, 10, -10, -10), 0 * 16, 180 * 16);
    p.drawEllipse(b.center(), 6, 6);

    p.restore();
}
