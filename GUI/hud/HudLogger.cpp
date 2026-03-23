#include "HudLogger.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QSqlError>
#include <QSqlQuery>

#include <cmath>
#include <limits>

namespace {
constexpr int kSampleCaptureIntervalMs = 1000;
constexpr int kFlushIntervalMs = 250;
constexpr int kBusyTimeoutMs = 3000;
constexpr qint64 kStaleThresholdMs = 1500;
constexpr double kDisplayThresholdMs = 33.3;
constexpr int kMaxBufferedEventsBeforeImmediateFlush = 8;
constexpr int kMaxBufferedSamplesBeforeImmediateFlush = 4;
}

HudLogger::HudLogger(QObject* parent)
    : QObject(parent)
{
    m_connectionName = QStringLiteral("HudLoggerConnection_%1")
        .arg(reinterpret_cast<quintptr>(this));

    m_sampleTimer.setInterval(kSampleCaptureIntervalMs);
    connect(&m_sampleTimer, &QTimer::timeout, this, &HudLogger::captureLatestSample);

    m_flushTimer.setInterval(kFlushIntervalMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &HudLogger::flushPendingWrites);
}

HudLogger::~HudLogger()
{
    endSession(QStringLiteral("shutdown"), QStringLiteral("Logger destroyed"));

    if (m_db.isValid()) {
        const QString connectionName = m_connectionName;
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
    }
}

bool HudLogger::initialize(const QString& dbPath)
{
    QMutexLocker locker(&m_mutex);

    if (m_db.isValid() && m_db.isOpen()) {
        return true;
    }

    QFileInfo dbInfo(dbPath);
    QDir dir = dbInfo.dir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        qCritical().noquote() << "Failed to create HUD log directory:" << dir.absolutePath();
        return false;
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath);
    m_dbPath = dbPath;

    if (!m_db.open()) {
        qCritical().noquote() << "Failed to open HUD SQLite database:"
                              << m_db.lastError().text();
        return false;
    }

    if (!configureDatabase() || !createTables() || !createIndexes() || !prepareStatements()) {
        return false;
    }

    return true;
}

bool HudLogger::beginSession(const QString& buildVersion,
                             const QString& gitBranch,
                             const QString& gitCommit,
                             const QString& notes)
{
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) {
        qCritical() << "beginSession called before SQLite database initialization";
        return false;
    }

    if (m_sessionActive) {
        return true;
    }

    m_insertSessionQuery.finish();
    m_insertSessionQuery.bindValue(QStringLiteral(":start_time"), nowIsoUtc());
    m_insertSessionQuery.bindValue(QStringLiteral(":build_version"), buildVersion);
    m_insertSessionQuery.bindValue(QStringLiteral(":git_branch"), gitBranch);
    m_insertSessionQuery.bindValue(QStringLiteral(":git_commit"), gitCommit);
    m_insertSessionQuery.bindValue(QStringLiteral(":notes"), notes);

    if (!prepareAndExec(m_insertSessionQuery, QStringLiteral("insert session"))) {
        return false;
    }

    m_sessionId = m_insertSessionQuery.lastInsertId().toLongLong();
    m_sessionActive = true;
    m_loggedStaleWarning = false;
    m_sampleTimer.start();
    return true;
}

void HudLogger::endSession(const QString& exitStatus, const QString& notes)
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_sessionActive || !m_db.isOpen()) {
            return;
        }

        if (m_sampleTimer.isActive()) {
            m_sampleTimer.stop();
        }
        if (m_flushTimer.isActive()) {
            m_flushTimer.stop();
        }
    }

    captureLatestSample();
    flushPendingWrites();

    QMutexLocker locker(&m_mutex);
    if (!m_sessionActive || !m_db.isOpen()) {
        return;
    }

    m_updateSessionQuery.finish();
    m_updateSessionQuery.bindValue(QStringLiteral(":end_time"), nowIsoUtc());
    m_updateSessionQuery.bindValue(QStringLiteral(":exit_status"), exitStatus);
    m_updateSessionQuery.bindValue(QStringLiteral(":notes"), notes);
    m_updateSessionQuery.bindValue(QStringLiteral(":id"), m_sessionId);
    prepareAndExec(m_updateSessionQuery, QStringLiteral("update session end"));

    m_sessionActive = false;
    m_sessionId = -1;
    m_hasLatestSample = false;
    m_pendingEvents.clear();
    m_pendingSamples.clear();
}

void HudLogger::logEvent(const QString& level,
                         const QString& component,
                         const QString& eventType,
                         const QString& message,
                         const QString& details,
                         const QString& errorCode)
{
    EventRecord event{level, component, eventType, message, details, errorCode, nowIsoUtc()};

    {
        QMutexLocker locker(&m_mutex);
        if (!m_sessionActive || !m_db.isOpen()) {
            qWarning().noquote() << QStringLiteral("[%1] %2/%3: %4")
                                    .arg(level, component, eventType, message);
            if (!details.isEmpty()) {
                qWarning().noquote() << details;
            }
            return;
        }
    }

    const bool highPriority = (level == QStringLiteral("ERROR")) ||
                              (level == QStringLiteral("WARNING"));
    queueEventRecord(event, highPriority);
}

void HudLogger::updateLatestSample(const HudSample& sample)
{
    QMutexLocker locker(&m_mutex);
    m_latestSample = sample;
    m_hasLatestSample = true;
    m_lastSampleWallClockMs = QDateTime::currentMSecsSinceEpoch();
    m_loggedStaleWarning = false;
}

void HudLogger::logFlightSample(const HudSample& sample)
{
    queueFlightSampleRecord(sample);
}

void HudLogger::recordDisplayMetrics(double displayMs, double displayRateHz)
{
    bool shouldLogThreshold = false;
    QString thresholdDetails;

    {
        QMutexLocker locker(&m_mutex);
        if (!m_hasLatestSample) {
            return;
        }

        m_latestSample.display_ms = displayMs;
        m_latestSample.display_rate_hz = displayRateHz;
        if (!std::isnan(m_latestSample.sensor_read_ms) && !std::isnan(m_latestSample.decode_ms)) {
            m_latestSample.end_to_end_ms =
                m_latestSample.sensor_read_ms + m_latestSample.decode_ms + displayMs;
        }

        if (displayMs > kDisplayThresholdMs) {
            shouldLogThreshold = true;
            thresholdDetails = QStringLiteral("display_ms=%1, threshold_ms=%2, display_rate_hz=%3")
                                   .arg(displayMs, 0, 'f', 3)
                                   .arg(kDisplayThresholdMs, 0, 'f', 1)
                                   .arg(displayRateHz, 0, 'f', 3);
        }
    }

    if (shouldLogThreshold) {
        logEvent(QStringLiteral("ERROR"),
                 QStringLiteral("HudWidget"),
                 QStringLiteral("display_timing_threshold"),
                 QStringLiteral("Display update exceeded threshold"),
                 thresholdDetails);
    }
}

void HudLogger::captureLatestSample()
{
    HudSample sample;
    bool shouldQueueSample = false;
    bool shouldLogStale = false;

    {
        QMutexLocker locker(&m_mutex);
        if (!m_sessionActive || !m_hasLatestSample) {
            return;
        }

        sample = m_latestSample;
        const qint64 ageMs = QDateTime::currentMSecsSinceEpoch() - m_lastSampleWallClockMs;
        sample.stale_data_flag = ageMs > kStaleThresholdMs;
        sample.data_valid = true;
        sample.data_fresh = !sample.stale_data_flag;
        shouldQueueSample = true;

        if (sample.stale_data_flag && !m_loggedStaleWarning) {
            shouldLogStale = true;
            m_loggedStaleWarning = true;
        }
    }

    if (shouldLogStale) {
        logEvent(QStringLiteral("WARNING"),
                 QStringLiteral("HudLogger"),
                 QStringLiteral("stale_data"),
                 QStringLiteral("Latest sample is stale"),
                 QStringLiteral("No new sample for more than %1 ms").arg(kStaleThresholdMs));
    }

    if (shouldQueueSample) {
        queueFlightSampleRecord(sample);
    }
}

void HudLogger::flushPendingWrites()
{
    QVector<EventRecord> events;
    QVector<HudSample> samples;

    {
        QMutexLocker locker(&m_mutex);
        if (!m_db.isOpen() || (!m_sessionActive && m_pendingEvents.isEmpty() && m_pendingSamples.isEmpty())) {
            return;
        }

        if (m_pendingEvents.isEmpty() && m_pendingSamples.isEmpty()) {
            if (m_flushTimer.isActive()) {
                m_flushTimer.stop();
            }
            return;
        }

        events = m_pendingEvents;
        samples = m_pendingSamples;
        m_pendingEvents.clear();
        m_pendingSamples.clear();
    }

    if (!m_db.transaction()) {
        logInsertFailure(QStringLiteral("begin transaction"), m_db.lastError());
        restorePendingWrites(events, samples);
        return;
    }

    bool ok = true;
    for (const EventRecord& event : events) {
        if (!insertEventRecord(event)) {
            ok = false;
            break;
        }
    }

    for (const HudSample& sample : samples) {
        if (!ok || !insertFlightSampleRecord(sample)) {
            ok = false;
            break;
        }
    }

    if (ok) {
        if (!m_db.commit()) {
            logInsertFailure(QStringLiteral("commit transaction"), m_db.lastError());
            ok = false;
        }
    } else {
        m_db.rollback();
    }

    if (!ok) {
        restorePendingWrites(events, samples);
        return;
    }

    if (m_flushTimer.isActive()) {
        QMutexLocker locker(&m_mutex);
        if (m_pendingEvents.isEmpty() && m_pendingSamples.isEmpty()) {
            m_flushTimer.stop();
        }
    }
}

bool HudLogger::configureDatabase()
{
    const QStringList pragmas = {
        QStringLiteral("PRAGMA foreign_keys = ON"),
        QStringLiteral("PRAGMA journal_mode = WAL"),
        QStringLiteral("PRAGMA synchronous = NORMAL"),
        QStringLiteral("PRAGMA wal_autocheckpoint = 1000"),
        QStringLiteral("PRAGMA busy_timeout = %1").arg(kBusyTimeoutMs),
        QStringLiteral("PRAGMA temp_store = MEMORY")
    };

    for (const QString& pragma : pragmas) {
        QSqlQuery query(m_db);
        if (!query.exec(pragma)) {
            logInsertFailure(QStringLiteral("configure database"), query.lastError());
            return false;
        }
    }

    return true;
}

bool HudLogger::prepareStatements()
{
    m_insertSessionQuery = QSqlQuery(m_db);
    m_insertSessionQuery.prepare(QStringLiteral(
        "INSERT INTO sessions (start_time, build_version, git_branch, git_commit, notes) "
        "VALUES (:start_time, :build_version, :git_branch, :git_commit, :notes)"));
    if (m_insertSessionQuery.lastError().isValid()) {
        logInsertFailure(QStringLiteral("prepare insert session"), m_insertSessionQuery.lastError());
        return false;
    }

    m_updateSessionQuery = QSqlQuery(m_db);
    m_updateSessionQuery.prepare(QStringLiteral(
        "UPDATE sessions "
        "SET end_time = :end_time, exit_status = :exit_status, notes = :notes "
        "WHERE id = :id"));
    if (m_updateSessionQuery.lastError().isValid()) {
        logInsertFailure(QStringLiteral("prepare update session"), m_updateSessionQuery.lastError());
        return false;
    }

    m_insertEventQuery = QSqlQuery(m_db);
    m_insertEventQuery.prepare(QStringLiteral(
        "INSERT INTO event_logs "
        "(session_id, timestamp, level, component, event_type, message, details, error_code) "
        "VALUES (:session_id, :timestamp, :level, :component, :event_type, :message, :details, :error_code)"));
    if (m_insertEventQuery.lastError().isValid()) {
        logInsertFailure(QStringLiteral("prepare insert event"), m_insertEventQuery.lastError());
        return false;
    }

    m_insertFlightSampleQuery = QSqlQuery(m_db);
    m_insertFlightSampleQuery.prepare(QStringLiteral(
        "INSERT INTO flight_samples ("
        "session_id, timestamp, tsMs, altitudeFt, vspeedFpm, pressureHpa, tempC, "
        "ax, ay, az, gx, gy, gz, mx, my, mz, rollDeg, pitchDeg, headingDeg, "
        "sensor_read_ms, decode_ms, display_ms, end_to_end_ms, sensor_rate_hz, display_rate_hz, "
        "invalid_packets, dropped_packets, stale_data_flag, data_valid, data_fresh, "
        "altitudeFt_time_meas, vspeedFpm_time_meas, pressureHpa_time_meas, tempC_time_meas, "
        "ax_time_meas, ay_time_meas, az_time_meas, gx_time_meas, gy_time_meas, gz_time_meas, "
        "mx_time_meas, my_time_meas, mz_time_meas, rollDeg_time_meas, pitchDeg_time_meas, headingDeg_time_meas, "
        "sensor_read_ms_min, sensor_read_ms_max, sensor_read_ms_avg, "
        "decode_ms_min, decode_ms_max, decode_ms_avg, "
        "display_ms_min, display_ms_max, display_ms_avg, "
        "end_to_end_ms_min, end_to_end_ms_max, end_to_end_ms_avg"
        ") VALUES ("
        ":session_id, :timestamp, :tsMs, :altitudeFt, :vspeedFpm, :pressureHpa, :tempC, "
        ":ax, :ay, :az, :gx, :gy, :gz, :mx, :my, :mz, :rollDeg, :pitchDeg, :headingDeg, "
        ":sensor_read_ms, :decode_ms, :display_ms, :end_to_end_ms, :sensor_rate_hz, :display_rate_hz, "
        ":invalid_packets, :dropped_packets, :stale_data_flag, :data_valid, :data_fresh, "
        ":altitudeFt_time_meas, :vspeedFpm_time_meas, :pressureHpa_time_meas, :tempC_time_meas, "
        ":ax_time_meas, :ay_time_meas, :az_time_meas, :gx_time_meas, :gy_time_meas, :gz_time_meas, "
        ":mx_time_meas, :my_time_meas, :mz_time_meas, :rollDeg_time_meas, :pitchDeg_time_meas, :headingDeg_time_meas, "
        ":sensor_read_ms_min, :sensor_read_ms_max, :sensor_read_ms_avg, "
        ":decode_ms_min, :decode_ms_max, :decode_ms_avg, "
        ":display_ms_min, :display_ms_max, :display_ms_avg, "
        ":end_to_end_ms_min, :end_to_end_ms_max, :end_to_end_ms_avg)"));
    if (m_insertFlightSampleQuery.lastError().isValid()) {
        logInsertFailure(QStringLiteral("prepare insert flight sample"),
                         m_insertFlightSampleQuery.lastError());
        return false;
    }

    return true;
}

bool HudLogger::insertEventRecord(const EventRecord& event)
{
    m_insertEventQuery.finish();
    m_insertEventQuery.bindValue(QStringLiteral(":session_id"), m_sessionId);
    m_insertEventQuery.bindValue(QStringLiteral(":timestamp"), event.timestamp);
    m_insertEventQuery.bindValue(QStringLiteral(":level"), event.level);
    m_insertEventQuery.bindValue(QStringLiteral(":component"), event.component);
    m_insertEventQuery.bindValue(QStringLiteral(":event_type"), event.eventType);
    m_insertEventQuery.bindValue(QStringLiteral(":message"), event.message);
    m_insertEventQuery.bindValue(QStringLiteral(":details"), event.details);
    m_insertEventQuery.bindValue(QStringLiteral(":error_code"), event.errorCode);
    return prepareAndExec(m_insertEventQuery, QStringLiteral("insert event log"));
}

bool HudLogger::insertFlightSampleRecord(const HudSample& sample)
{
    updateRollingStats(sample);

    m_insertFlightSampleQuery.finish();
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":session_id"), m_sessionId);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":timestamp"), nowIsoUtc());
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":tsMs"), nullableLongLong(sample.tsMs));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":altitudeFt"), sample.altitudeFt);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":vspeedFpm"), sample.vspeedFpm);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":pressureHpa"), sample.pressureHpa);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":tempC"), sample.tempC);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":ax"), sample.ax);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":ay"), sample.ay);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":az"), sample.az);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":gx"), sample.gx);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":gy"), sample.gy);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":gz"), sample.gz);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":mx"), sample.mx);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":my"), sample.my);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":mz"), sample.mz);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":rollDeg"), sample.rollDeg);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":pitchDeg"), sample.pitchDeg);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":headingDeg"), sample.headingDeg);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":sensor_read_ms"), nullableDouble(sample.sensor_read_ms));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":decode_ms"), nullableDouble(sample.decode_ms));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":display_ms"), nullableDouble(sample.display_ms));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":end_to_end_ms"), nullableDouble(sample.end_to_end_ms));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":sensor_rate_hz"), nullableDouble(sample.sensor_rate_hz));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":display_rate_hz"), nullableDouble(sample.display_rate_hz));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":invalid_packets"), sample.invalid_packets);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":dropped_packets"), sample.dropped_packets);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":stale_data_flag"), sample.stale_data_flag ? 1 : 0);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":data_valid"), sample.data_valid ? 1 : 0);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":data_fresh"), sample.data_fresh ? 1 : 0);
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":altitudeFt_time_meas"), nullableDouble(sample.altitudeFt_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":vspeedFpm_time_meas"), nullableDouble(sample.vspeedFpm_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":pressureHpa_time_meas"), nullableDouble(sample.pressureHpa_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":tempC_time_meas"), nullableDouble(sample.tempC_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":ax_time_meas"), nullableDouble(sample.ax_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":ay_time_meas"), nullableDouble(sample.ay_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":az_time_meas"), nullableDouble(sample.az_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":gx_time_meas"), nullableDouble(sample.gx_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":gy_time_meas"), nullableDouble(sample.gy_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":gz_time_meas"), nullableDouble(sample.gz_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":mx_time_meas"), nullableDouble(sample.mx_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":my_time_meas"), nullableDouble(sample.my_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":mz_time_meas"), nullableDouble(sample.mz_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":rollDeg_time_meas"), nullableDouble(sample.rollDeg_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":pitchDeg_time_meas"), nullableDouble(sample.pitchDeg_time_meas));
    m_insertFlightSampleQuery.bindValue(QStringLiteral(":headingDeg_time_meas"), nullableDouble(sample.headingDeg_time_meas));
    applyRollingStats(m_insertFlightSampleQuery);
    return prepareAndExec(m_insertFlightSampleQuery, QStringLiteral("insert flight sample"));
}

void HudLogger::queueEventRecord(const EventRecord& event, bool highPriority)
{
    bool shouldFlushSoon = false;
    bool shouldScheduleImmediateFlush = false;

    {
        QMutexLocker locker(&m_mutex);
        m_pendingEvents.push_back(event);
        shouldFlushSoon = true;
        shouldScheduleImmediateFlush = highPriority ||
            (m_pendingEvents.size() >= kMaxBufferedEventsBeforeImmediateFlush);
    }

    if (shouldFlushSoon) {
        startFlushTimerIfNeeded();
    }
    if (shouldScheduleImmediateFlush) {
        QMetaObject::invokeMethod(this, "flushPendingWrites", Qt::QueuedConnection);
    }
}

void HudLogger::queueFlightSampleRecord(const HudSample& sample)
{
    bool shouldScheduleImmediateFlush = false;

    {
        QMutexLocker locker(&m_mutex);
        m_pendingSamples.push_back(sample);
        shouldScheduleImmediateFlush = m_pendingSamples.size() >= kMaxBufferedSamplesBeforeImmediateFlush;
    }

    startFlushTimerIfNeeded();
    if (shouldScheduleImmediateFlush) {
        QMetaObject::invokeMethod(this, "flushPendingWrites", Qt::QueuedConnection);
    }
}

void HudLogger::restorePendingWrites(const QVector<EventRecord>& events,
                                     const QVector<HudSample>& samples)
{
    {
        QMutexLocker locker(&m_mutex);

        QVector<EventRecord> restoredEvents;
        restoredEvents.reserve(events.size() + m_pendingEvents.size());
        restoredEvents += events;
        restoredEvents += m_pendingEvents;
        m_pendingEvents = restoredEvents;

        QVector<HudSample> restoredSamples;
        restoredSamples.reserve(samples.size() + m_pendingSamples.size());
        restoredSamples += samples;
        restoredSamples += m_pendingSamples;
        m_pendingSamples = restoredSamples;
    }

    startFlushTimerIfNeeded();
}

void HudLogger::startFlushTimerIfNeeded()
{
    QMutexLocker locker(&m_mutex);
    if ((m_pendingEvents.isEmpty() && m_pendingSamples.isEmpty()) || m_flushTimer.isActive()) {
        return;
    }
    m_flushTimer.start();
}

QString HudLogger::nowIsoUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QVariant HudLogger::nullableDouble(double value)
{
    if (std::isnan(value)) {
        return QVariant(QVariant::Double);
    }
    return value;
}

QVariant HudLogger::nullableLongLong(long long value)
{
    if (value <= 0) {
        return QVariant(QVariant::LongLong);
    }
    return QVariant::fromValue<qlonglong>(value);
}

bool HudLogger::createTables()
{
    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sessions ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "start_time TEXT NOT NULL,"
            "end_time TEXT,"
            "build_version TEXT,"
            "git_branch TEXT,"
            "git_commit TEXT,"
            "exit_status TEXT,"
            "notes TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS event_logs ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "session_id INTEGER NOT NULL,"
            "timestamp TEXT NOT NULL,"
            "level TEXT NOT NULL,"
            "component TEXT NOT NULL,"
            "event_type TEXT NOT NULL,"
            "message TEXT NOT NULL,"
            "details TEXT,"
            "error_code TEXT,"
            "FOREIGN KEY(session_id) REFERENCES sessions(id))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS flight_samples ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "session_id INTEGER NOT NULL,"
            "timestamp TEXT NOT NULL,"
            "tsMs INTEGER,"
            "altitudeFt REAL,"
            "vspeedFpm REAL,"
            "pressureHpa REAL,"
            "tempC REAL,"
            "ax REAL,"
            "ay REAL,"
            "az REAL,"
            "gx REAL,"
            "gy REAL,"
            "gz REAL,"
            "mx REAL,"
            "my REAL,"
            "mz REAL,"
            "rollDeg REAL,"
            "pitchDeg REAL,"
            "headingDeg REAL,"
            "sensor_read_ms REAL,"
            "decode_ms REAL,"
            "display_ms REAL,"
            "end_to_end_ms REAL,"
            "sensor_rate_hz REAL,"
            "display_rate_hz REAL,"
            "invalid_packets INTEGER,"
            "dropped_packets INTEGER,"
            "stale_data_flag INTEGER,"
            "data_valid INTEGER,"
            "data_fresh INTEGER,"
            "altitudeFt_time_meas REAL,"
            "vspeedFpm_time_meas REAL,"
            "pressureHpa_time_meas REAL,"
            "tempC_time_meas REAL,"
            "ax_time_meas REAL,"
            "ay_time_meas REAL,"
            "az_time_meas REAL,"
            "gx_time_meas REAL,"
            "gy_time_meas REAL,"
            "gz_time_meas REAL,"
            "mx_time_meas REAL,"
            "my_time_meas REAL,"
            "mz_time_meas REAL,"
            "rollDeg_time_meas REAL,"
            "pitchDeg_time_meas REAL,"
            "headingDeg_time_meas REAL,"
            "sensor_read_ms_min REAL,"
            "sensor_read_ms_max REAL,"
            "sensor_read_ms_avg REAL,"
            "decode_ms_min REAL,"
            "decode_ms_max REAL,"
            "decode_ms_avg REAL,"
            "display_ms_min REAL,"
            "display_ms_max REAL,"
            "display_ms_avg REAL,"
            "end_to_end_ms_min REAL,"
            "end_to_end_ms_max REAL,"
            "end_to_end_ms_avg REAL,"
            "FOREIGN KEY(session_id) REFERENCES sessions(id))")
    };

    for (const QString& statement : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(statement)) {
            logInsertFailure(QStringLiteral("create tables"), query.lastError());
            return false;
        }
    }

    return true;
}

bool HudLogger::createIndexes()
{
    const QStringList statements = {
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_flight_samples_session_timestamp "
            "ON flight_samples(session_id, timestamp)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_event_logs_session_timestamp "
            "ON event_logs(session_id, timestamp)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_event_logs_level "
            "ON event_logs(level)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_event_logs_component "
            "ON event_logs(component)")
    };

    for (const QString& statement : statements) {
        QSqlQuery query(m_db);
        if (!query.exec(statement)) {
            logInsertFailure(QStringLiteral("create indexes"), query.lastError());
            return false;
        }
    }

    return true;
}

bool HudLogger::prepareAndExec(QSqlQuery& query, const QString& context)
{
    if (!query.exec()) {
        logInsertFailure(context, query.lastError());
        return false;
    }
    return true;
}

void HudLogger::logInsertFailure(const QString& context, const QSqlError& error)
{
    qCritical().noquote() << QStringLiteral("SQLite failure during %1: %2")
                             .arg(context, error.text());
}

void HudLogger::updateRollingStats(const HudSample& sample)
{
    if (!std::isnan(sample.sensor_read_ms)) {
        m_timingStats.sensorRead.update(sample.sensor_read_ms);
    }
    if (!std::isnan(sample.decode_ms)) {
        m_timingStats.decode.update(sample.decode_ms);
    }
    if (!std::isnan(sample.display_ms)) {
        m_timingStats.display.update(sample.display_ms);
    }
    if (!std::isnan(sample.end_to_end_ms)) {
        m_timingStats.endToEnd.update(sample.end_to_end_ms);
    }
}

void HudLogger::applyRollingStats(QSqlQuery& query) const
{
    query.bindValue(QStringLiteral(":sensor_read_ms_min"),
                    m_timingStats.sensorRead.hasValue ? QVariant(m_timingStats.sensorRead.min)
                                                      : QVariant(QVariant::Double));
    query.bindValue(QStringLiteral(":sensor_read_ms_max"),
                    m_timingStats.sensorRead.hasValue ? QVariant(m_timingStats.sensorRead.max)
                                                      : QVariant(QVariant::Double));
    query.bindValue(QStringLiteral(":sensor_read_ms_avg"),
                    nullableDouble(m_timingStats.sensorRead.avg()));

    query.bindValue(QStringLiteral(":decode_ms_min"),
                    m_timingStats.decode.hasValue ? QVariant(m_timingStats.decode.min)
                                                  : QVariant(QVariant::Double));
    query.bindValue(QStringLiteral(":decode_ms_max"),
                    m_timingStats.decode.hasValue ? QVariant(m_timingStats.decode.max)
                                                  : QVariant(QVariant::Double));
    query.bindValue(QStringLiteral(":decode_ms_avg"),
                    nullableDouble(m_timingStats.decode.avg()));

    query.bindValue(QStringLiteral(":display_ms_min"),
                    m_timingStats.display.hasValue ? QVariant(m_timingStats.display.min)
                                                   : QVariant(QVariant::Double));
    query.bindValue(QStringLiteral(":display_ms_max"),
                    m_timingStats.display.hasValue ? QVariant(m_timingStats.display.max)
                                                   : QVariant(QVariant::Double));
    query.bindValue(QStringLiteral(":display_ms_avg"),
                    nullableDouble(m_timingStats.display.avg()));

    query.bindValue(QStringLiteral(":end_to_end_ms_min"),
                    m_timingStats.endToEnd.hasValue ? QVariant(m_timingStats.endToEnd.min)
                                                    : QVariant(QVariant::Double));
    query.bindValue(QStringLiteral(":end_to_end_ms_max"),
                    m_timingStats.endToEnd.hasValue ? QVariant(m_timingStats.endToEnd.max)
                                                    : QVariant(QVariant::Double));
    query.bindValue(QStringLiteral(":end_to_end_ms_avg"),
                    nullableDouble(m_timingStats.endToEnd.avg()));
}

void HudLogger::RollingStat::update(double value)
{
    if (!hasValue) {
        hasValue = true;
        min = value;
        max = value;
        sum = value;
        count = 1;
        return;
    }

    if (value < min) min = value;
    if (value > max) max = value;
    sum += value;
    ++count;
}

double HudLogger::RollingStat::avg() const
{
    if (!hasValue || count <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return sum / static_cast<double>(count);
}
