// DataSource.h
#pragma once
#include <QObject>
#include "HudSample.h"

class DataSource : public QObject {
    Q_OBJECT
public:
    explicit DataSource(QObject* parent=nullptr) : QObject(parent) {}
    virtual ~DataSource() = default;

    // Called periodically to get latest sample
    virtual HudSample read() = 0;
};