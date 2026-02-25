#pragma once
#include <QObject>
#include <QByteArray>
#include <QSerialPort>
#include "HudSample.h"

// Reads ESP32 output in either mode:
// 1) DEV_MODE=0 binary frames: [AA][55][len u32 BE][CBOR][crc32 u32 BE]
// 2) DEV_MODE=1 text lines like: "p=1015.476 hPa  T=23.19 C  alt=-18.51 m ..."

class UartCborSource : public QObject {
    Q_OBJECT
public:
    explicit UartCborSource(QObject* parent=nullptr);

    bool start(const QString& portName, int baud=115200);
    void stop();
    bool isOpen() const { return m_serial.isOpen(); }

signals:
    void sampleReady(const HudSample& s);
    void logLine(const QString& s);

private slots:
    void onReadyRead();
    void onError(QSerialPort::SerialPortError e);

private:
    QSerialPort m_serial;
    QByteArray  m_buf;

    // --- Binary-frame parsing state ---
    enum class State { FindSync, ReadLen, ReadPayload, ReadCrc };
    State   m_state = State::FindSync;
    quint32 m_expectedLen = 0;
    QByteArray m_payload;
    quint32 m_expectedCrc = 0;

    // --- Stats (optional) ---
    quint64 m_ok=0, m_badCrc=0, m_badLen=0, m_badCbor=0, m_textLines=0;

    // helpers
    bool tryParseBinaryFrame();      // returns true if it consumed a full frame
    bool tryParseTextLine();         // returns true if it consumed one full line
    bool decodeCborToSample(const QByteArray& payload, HudSample& out);
    bool parseDevLineToSample(const QByteArray& line, HudSample& out);

    // math fallback if ESP sends euler zeros
    void computeAttitudeFallback(HudSample& s);

    static quint32 readU32BE(const uchar* p);
    static quint32 crc32_ieee(const uchar* data, int len);
    static double  wrap360(double deg);

    static constexpr uchar SYNC0 = 0xAA;
    static constexpr uchar SYNC1 = 0x55;
    static constexpr quint32 MAX_LEN = 4096;
};