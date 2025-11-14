// src/main.cpp (on Pi)
#include <QGuiApplication>
#include <QQuickView>
#include <QTimer>
#include <QImage>
#include <QPainter>
#include "ssd1306.h"

static std::vector<uint8_t> imageToOledBuffer(const QImage &img) {
    QImage mono = img.convertToFormat(QImage::Format_Grayscale8);
    std::vector<uint8_t> buf(SSD1306::BufferSize, 0x00);

    for (int y = 0; y < SSD1306::Height; ++y) {
        for (int x = 0; x < SSD1306::Width; ++x) {
            int gray = qGray(mono.pixel(x, y));
            bool on = gray > 128; // threshold
            if (on) {
                int byteIndex = x + (y / 8) * SSD1306::Width;
                int bit = y % 8;
                buf[byteIndex] |= (1 << bit);
            }
        }
    }
    return buf;
}

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    SSD1306 oled("/dev/i2c-1", 0x3C);
    if (!oled.init()) {
        return 1;
    }

    QQuickView view;
    view.setSource(QUrl(QStringLiteral("qrc:/main.qml")));
    view.setColor(Qt::black);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(SSD1306::Width, SSD1306::Height);
    // We don't show the window; we render offscreen
    // view.show();  // not needed for the OLED path

    QImage frame(SSD1306::Width, SSD1306::Height, QImage::Format_ARGB32);

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        frame.fill(Qt::black);
        QPainter p(&frame);
        view.render(&p);
        p.end();

        auto buf = imageToOledBuffer(frame);
        oled.update(buf);
    });
    timer.start(50); // 20 FPS

    return app.exec();
}
