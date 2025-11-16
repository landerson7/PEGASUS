#include "ssd1306.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QDateTime>
#include <QTimeZone>

#include <vector>
#include <chrono>
#include <thread>
#include <iostream>

// Convert a QImage into the SSD1306 buffer format (same logic as before)
static std::vector<uint8_t> imageToOledBuffer(const QImage &img) {
    QImage mono = img.convertToFormat(QImage::Format_Grayscale8)
                      .scaled(SSD1306::Width,
                              SSD1306::Height,
                              Qt::IgnoreAspectRatio,
                              Qt::FastTransformation);

    std::vector<uint8_t> buf(SSD1306::BufferSize, 0x00);

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

// Render "HH:MM:SS" for EST into a QImage of OLED size
static QImage renderClockFrame() {
    QImage frame(SSD1306::Width, SSD1306::Height, QImage::Format_Grayscale8);
    frame.fill(Qt::black);

    QPainter p(&frame);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::white);

    // Time in America/New_York (EST/EDT)
    QTimeZone nyTz("America/New_York");
    QDateTime now = QDateTime::currentDateTime().toTimeZone(nyTz);
    QString timeStr = now.toString("HH:mm:ss");

    // Optional: show timezone too (small)
    QString tzStr = "EST";  // you could switch to EDT dynamically if you want

    // Choose font sizes that fit 128x64
    QFont timeFont("Monospace");
    timeFont.setStyleHint(QFont::TypeWriter);
    timeFont.setPointSize(18);  // adjust if it clips
    p.setFont(timeFont);

    QFontMetrics fmTime(timeFont);
    int timeWidth = fmTime.horizontalAdvance(timeStr);
    int timeHeight = fmTime.height();

    int timeX = (SSD1306::Width  - timeWidth)  / 2;
    int timeY = (SSD1306::Height + timeHeight) / 2 - fmTime.descent();

    p.drawText(timeX, timeY, timeStr);

    // Small timezone label at the top-left
    QFont tzFont("Monospace");
    tzFont.setStyleHint(QFont::TypeWriter);
    tzFont.setPointSize(8);
    p.setFont(tzFont);
    p.drawText(2, 10, tzStr);

    p.end();
    return frame;
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    // Init OLED
    SSD1306 oled("/dev/i2c-1", 0x3C);

    if (!oled.isOpen()) {
        std::cerr << "Failed to open I2C\n";
        return 1;
    }
    if (!oled.init()) {
        std::cerr << "Failed to init SSD1306\n";
        return 1;
    }

    std::cout << "Showing EST clock on OLED...\n";

    // Main loop: update once per second
    while (true) {
        QImage frame = renderClockFrame();
        auto buf = imageToOledBuffer(frame);
        oled.update(buf);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0; // never reached
}
