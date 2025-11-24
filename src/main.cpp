#include "ssd1306.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QFont>

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
#include <iostream>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <arpa/inet.h>
//comment
// CBOR / JSON
#include <nlohmann/json.hpp>

using json = nlohmann::json;
// ---------- Serial config ----------

static const char* SERIAL_PORT = "/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0";   // change to "/dev/ttyACM0" if needed
static const int   SERIAL_BAUD = B115200;

// -----------------------------------------------------------
// Serial helpers
// -----------------------------------------------------------
int openSerial(const char* dev) {
    int fd = ::open(dev, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        std::perror("open");
        throw std::runtime_error("serial open failed");
    }

    termios cfg{};
    tcgetattr(fd, &cfg);

    cfsetospeed(&cfg, SERIAL_BAUD);
    cfsetispeed(&cfg, SERIAL_BAUD);

    cfg.c_cflag = (cfg.c_cflag & ~CSIZE) | CS8;
    cfg.c_iflag &= ~IGNBRK;
    cfg.c_lflag = 0;
    cfg.c_oflag = 0;
    cfg.c_cc[VMIN] = 1;
    cfg.c_cc[VTIME] = 5;

    cfg.c_iflag &= ~(IXON | IXOFF | IXANY);
    cfg.c_cflag |= (CLOCAL | CREAD);
    cfg.c_cflag &= ~(PARENB | PARODD);
    cfg.c_cflag &= ~CSTOPB;
    cfg.c_cflag &= ~CRTSCTS;

    tcsetattr(fd, TCSANOW, &cfg);

    tcflush(fd, TCIFLUSH);

    return fd;
}

void readExact(int fd, uint8_t* buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = ::read(fd, buf + off, n - off);
        if (r <= 0) throw std::runtime_error("serial read fail");
        off += r;
    }
}

// -----------------------------------------------------------
// Read one CBOR altitude frame
// -----------------------------------------------------------
bool readAltitudeFrame(int fd, double& outAltFt) {
    // 1) sync header
    uint8_t b;
    bool gotAA = false;
    while (true) {
        readExact(fd, &b, 1);
        if (!gotAA) {
            if (b == 0xAA) gotAA = true;
        } else {
            if (b == 0x55) break;
            gotAA = (b == 0xAA);
        }
    }

    // 2) length
    uint8_t lenBuf[4];
    readExact(fd, lenBuf, 4);

    uint32_t lenNet;
    memcpy(&lenNet, lenBuf, 4);
    uint32_t payloadLen = ntohl(lenNet);

    if (payloadLen == 0 || payloadLen > 512) {
        return false;
    }

    // 3) payload
    std::vector<uint8_t> payload(payloadLen);
    readExact(fd, payload.data(), payloadLen);

    try {
        json j = json::from_cbor(payload);

        if (!j.contains("pressure")) return false;

        double p = j["pressure"].get<double>();

        static bool   baselineSet = false;
        static double p0_hPa      = 1016.0;

        if (!baselineSet) {
            p0_hPa = pressure_hPa;
            baselineSet = true;
            std::cout << "Baseline pressure set to " << p0_hPa << " hPa\n";
        }

        double alt_m = 44330.0 * (1.0 - pow(p / p0, 0.1903));
        outAltFt = alt_m * 3.28084;
        return true;
    }
    catch (...) {
        return false;
    }
}

// -----------------------------------------------------------
// Serial thread
// -----------------------------------------------------------
void serialThread() {
    int fd = openSerial(SERIAL_PORT);
    std::cout << "Serial thread running\n";

    while (true) {
        try {
            double alt;
            if (readAltitudeFrame(fd, alt)) {
                std::lock_guard<std::mutex> lk(altMutex);
                g_lastAltitudeFt = alt;
                g_haveAltitude = true;
            }
        } catch (...) {
            std::cerr << "Serial error, continuing…\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

// -----------------------------------------------------------
// OLED helpers
// -----------------------------------------------------------

static QImage renderHUD(double altitudeFt, bool have) {
    const int W = SSD1306::Width;
    const int H = SSD1306::Height;

    QImage img(W, H, QImage::Format_Grayscale8);
    img.fill(Qt::black);

    QPainter p(&img);
    p.setPen(Qt::white);

    int cx = W/2;
    int cy = H/2;

    // Crosshair
    p.drawLine(cx-12, cy, cx-2, cy);
    p.drawLine(cx+2, cy, cx+12, cy);
    p.drawLine(cx, cy-12, cx, cy-2);
    p.drawLine(cx, cy+2, cx, cy+12);

    // Altitude box
    int bx = W - 48;
    int by = 8;
    int bw = 40;
    int bh = 48;
    p.drawRect(bx, by, bw, bh);

    if (!have) {
        QFont f("Monospace");
        f.setPointSize(10);
        p.setFont(f);
        p.drawText(bx+8, by+28, "WAIT");
    } else {
        int altInt = (int)std::round(altitudeFt);
        QFont f("Monospace");
        f.setPointSize(14);
        p.setFont(f);

        QString s = QString::number(altInt);
        p.drawText(bx+6, by+30, s);

        QFont small("Monospace");
        small.setPointSize(7);
        p.setFont(small);
        p.drawText(bx+14, by+46, "FT");
    }

    return img;
}

static std::vector<uint8_t> toBuffer(const QImage& img) {
    QImage mono = img.convertToFormat(QImage::Format_Grayscale8);

    std::vector<uint8_t> buf(SSD1306::BufferSize, 0);
    for (int y=0; y<64; y++) {
        for (int x=0; x<128; x++) {
            bool on = (qGray(mono.pixel(x,y)) > 128);
            if (on) {
                int page = y/8;
                int bit = y%8;
                buf[page*128 + x] |= (1<<bit);
            }
        }
    }
    return buf;
}

// -----------------------------------------------------------
// MAIN
// -----------------------------------------------------------
int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    // Start serial thread
    std::thread t(serialThread);
    t.detach();

    // Init OLED
    SSD1306 oled("/dev/i2c-1", 0x3C);
    if (!oled.isOpen() || !oled.init()) {
        std::cerr << "OLED init failed\n";
        return 1;
    }
    oled.clear();

    std::cout << "HUD running…\n";

    // Render loop: 12 FPS
    while (true) {
        double alt;
        bool have;
        {
            std::lock_guard<std::mutex> lk(altMutex);
            alt = g_lastAltitudeFt;
            have = g_haveAltitude;
        }

        QImage frame = renderHUD(alt, have);
        auto buf = toBuffer(frame);

        oled.update(buf);

        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }

    return 0;
}
