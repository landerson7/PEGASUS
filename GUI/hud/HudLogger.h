#pragma once

#include <QObject>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMutex>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVector>

#include "HudSample.h"

class HudLogger : public QObject
{
    Q_OBJECT
public:
    explicit HudLogger(QObject* parent = nullptr);
    ~HudLogger() override;

    bool initialize(const QString& dbPath);
    bool beginSession(const QString& buildVersion,
                      const QString& gitBranch,
                      const QString& gitCommit,
                      const QString& notes = QString());
    void endSession(const QString& exitStatus, const QString& notes = QString());

    void logEvent(const QString& level,
                  const QString& component,
                  const QString& eventType,
                  const QString& message,
                  const QString& details = QString(),
                  const QString& errorCode = QString());

    void updateLatestSample(const HudSample& sample);
    void logFlightSample(const HudSample& sample);
    void recordDisplayMetrics(double displayMs, double displayRateHz);

    QString databasePath() const { return m_dbPath; }

private slots:
    void captureLatestSample();
    void flushPendingWrites();

private:
    struct EventRecord {
        QString level;
        QString component;
        QString eventType;
        QString message;
        QString details;
        QString errorCode;
        QString timestamp;
    };

    struct RollingStat {
        bool hasValue = false;
        double min = 0.0;
        double max = 0.0;
        double sum = 0.0;
        int count = 0;

        void update(double value);
        double avg() const;
    };

    struct TimingStats {
        RollingStat sensorRead;
        RollingStat decode;
        RollingStat display;
        RollingStat endToEnd;
    };

    static QString nowIsoUtc();
    static QVariant nullableDouble(double value);
    static QVariant nullableLongLong(long long value);

    bool createTables();
    bool createIndexes();
    bool configureDatabase();
    bool prepareStatements();
    bool prepareAndExec(QSqlQuery& query, const QString& context);
    void logInsertFailure(const QString& context, const QSqlError& error);
    void updateRollingStats(const HudSample& sample);
    void applyRollingStats(QSqlQuery& query) const;
    bool insertEventRecord(const EventRecord& event);
    bool insertFlightSampleRecord(const HudSample& sample);
    void queueEventRecord(const EventRecord& event, bool highPriority);
    void queueFlightSampleRecord(const HudSample& sample);
    void restorePendingWrites(const QVector<EventRecord>& events,
                              const QVector<HudSample>& samples);
    void startFlushTimerIfNeeded();

    QString m_connectionName;
    QString m_dbPath;
    QSqlDatabase m_db;
    QMutex m_mutex;
    QTimer m_sampleTimer;
    QTimer m_flushTimer;
    QSqlQuery m_insertSessionQuery;
    QSqlQuery m_updateSessionQuery;
    QSqlQuery m_insertEventQuery;
    QSqlQuery m_insertFlightSampleQuery;

    qint64 m_sessionId = -1;
    bool m_sessionActive = false;
    bool m_hasLatestSample = false;
    bool m_loggedStaleWarning = false;
    HudSample m_latestSample;
    qint64 m_lastSampleWallClockMs = 0;
    TimingStats m_timingStats;
    QVector<EventRecord> m_pendingEvents;
    QVector<HudSample> m_pendingSamples;
};
