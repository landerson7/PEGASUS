#include "HudWidget.h"
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <algorithm>
#include <limits>

float textMultiplier = 1.0f;

namespace
{
QFont fittedFont(const QFont &baseFont,
                 const QRectF &rect,
                 const QString &text,
                 double startPointSize,
                 double minPointSize = 1.0)
{
    QFont f(baseFont);
    f.setPointSizeF(startPointSize);

    // Leave a little padding so glyphs and degree symbols never touch the box edges.
    const QRectF padded = rect.adjusted(4.0, 2.0, -4.0, -2.0);

    for (double sz = startPointSize; sz >= minPointSize; sz -= 0.5) {
        f.setPointSizeF(sz);
        QFontMetricsF fm(f);
        const QRectF br = fm.boundingRect(padded, Qt::AlignCenter | Qt::TextSingleLine, text);
        if (br.width() <= padded.width() && br.height() <= padded.height()) {
            return f;
        }
    }

    f.setPointSizeF(minPointSize);
    return f;
}

void drawFittedCenteredText(QPainter &p,
                            const QRectF &rect,
                            const QString &text,
                            double startPointSize,
                            double minPointSize = 1.0)
{
    QFont f = fittedFont(p.font(), rect, text, startPointSize, minPointSize);
    p.setFont(f);
    p.drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, text);
}
}

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
    QElapsedTimer paintTimer;
    paintTimer.start();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    p.fillRect(rect(), Qt::black);

    const QRectF full = rect();
    const double W = full.width();
    const double H = full.height();

    // Layout tuned for:
    // - smaller central gimbal
    // - larger heading/altitude values
    // - extra room to avoid clipping
    QRectF headingRect (W * 0.25, H * 0.045, W * 0.50, H * 0.125);
    QRectF attitudeRect(W * 0.395, H * 0.285, W * 0.21, H * 0.32);   // shrunk more
    QRectF altitudeRect(W * 0.655, H * 0.215, W * 0.14, H * 0.47);
    QRectF bottomRect  (W * 0.305, H * 0.695, W * 0.39, H * 0.165);

    p.save();

    p.translate(W / 2.0, H / 2.0);
    p.rotate(180);
    p.scale(0.425, 0.425);
    p.translate(-W / 2.0, -H / 2.0);

    drawHeadingTape(p, headingRect);
    drawAttitude(p, attitudeRect);
    drawAltitudeTape(p, altitudeRect);
    drawBottomReadouts(p, bottomRect);

    p.restore();

    double displayRateHz = std::numeric_limits<double>::quiet_NaN();
    if (!m_displayRateTimer.isValid()) {
        m_displayRateTimer.start();
        m_lastDisplayElapsedMs = 0;
    } else {
        const qint64 elapsedMs = m_displayRateTimer.elapsed();
        const qint64 deltaMs = elapsedMs - m_lastDisplayElapsedMs;
        if (deltaMs > 0) {
            displayRateHz = 1000.0 / static_cast<double>(deltaMs);
        }
        m_lastDisplayElapsedMs = elapsedMs;
    }

    emit frameRendered(static_cast<double>(paintTimer.nsecsElapsed()) / 1000000.0,
                       displayRateHz);
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

    p.drawRoundedRect(r, 2, 2);

    const QPointF topMid(r.center().x(), r.top());
    p.drawLine(QPointF(topMid.x(), r.top() - 10), QPointF(topMid.x(), r.top() + 8));

    QRectF inner = r.adjusted(10, 10, -10, -10);
    const double pxPerDeg = inner.width() / 60.0;

    const double centerHdg = m_headingDeg;
    const double startDeg = centerHdg - 30.0;

    for (int i = 0; i <= 60; i += 5) {
        double deg = startDeg + i;
        double x = inner.left() + (deg - startDeg) * pxPerDeg;

        double tickH = ((int)qRound(deg) % 10 == 0) ? inner.height() * 0.55 : inner.height() * 0.35;
        p.drawLine(QPointF(x, inner.bottom()), QPointF(x, inner.bottom() - tickH));

        if (((int)qRound(deg) % 30) == 0) {
            QString label;
            double d = wrap360(deg);
            if (qFuzzyCompare(d, 0.0) || qFuzzyCompare(d, 360.0)) label = "N";
            else if (qFuzzyCompare(d, 90.0)) label = "E";
            else if (qFuzzyCompare(d, 180.0)) label = "S";
            else if (qFuzzyCompare(d, 270.0)) label = "W";
            else label = QString::number((int)qRound(d));

            QFont f = p.font();
            f.setPointSizeF(r.height() * 0.17 * textMultiplier);
            p.setFont(f);

            p.drawText(QRectF(x - 22, inner.top(), 44, inner.height() * 0.6),
                       Qt::AlignHCenter | Qt::AlignVCenter, label);
        }
    }

    // Bigger readout box to safely hold a larger value.
    QRectF readout(r.center().x() - r.width() * 0.17,
                   r.center().y() - r.height() * 0.25,
                   r.width() * 0.34,
                   r.height() * 0.50);
    p.drawRect(readout);

    const QString headingText = QString::number(m_headingDeg, 'f', 1) + "°";
    drawFittedCenteredText(p, readout, headingText, r.height() * 0.60 * textMultiplier, 6.0);

    QFont f2 = p.font();
    f2.setPointSizeF(r.height() * 0.13 * textMultiplier);
    p.setFont(f2);
    p.drawText(QRectF(r.left(), r.bottom() + 2, r.width(), r.height() * 0.28),
               Qt::AlignHCenter | Qt::AlignTop, "HEADING");

    p.restore();
}

void HudWidget::drawAttitude(QPainter &p, const QRectF &r)
{
    p.save();
    p.setPen(hudPen(2.0));
    p.setBrush(Qt::NoBrush);

    QRectF circle = r;
    p.drawEllipse(circle);

    QPainterPath clip;
    clip.addEllipse(circle);
    p.setClipPath(clip);

    const double pxPerDeg = circle.height() / 30.0;
    const double horizonY = circle.center().y() + (-m_pitchDeg * pxPerDeg);

    QRectF skyRect(circle.left(), circle.top(), circle.width(), horizonY - circle.top());
    QRectF groundRect(circle.left(), horizonY, circle.width(), circle.bottom() - horizonY);

    p.fillRect(skyRect, QColor(20, 80, 140));
    p.fillRect(groundRect, QColor(45, 45, 45));

    p.save();
    p.translate(circle.center());
    p.rotate(-m_rollDeg);
    p.translate(-circle.center());

    p.setPen(hudPen(3.0));
    p.drawLine(QPointF(circle.left(), horizonY), QPointF(circle.right(), horizonY));

    p.setPen(hudPen(2.0));
    QFont f = p.font();
    f.setPointSizeF(circle.height() * 0.055 * textMultiplier);
    p.setFont(f);

    for (int deg = -30; deg <= 30; deg += 5) {
        if (deg == 0) continue;
        double y = horizonY - (deg * pxPerDeg);
        if (y < circle.top() - 20 || y > circle.bottom() + 20) continue;

        double halfLen = (qAbs(deg) % 10 == 0) ? circle.width() * 0.22 : circle.width() * 0.16;
        QPointF L(circle.center().x() - halfLen, y);
        QPointF R(circle.center().x() + halfLen, y);

        p.drawLine(L, R);

        if (qAbs(deg) % 10 == 0) {
            QString t = QString::number(qAbs(deg));
            QRectF leftText(L.x() - 30, y - 10, 28, 20);
            QRectF rightText(R.x() + 2, y - 10, 28, 20);
            p.drawText(leftText, Qt::AlignRight | Qt::AlignVCenter, t);
            p.drawText(rightText, Qt::AlignLeft  | Qt::AlignVCenter, t);
        }
    }

    p.restore();

    p.setClipping(false);

    p.setPen(hudPen(2.5));
    const QPointF c = circle.center();
    p.drawLine(QPointF(c.x() - circle.width() * 0.025, c.y()),
               QPointF(c.x() - circle.width() * 0.005, c.y()));
    p.drawLine(QPointF(c.x() + circle.width() * 0.005, c.y()),
               QPointF(c.x() + circle.width() * 0.025, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - circle.height() * 0.005),
               QPointF(c.x(), c.y() + circle.height() * 0.005));

    p.restore();
}

void HudWidget::drawAltitudeTape(QPainter &p, const QRectF &r)
{
    p.save();
    p.setPen(hudPen(2.0));
    p.setBrush(Qt::NoBrush);

    p.drawRoundedRect(r, 2, 2);

    QRectF inner = r.adjusted(r.width() * 0.12, r.height() * 0.08, -r.width() * 0.12, -r.height() * 0.08);

    const double spanFt = 1000.0;
    const double pxPerFt = inner.height() / spanFt;
    const double centerAlt = m_altitudeFt;

    p.setPen(hudPen(2.0));
    QFont f = p.font();
    f.setPointSizeF(r.height() * 0.07 * textMultiplier);
    p.setFont(f);

    for (int ft = -500; ft <= 500; ft += 50) {
        double alt = centerAlt + ft;
        double y = inner.center().y() + (-ft * pxPerFt);

        bool major = ((int)qRound(alt) % 200 == 0);
        bool med   = ((int)qRound(alt) % 100 == 0);

        double tickLen = major ? inner.width() * 0.55 : (med ? inner.width() * 0.40 : inner.width() * 0.25);

        p.drawLine(QPointF(inner.right() - tickLen, y), QPointF(inner.right(), y));

        if (med) {
            QString label = QString::number((int)qRound(alt / 10.0) * 10);
            p.drawText(QRectF(inner.left(), y - 10, inner.width() * 0.60, 20),
                       Qt::AlignLeft | Qt::AlignVCenter, label);
        }
    }

    // Slightly larger current-altitude box with fitted text to prevent clipping.
    QRectF box(r.left() + r.width() * 0.06,
               r.center().y() - r.height() * 0.12,
               r.width() * 0.88,
               r.height() * 0.24);
    p.setPen(hudPen(2.0));
    p.drawRect(box);

    const QString altitudeText = QString::number((int)qRound(m_altitudeFt));
    drawFittedCenteredText(p, box, altitudeText, r.height() * 0.22 * textMultiplier, 6.0);

    QFont f3 = p.font();
    f3.setPointSizeF(r.height() * 0.07 * textMultiplier);
    p.setFont(f3);
    p.drawText(QRectF(r.left(), r.bottom() + 4, r.width(), r.height() * 0.22),
               Qt::AlignHCenter | Qt::AlignTop, "ALTITUDE");

    p.restore();
}

void HudWidget::drawBottomReadouts(QPainter &p, const QRectF &r)
{
    p.save();
    p.setPen(hudPen(2.0));
    p.setBrush(Qt::NoBrush);

    p.drawRoundedRect(r, 2, 2);
    p.drawLine(QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.bottom()));

    QFont label = p.font();
    label.setPointSizeF(r.height() * 0.14 * textMultiplier);

    p.setFont(label);
    p.drawText(QRectF(r.left(), r.top() + 6, r.width() / 2, r.height() * 0.35),
               Qt::AlignHCenter | Qt::AlignVCenter, "ROLL");
    p.drawText(QRectF(r.center().x(), r.top() + 6, r.width() / 2, r.height() * 0.35),
               Qt::AlignHCenter | Qt::AlignVCenter, "PITCH");

    const QRectF leftValueRect (r.left(),       r.top() + r.height() * 0.35, r.width() / 2, r.height() * 0.55);
    const QRectF rightValueRect(r.center().x(), r.top() + r.height() * 0.35, r.width() / 2, r.height() * 0.55);

    const QString rollText  = QString("%1°").arg(m_rollDeg, 0, 'f', 1);
    const QString pitchText = QString("%1°").arg(m_pitchDeg, 0, 'f', 1);

    drawFittedCenteredText(p, leftValueRect,  rollText,  r.height() * 0.51 * textMultiplier, 6.0);
    drawFittedCenteredText(p, rightValueRect, pitchText, r.height() * 0.51 * textMultiplier, 6.0);

    p.restore();
}

void HudWidget::drawIconButtons(QPainter &p, const QRectF &r)
{
    p.save();
    p.setPen(hudPen(2.0));
    p.setBrush(Qt::NoBrush);

    QRectF a(r.left(), r.top(), r.width() * 0.45, r.height() * 0.60);
    QRectF b(r.left() + r.width() * 0.52, r.top(), r.width() * 0.45, r.height() * 0.60);

    p.drawRoundedRect(a, 4, 4);
    p.drawRoundedRect(b, 4, 4);

    p.drawArc(a.adjusted(10, 10, -10, -10), 0 * 16, 180 * 16);
    p.drawEllipse(b.center(), 6, 6);

    p.restore();
}