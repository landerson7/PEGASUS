#pragma once
#include "IDataSource.h"
#include <QElapsedTimer>
#include <QtMath>
#include <QDateTime>

class DummySource : public IDataSource {
public:
    DummySource() { t.start(); }

    double baseAltFt = 35000.0;
    double ampAltFt  = 400.0;
    double periodSec = 6.0;

    std::optional<HudSample> poll() override {
        const double s = t.elapsed() / 1000.0;

        HudSample out;
        out.tsMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();

        const double w = (periodSec <= 0.01) ? 0.0 : (2.0 * M_PI / periodSec);
        out.altitudeFt = baseAltFt + ampAltFt * std::sin(w * s);
        out.vspeedFpm  = (ampAltFt * w * std::cos(w * s)) * 60.0;

        out.headingDeg = std::fmod(270.0 + 10.0 * s, 360.0);
        out.rollDeg    = 8.0  * std::sin(0.6 * s);
        out.pitchDeg   = 4.0  * std::sin(0.45 * s);

        return out;
    }

private:
    QElapsedTimer t;
};
