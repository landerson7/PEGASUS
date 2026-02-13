#pragma once
#include <QObject>

struct HudSample {
    double headingDeg = 272.5;
    double rollDeg    = 0.0;
    double pitchDeg   = 0.0;
    double altitudeFt = 35000.0;
    double vspeedFpm  = 0.0;
};

class DataSource : public QObject {
    Q_OBJECT
public:
    explicit DataSource(QObject* parent=nullptr) : QObject(parent) {}
    virtual ~DataSource() = default;

    // Called periodically to get latest sample (dummy now, GPIO later)
    virtual HudSample read() = 0;
};
