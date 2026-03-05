#include "UartCborSource.h"

#include <QtCore/QCborValue>
#include <QtCore/QCborMap>
#include <QtCore/QCborArray>
#include <QtCore/QCborParserError>

#include <QtMath>
#include <cmath>

UartCborSource::UartCborSource(QObject* parent) : QObject(parent) {
    connect(&m_serial, &QSerialPort::readyRead, this, &UartCborSource::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &UartCborSource::onError);
}

bool UartCborSource::start(const QString& portName, int baud) {
    if (m_serial.isOpen()) m_serial.close();

    m_serial.setPortName(portName);
    m_serial.setBaudRate(baud);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial.open(QIODevice::ReadOnly)) {
        emit logLine(QString("UART open failed: %1").arg(m_serial.errorString()));
        return false;
    }

    emit logLine(QString("UART opened: %1 @ %2").arg(portName).arg(baud));
    m_buf.clear();
    m_state = State::FindSync;
    m_expectedLen = 0;
    m_expectedCrc = 0;
    m_payload.clear();
    return true;
}

void UartCborSource::stop() {
    if (m_serial.isOpen()) m_serial.close();
}

void UartCborSource::onError(QSerialPort::SerialPortError e) {
    if (e == QSerialPort::NoError) return;
    emit logLine(QString("UART error: %1").arg(m_serial.errorString()));
}

quint32 UartCborSource::readU32BE(const uchar* p) {
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint32 UartCborSource::crc32_ieee(const uchar* data, int len) {
    quint32 crc = 0xFFFFFFFFu;
    for (int i = 0; i < len; i++) {
        crc ^= quint32(data[i]);
        for (int b = 0; b < 8; b++) {
            quint32 mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

double UartCborSource::wrap360(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg < 0) deg += 360.0;
    return deg;
}

void UartCborSource::onReadyRead() {
    m_buf += m_serial.readAll();

    // Parse as much as possible.
    // We attempt binary frames first (DEV_MODE=0), but if we never find sync,
    // we fall back to parsing text lines (DEV_MODE=1).
    bool progressed = true;
    while (progressed) {
        progressed = false;

        if (tryParseBinaryFrame()) progressed = true;
        else if (tryParseTextLine()) progressed = true;

        // safety bound
        if (m_buf.size() > (1<<20)) {
            emit logLine("UART buffer too large; clearing");
            m_buf.clear();
            m_state = State::FindSync;
            break;
        }
    }
}

bool UartCborSource::tryParseTextLine() {
    // Look for newline-terminated line
    int nl = m_buf.indexOf('\n');
    if (nl < 0) return false;

    QByteArray line = m_buf.left(nl);
    m_buf.remove(0, nl + 1);
    line = line.trimmed();
    if (line.isEmpty()) return true;

    // Heuristic: dev line starts with "p=" usually
    if (!line.contains("p=") && !line.contains("alt=") && !line.contains("AX=")) {
        // Not our line; ignore but consumed
        return true;
    }

    HudSample s;
    if (parseDevLineToSample(line, s)) {
        computeAttitudeFallback(s);
        m_textLines++;
        emit sampleReady(s);
    }
    return true;
}

bool UartCborSource::tryParseBinaryFrame() {
    // Frame: [AA][55][len u32 BE][payload][crc u32 BE]

    while (true) {
        if (m_state == State::FindSync) {
            int idx = -1;
            for (int i = 0; i + 1 < m_buf.size(); ++i) {
                if ((uchar)m_buf[i] == SYNC0 && (uchar)m_buf[i+1] == SYNC1) { idx = i; break; }
            }
            if (idx < 0) {
                // no sync found; do not consume (text parser may handle it)
                return false;
            }
            if (idx > 0) m_buf.remove(0, idx);
            if (m_buf.size() < 2 + 4) return false;
            m_buf.remove(0, 2);
            m_state = State::ReadLen;
        }

        if (m_state == State::ReadLen) {
            if (m_buf.size() < 4) return false;
            m_expectedLen = readU32BE((const uchar*)m_buf.constData());
            m_buf.remove(0, 4);

            if (m_expectedLen == 0 || m_expectedLen > MAX_LEN) {
                m_badLen++;
                emit logLine(QString("Bad frame len=%1; resync").arg(m_expectedLen));
                m_state = State::FindSync;
                continue;
            }

            m_payload.clear();
            m_state = State::ReadPayload;
        }

        if (m_state == State::ReadPayload) {
            if ((quint32)m_buf.size() < m_expectedLen) return false;
            m_payload = m_buf.left((int)m_expectedLen);
            m_buf.remove(0, (int)m_expectedLen);
            m_state = State::ReadCrc;
        }

        if (m_state == State::ReadCrc) {
            if (m_buf.size() < 4) return false;
            m_expectedCrc = readU32BE((const uchar*)m_buf.constData());
            m_buf.remove(0, 4);

            const quint32 crc = crc32_ieee((const uchar*)m_payload.constData(), m_payload.size());
            if (crc != m_expectedCrc) {
                m_badCrc++;
                emit logLine(QString("CRC mismatch got=%1 exp=%2; resync")
                             .arg(crc, 8, 16, QChar('0'))
                             .arg(m_expectedCrc, 8, 16, QChar('0')));
                m_state = State::FindSync;
                continue;
            }

            HudSample s;
            if (!decodeCborToSample(m_payload, s)) {
                m_badCbor++;
                m_state = State::FindSync;
                continue;
            }

            // if ESP32 hasn't populated euler yet, compute attitude from raw IMU here
            computeAttitudeFallback(s);

            m_ok++;
            emit sampleReady(s);
            m_state = State::FindSync;
            return true; // parsed one full binary frame
        }
    }
}

static bool mapGetDouble(const QCborMap& m, const char* key, double& out) {
    QCborValue v = m.value(QCborValue(QString::fromUtf8(key)));
    if (v.isDouble()) { out = v.toDouble(); return true; }
    if (v.isInteger()) { out = (double)v.toInteger(); return true; }
    return false;
}

bool UartCborSource::decodeCborToSample(const QByteArray& payload, HudSample& out) {
    QCborParserError err;
    QCborValue root = QCborValue::fromCbor(payload, &err);
    if (err.error != QCborError::NoError || !root.isMap()) {
        emit logLine(QString("CBOR parse error: %1").arg((int)err.error));
        return false;
    }

    QCborMap m = root.toMap();

    // ts_us -> tsMs
    double ts_us = 0;
    if (mapGetDouble(m, "ts_us", ts_us)) out.tsMs = (long long)(ts_us / 1000.0);

    // baro/alt/vs
    double alt_m = 0, vs_mps = 0;
    mapGetDouble(m, "alt", alt_m);
    mapGetDouble(m, "vs",  vs_mps);

    out.altitudeFt = alt_m * 3.280839895;          // m -> ft
    out.vspeedFpm  = vs_mps * 196.8503937007874;   // m/s -> ft/min

    // baro map
    QCborValue baroV = m.value(QCborValue("baro"));
    if (baroV.isMap()) {
        QCborMap b = baroV.toMap();
        double p=0, T=0;
        mapGetDouble(b, "p", p);
        mapGetDouble(b, "T", T);
        out.pressureHpa = p;
        out.tempC = T;
    }

    // imu map
    QCborValue imuV = m.value(QCborValue("imu"));
    if (imuV.isMap()) {
        QCborMap im = imuV.toMap();
        mapGetDouble(im, "ax", out.ax);
        mapGetDouble(im, "ay", out.ay);
        mapGetDouble(im, "az", out.az);
        mapGetDouble(im, "gx", out.gx);
        mapGetDouble(im, "gy", out.gy);
        mapGetDouble(im, "gz", out.gz);
    }

    // mag map
    QCborValue magV = m.value(QCborValue("mag"));
    if (magV.isMap()) {
        QCborMap mg = magV.toMap();
        mapGetDouble(mg, "mx", out.mx);
        mapGetDouble(mg, "my", out.my);
        mapGetDouble(mg, "mz", out.mz);
    }

    // euler array (if ESP32 fills it later)
    QCborValue eulerV = m.value(QCborValue("euler"));
    if (eulerV.isArray()) {
        QCborArray a = eulerV.toArray();
        if (a.size() >= 3) {
            // assume radians from ESP32
            double roll  = a.at(0).toDouble();
            double pitch = a.at(1).toDouble();
            double yaw   = a.at(2).toDouble();

            // if it's actually degrees later, flip this off.
            out.rollDeg  = roll  * (180.0 / M_PI);
            out.pitchDeg = pitch * (180.0 / M_PI);
            out.headingDeg = wrap360(yaw * (180.0 / M_PI));
        }
    }

    return true;
}

static bool extractNumberAfter(const QByteArray& line, const QByteArray& key, double& out) {
    int idx = line.indexOf(key);
    if (idx < 0) return false;
    idx += key.size();

    // skip spaces
    while (idx < line.size() && line[idx] == ' ') idx++;

    // read number token
    int end = idx;
    while (end < line.size()) {
        char c = line[end];
        if ((c >= '0' && c <= '9') || c=='-' || c=='+' || c=='.' || c=='e' || c=='E') end++;
        else break;
    }
    if (end == idx) return false;

    bool ok=false;
    out = QByteArray(line.constData()+idx, end-idx).toDouble(&ok);
    return ok;
}

bool UartCborSource::parseDevLineToSample(const QByteArray& line, HudSample& out) {
    // Example:
    // p=1015.476 hPa  T=23.19 C  alt=-18.51 m  vs=-0.05 m/s  AX=-0.056 AY=-0.130 AZ=9.772  GX=0.038 ...

    double p=0, T=0, alt_m=0, vs_mps=0;

    extractNumberAfter(line, "p=", p);
    extractNumberAfter(line, "T=", T);
    extractNumberAfter(line, "alt=", alt_m);
    extractNumberAfter(line, "vs=", vs_mps);

    out.pressureHpa = p;
    out.tempC = T;

    out.altitudeFt = alt_m * 3.280839895;
    out.vspeedFpm  = vs_mps * 196.8503937007874;

    extractNumberAfter(line, "AX=", out.ax);
    extractNumberAfter(line, "AY=", out.ay);
    extractNumberAfter(line, "AZ=", out.az);

    extractNumberAfter(line, "GX=", out.gx);
    extractNumberAfter(line, "GY=", out.gy);
    extractNumberAfter(line, "GZ=", out.gz);

    extractNumberAfter(line, "MX=", out.mx);
    extractNumberAfter(line, "MY=", out.my);
    extractNumberAfter(line, "MZ=", out.mz);

    out.tsMs = 0; // DEV line doesn't carry time; ok for now
    return true;
}

// --- attitude fallback ---
// This gives you R/P from accelerometer and heading from tilt-comp mag.
// Good enough for demo; later you should do proper fusion on ESP32.
void UartCborSource::computeAttitudeFallback(HudSample& s) {
    // If ESP32 already provided non-zero euler, keep it.
    if (std::fabs(s.rollDeg) > 0.01 || std::fabs(s.pitchDeg) > 0.01 || std::fabs(s.headingDeg) > 0.01) {
        return;
    }

    // NOTE: Axis convention depends on your physical mounting.
    // This assumes:
    //  ax = +forward (m/s^2)
    //  ay = +right
    //  az = +up
    // If your az is "down", you'll flip signs.
    const double ax = s.ax;
    const double ay = s.ay;
    const double az = s.az;

    // Roll/Pitch from accel (radians)
    const double roll  = std::atan2(ay, az);
    const double pitch = std::atan2(-ax, std::sqrt(ay*ay + az*az));

    s.rollDeg  = roll * (180.0 / M_PI);
    s.pitchDeg = pitch * (180.0 / M_PI);

    // Tilt-compensated heading from magnetometer
    // Normalize mag is optional; we just use ratios.
    const double mx = s.my;
    const double my = s.mx;
    const double mz = -s.mz;

    const double cr = std::cos(roll),  sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);

    const double Xh = mx*cp + mz*sp;
    const double Yh = mx*sr*sp + my*cr - mz*sr*cp;

    double heading = std::atan2(Yh, Xh) * 180.0 / M_PI;

    if (heading < 0)
        heading += 360.0;

    // Orlando magnetic declination
    heading -= 6.3;

    if (heading < 0) heading += 360.0;
    if (heading >= 360) heading -= 360.0;

    // If it appears mirrored, flip sign:
    // heading = -heading;

    s.headingDeg = wrap360(heading);
}