#pragma once
#include <QWidget>

class HudWidget : public QWidget
{
    Q_OBJECT
public:
    explicit HudWidget(QWidget *parent = nullptr);

    void setHeadingDeg(double deg);
    void setRollDeg(double deg);
    void setPitchDeg(double deg);
    void setAltitudeFt(double ft);
    void setVSpeedFpm(double fpm);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    // State
    double m_headingDeg = 272.5;
    double m_rollDeg    = -2.8;
    double m_pitchDeg   = 2.2;
    double m_altitudeFt = 34959;
    double m_vspeedFpm  = -164;

    // Drawing helpers
    void drawHeadingTape(QPainter &p, const QRectF &r);
    void drawAttitude(QPainter &p, const QRectF &r);
    void drawAltitudeTape(QPainter &p, const QRectF &r);
    void drawBottomReadouts(QPainter &p, const QRectF &r);
    void drawIconButtons(QPainter &p, const QRectF &r);

    static double wrap360(double deg);
};
