#include <QGuiApplication>
#include <QQuickView>
#include <QImage>
#include <QTimer>
#include <QUrl>

#include "ssd1306.h"

static std::vector<uint8_t> imageToOledBuffer(const QImage &img) {
    QImage mono = img.convertToFormat(QImage::Format_Grayscale8)
                      .scaled(SSD1306::Width,
                              SSD1306::Height,
                              Qt::IgnoreAspectRatio,
                              Qt::FastTransformation);

    std::vector<uint8_t> buf(SSD1306::BufferSize, 0x00);

    for (int page = 0; page < SSD1306::Pages; ++page) {
        for (int x = 0; x < SSD1306::Width; ++x) {
            uint8_t byte = 0;
            for (int bit = 0; bit < 8; ++bit) {
                int y = page * 8 + bit;
                int gray = qGray(mono.pixel(x, y));
                bool on = gray > 128;

                if (on) byte |= (1 << bit);
            }
            buf[page * SSD1306::Width + x] = byte;
        }
    }

    return buf;
}


int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    // Init OLED
    SSD1306 oled("/dev/i2c-1", 0x3C);
    if (!oled.init()) {
        qCritical("Failed to init SSD1306 OLED over I2C");
        return 1;
    }

    // Qt Quick view at the OLED resolution
    QQuickView view;
    view.setSource(QUrl(QStringLiteral("qrc:/main.qml")));
    view.setColor(Qt::black);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(SSD1306::Width, SSD1306::Height);

    // Important: ensure the window is actually created.
    // This can be minimized or off to the side; it's mostly for debugging.
    view.show();

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        QImage frame = view.grabWindow();
        if (frame.isNull()) return;

        auto buf = imageToOledBuffer(frame);
        oled.update(buf);
    });

    timer.start(50); // ~20 FPS



    return app.exec();
}
