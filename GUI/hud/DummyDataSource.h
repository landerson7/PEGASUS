#pragma once
#include "DataSource.h"
#include <QElapsedTimer>
#include <QtMath>
#include <cmath>


class DummyDataSource : public DataSource {
    Q_OBJECT
public:
    explicit DummyDataSource(QObject* parent=nullptr)
        : DataSource(parent)
    {
        m_t.start();
    }

    // Easy knobs you can tweak
    double baseAltFt   = 35000.0;
    double ampAltFt    = 300.0;     // altitude swing
    double periodSec   = 8.0;       // how fast it oscillates
    double baseHeading = 272.5;
    double headingRateDegPerSec = 8.0;

    HudSample read() override {
        const double tSec = m_t.elapsed() / 1000.0;

        HudSample s;

        // Altitude: smooth sine wave
        const double w = (periodSec <= 0.01) ? 0.0 : (2.0 * M_PI / periodSec);
        s.altitudeFt = baseAltFt + ampAltFt * std::sin(w * tSec);

        // Vertical speed from derivative of altitude (ft/s -> fpm)
        // d/dt: amp*w*cos(wt) ft/s  => *60 => fpm
        s.vspeedFpm = (ampAltFt * w * std::cos(w * tSec)) * 60.0;

        // Optional motion for visuals
        s.headingDeg = std::fmod(baseHeading + headingRateDegPerSec * tSec, 360.0);
        s.rollDeg  = 8.0 * std::sin(0.6 * tSec);
        s.pitchDeg = 4.0 * std::sin(0.45 * tSec);

        return s;
    }

private:
    QElapsedTimer m_t;
};
