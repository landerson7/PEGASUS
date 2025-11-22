#include "ssd1306.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QFont>

#include <vector>
#include <chrono>
#include <thread>
#include <iostream>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <iomanip>

// POSIX / serial
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>

// CBOR / JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// ---------- Serial config ----------

static const char* SERIAL_PORT = "/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0";   // change to "/dev/ttyACM0" if needed
static const int   SERIAL_BAUD = B115200;

// Open and configure serial port (8N1, raw)
static int openSerial(const char* dev) {
    int fd = ::open(dev, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        std::perror("open serial");
        throw std::runtime_error("Failed to open serial port");
    }

    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        std::perror("tcgetattr");
        ::close(fd);
        throw std::runtime_error("tcgetattr failed");
    }

    cfsetospeed(&tty, SERIAL_BAUD);
    cfsetispeed(&tty, SERIAL_BAUD);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8 bits
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag  = 0;                           // raw
    tty.c_oflag  = 0;
    tty.c_cc[VMIN]  = 1;                        // block until at least 1 byte
    tty.c_cc[VTIME] = 5;                        // 0.5s timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);     // no XON/XOFF
    tty.c_cflag |= (CLOCAL | CREAD);            // enable receiver
    tty.c_cflag &= ~(PARENB | PARODD);          // no parity
    tty.c_cflag &= ~CSTOPB;                     // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;                    // no HW flow control

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::perror("tcsetattr");
        ::close(fd);
        throw std::runtime_error("tcsetattr failed");
    }

    // 🔴 Add this:
    tcflush(fd, TCIFLUSH);   // discard any unread input

    return fd;
}

static void readExact(int fd, uint8_t* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = ::read(fd, buf + off, n - off);
        if (r < 0) {
            if (errno == EINTR) continue;
            std::perror("read");
            throw std::runtime_error("serial read error");
        }
        if (r == 0) {
            throw std::runtime_error("serial EOF");
        }
        off += static_cast<size_t>(r);
    }
}

// Read one CBOR packet, decode, compute altitude (ft).
// Returns true if successful and fills altitudeFt.
static bool readAltitudeFromEsp32(int fd, double &altitudeFt) {
    // Sliding 4-byte window for length prefix
    uint8_t window[4];
    // Initial fill
    readExact(fd, window, 4);

    while (true) {
        // Interpret current 4-byte window as big-endian length
        uint32_t lenNet = 0;
        std::memcpy(&lenNet, window, 4);
        uint32_t payloadLen = ntohl(lenNet);

        // We expect a small CBOR map, ~40–80 bytes
        if (payloadLen > 0 && payloadLen < 256) {
            // Try reading that many bytes
            std::vector<uint8_t> payload(payloadLen);
            readExact(fd, payload.data(), payloadLen);

            try {
                json j = json::from_cbor(payload);

                if (!j.contains("pressure")) {
                    std::cerr << "CBOR missing 'pressure': " << j.dump() << "\n";
                    // keep scanning for a valid frame
                } else {
                    double pressure_hPa = j["pressure"].get<double>();

                    static bool   baselineSet = false;
                    static double p0_hPa      = 1013.25;

                    if (!baselineSet) {
                        p0_hPa = pressure_hPa;
                        baselineSet = true;
                        std::cout << "Baseline pressure set to " << p0_hPa << " hPa\n";
                    }

                    double altitude_m  = 44330.0 * (1.0 - std::pow(pressure_hPa / p0_hPa, 0.1903));
                    altitudeFt         = altitude_m * 3.28084;
                    return true;
                }
            } catch (const std::exception &e) {
                std::cerr << "CBOR decode error (len=" << payloadLen
                          << "): " << e.what() << "\n";
                // fall through to resync logic below
            }

            // If we got here, we *thought* we had a frame but decode failed.
            // Resync by shifting the window by 1 byte from the *end* of this payload.
            // Put the last 4 bytes of payload into the window and continue scanning.
            if (payloadLen >= 4) {
                std::memcpy(window, &payload[payloadLen - 4], 4);
                continue;
            } else {
                // Very short / weird, just refill the window from the stream
                readExact(fd, window, 4);
                continue;
            }
        }

        // If payloadLen is clearly bogus, slide window by 1 byte and try again
        // (this is where your 0x73 0x73 0x75 0x72 bytes came from before)
        std::cerr << "Suspicious payload length: " << payloadLen
                  << " (bytes: 0x"
                  << std::hex << std::setw(2) << std::setfill('0') << (int)window[0]
                  << " 0x" << std::setw(2) << (int)window[1]
                  << " 0x" << std::setw(2) << (int)window[2]
                  << " 0x" << std::setw(2) << (int)window[3]
                  << std::dec << ")\n";

        // Shift window left by 1
        window[0] = window[1];
        window[1] = window[2];
        window[2] = window[3];
        // Read one new byte into window[3]
        readExact(fd, &window[3], 1);
        // Loop and re-check
    }
}


// ---------- Existing OLED helpers ----------

// Convert a QImage into the SSD1306 buffer format
static std::vector<uint8_t> imageToOledBuffer(const QImage &img) {
    QImage mono = img.convertToFormat(QImage::Format_Grayscale8)
                      .scaled(SSD1306::Width,
                              SSD1306::Height,
                              Qt::IgnoreAspectRatio,
                              Qt::FastTransformation);

    std::vector<uint8_t> buf(SSD1306::BufferSize, 0x00);  // start fully black

    for (int y = 0; y < SSD1306::Height; ++y) {
        for (int x = 0; x < SSD1306::Width; ++x) {
            int gray = qGray(mono.pixel(x, y));
            bool on  = gray > 128;  // threshold

            if (on) {
                int page    = y / 8;
                int bit     = y % 8;
                int byteIdx = page * SSD1306::Width + x;
                buf[byteIdx] |= (1 << bit);
            }
        }
    }

    return buf;
}

// Render a frame with altitude in feet, centered
static QImage renderAltitudeFrame(double altitudeFt, bool haveAltitude) {
    QImage frame(SSD1306::Width, SSD1306::Height, QImage::Format_Grayscale8);
    frame.fill(Qt::black);

    QPainter p(&frame);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::white);

    if (!haveAltitude) {
        // Show "WAITING..." until first valid packet
        QFont font("Monospace");
        font.setStyleHint(QFont::TypeWriter);
        font.setPointSize(12);
        p.setFont(font);

        QString msg = "WAITING...";
        QFontMetrics fm(font);
        int w = fm.horizontalAdvance(msg);
        int h = fm.height();

        int x = (SSD1306::Width  - w) / 2;
        int y = (SSD1306::Height + h) / 2 - fm.descent();

        p.drawText(x, y, msg);
    } else {
        // Big altitude number, e.g. "1234 FT"
        int altInt = static_cast<int>(std::round(altitudeFt));
        QString altStr = QString::number(altInt) + " FT";

        QFont bigFont("Monospace");
        bigFont.setStyleHint(QFont::TypeWriter);
        bigFont.setPointSize(18);
        p.setFont(bigFont);

        QFontMetrics fm(bigFont);
        int w = fm.horizontalAdvance(altStr);
        int h = fm.height();

        int x = (SSD1306::Width  - w) / 2;
        int y = (SSD1306::Height + h) / 2 - fm.descent();

        p.drawText(x, y, altStr);
    }

    p.end();
    return frame;
}

// ---------- main ----------

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    // Open OLED
    SSD1306 oled("/dev/i2c-1", 0x3C);
    if (!oled.isOpen()) {
        std::cerr << "Failed to open I2C for OLED\n";
        return 1;
    }
    if (!oled.init()) {
        std::cerr << "Failed to init SSD1306\n";
        return 1;
    }
    oled.clear();

    // Open serial to ESP32
    int serialFd = -1;
    try {
        serialFd = openSerial(SERIAL_PORT);
        std::cout << "Opened serial " << SERIAL_PORT << " for ESP32\n";
    } catch (const std::exception &e) {
        std::cerr << "Serial error: " << e.what() << "\n";
        return 1;
    }

    double lastAltitudeFt = 0.0;
    bool   haveAltitude   = false;

    std::cout << "Displaying altitude on OLED...\n";

    while (true) {
        try {
            double altFt = lastAltitudeFt;
            if (readAltitudeFromEsp32(serialFd, altFt)) {
                lastAltitudeFt = altFt;
                haveAltitude   = true;
            }

            QImage frame = renderAltitudeFrame(lastAltitudeFt, haveAltitude);
            auto buf     = imageToOledBuffer(frame);
            oled.update(buf);

        } catch (const std::exception &e) {
            std::cerr << "Error reading altitude: " << e.what() << "\n";
            // keep lastAltitudeFt, just redraw it
            QImage frame = renderAltitudeFrame(lastAltitudeFt, haveAltitude);
            auto buf     = imageToOledBuffer(frame);
            oled.update(buf);
        }

        // Limit update rate a bit so we’re not hammering the OLED
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return 0;
}
