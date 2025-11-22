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

// Render a HUD-style frame with altitude tape + crosshair
static QImage renderAltitudeFrame(double altitudeFt, bool haveAltitude) {
    const int W = SSD1306::Width;   // 128
    const int H = SSD1306::Height;  // 64

    QImage frame(W, H, QImage::Format_Grayscale8);
    frame.fill(Qt::black);

    QPainter p(&frame);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::white);

    // =========================
    // 1) Crosshair in the middle
    // =========================
    int cx = W / 2;
    int cy = H / 2;
    int crossHalf = 14;      // half-length of crosshair arms
    int gap = 3;             // small gap at the center

    // Horizontal line (left)
    p.drawLine(cx - crossHalf, cy, cx - gap, cy);
    // Horizontal line (right)
    p.drawLine(cx + gap, cy, cx + crossHalf, cy);
    // Vertical line (top)
    p.drawLine(cx, cy - crossHalf, cx, cy - gap);
    // Vertical line (bottom)
    p.drawLine(cx, cy + gap, cx, cy + crossHalf);

    // Optional small box at the center
    p.drawRect(cx - 2, cy - 2, 4, 4);

    // =========================================
    // 2) Altitude "tape" / box on the right side
    // =========================================
    int boxW = 44;
    int boxH = 48;
    int boxX = W - boxW - 4;       // small margin from right edge
    int boxY = (H - boxH) / 2;

    // Outer box
    p.drawRect(boxX, boxY, boxW, boxH);

    // Small label "ALT" at top of box
    {
        QFont labelFont("Monospace");
        labelFont.setStyleHint(QFont::TypeWriter);
        labelFont.setPointSize(7);
        p.setFont(labelFont);

        QString label = "ALT";
        QFontMetrics fm(labelFont);
        int lw = fm.horizontalAdvance(label);
        int lx = boxX + (boxW - lw) / 2;
        int ly = boxY - 2;   // slightly above box
        p.drawText(lx, ly, label);
    }

    // ==========================
    // 3) Altitude numeric display
    // ==========================
    if (!haveAltitude) {
        // Show "WAIT" in the box until we get real data
        QFont waitFont("Monospace");
        waitFont.setStyleHint(QFont::TypeWriter);
        waitFont.setPointSize(10);
        p.setFont(waitFont);

        QString msg = "WAIT";
        QFontMetrics fm(waitFont);
        int tw = fm.horizontalAdvance(msg);
        int th = fm.height();

        int tx = boxX + (boxW - tw) / 2;
        int ty = boxY + (boxH + th) / 2 - fm.descent();
        p.drawText(tx, ty, msg);
    } else {
        // Big altitude number (integer feet) in the center of the box
        int altInt = static_cast<int>(std::round(altitudeFt));
        QString altStr = QString::number(altInt);

        QFont bigFont("Monospace");
        bigFont.setStyleHint(QFont::TypeWriter);
        bigFont.setPointSize(14);   // adjust if it clips; 12–14 works well
        p.setFont(bigFont);

        QFontMetrics fm(bigFont);
        int tw = fm.horizontalAdvance(altStr);
        int th = fm.height();

        int tx = boxX + (boxW - tw) / 2;
        int ty = boxY + (boxH + th) / 2 - fm.descent();
        p.drawText(tx, ty, altStr);

        // Small "FT" label at bottom of box
        QFont unitFont("Monospace");
        unitFont.setStyleHint(QFont::TypeWriter);
        unitFont.setPointSize(7);
        p.setFont(unitFont);

        QString unit = "FT";
        QFontMetrics fm2(unitFont);
        int uw = fm2.horizontalAdvance(unit);
        int ux = boxX + (boxW - uw) / 2;
        int uy = boxY + boxH + fm2.ascent();   // just under box
        p.drawText(ux, uy, unit);
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
