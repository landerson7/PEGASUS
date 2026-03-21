#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <QDebug>
#include <QCommandLineParser>
#include <QProcessEnvironment>
#include <QCoreApplication>

#include "HudWidget.h"
#include "DummyDataSource.h"
#include "UartCborSource.h"

static QScreen* pickExternalScreen(QApplication& app)
{
    const auto screens = app.screens();
    if (screens.isEmpty()) return nullptr;
    if (screens.size() > 1) return screens[1];
    return app.primaryScreen();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("PEGASUS HUD");

    qDebug() << "APP STARTED FROM:" << QCoreApplication::applicationFilePath();
    qDebug() << "ARGS:" << app.arguments();

    QCommandLineParser parser;
    parser.setApplicationDescription("PEGASUS HUD");
    parser.addHelpOption();

    QCommandLineOption devOpt(QStringList() << "d" << "dev",
                              "Developer mode: render on primary display.");
    QCommandLineOption dummyOpt(QStringList() << "dummy",
                                "Use dummy values (no UART).");
    QCommandLineOption portOpt(QStringList() << "p" << "port",
                               "UART port.",
                               "path", "/dev/serial0");
    QCommandLineOption baudOpt(QStringList() << "b" << "baud",
                               "UART baud rate.",
                               "baud", "115200");

    parser.addOption(devOpt);
    parser.addOption(dummyOpt);
    parser.addOption(portOpt);
    parser.addOption(baudOpt);

    parser.process(app);

    const bool devMode = parser.isSet(devOpt);
    const bool forceDummy = parser.isSet(dummyOpt);

    qDebug() << "DEV mode:" << devMode;
    qDebug() << "Dummy flag:" << forceDummy;

    HudWidget hud;
    hud.resize(1280, 720);

    // Show once first so a native window exists
    hud.show();

    // Test values at startup so you can verify the HUD can repaint
    qDebug() << "Setting test values at startup";
    hud.setHeadingDeg(123);
    hud.setRollDeg(25);
    hud.setPitchDeg(-10);
    hud.setAltitudeFt(12345);
    hud.setVSpeedFpm(800);

    // Screen placement after window exists
    QTimer::singleShot(0, [&](){
        const auto screens = app.screens();
        qDebug() << "Detected screens:";
        for (int i = 0; i < screens.size(); ++i) {
            qDebug() << " " << i << screens[i]->name() << screens[i]->geometry();
        }

        if (devMode) {
            if (auto* primary = app.primaryScreen()) {
                hud.move(primary->geometry().topLeft());
            }
            hud.showMaximized();
            return;
        }

        QScreen* target = pickExternalScreen(app);
        if (hud.windowHandle() && target) {
            hud.windowHandle()->setScreen(target);
            hud.move(target->geometry().topLeft());
        }
        hud.showFullScreen();
    });

    // ---- Data sources ----
    DummyDataSource dummy(&app);
    dummy.baseAltFt = 35000.0;
    dummy.ampAltFt  = 600.0;
    dummy.periodSec = 5.0;

    UartCborSource uart(&app);

    QObject::connect(&uart, &UartCborSource::logLine, [&](const QString& s){
        qDebug().noquote() << s;
    });

    QObject::connect(&uart, &UartCborSource::sampleReady, [&](const HudSample& s){
        qDebug() << "UART sampleReady:"
                 << "hdg" << s.headingDeg
                 << "roll" << s.rollDeg
                 << "pitch" << s.pitchDeg
                 << "alt" << s.altitudeFt
                 << "vs" << s.vspeedFpm;

        hud.setHeadingDeg(s.headingDeg);
        hud.setRollDeg(s.rollDeg);
        hud.setPitchDeg(s.pitchDeg);
        hud.setAltitudeFt(s.altitudeFt);
        hud.setVSpeedFpm(s.vspeedFpm);
    });

    if (!forceDummy) {
        const QString port = parser.value(portOpt);
        const int baud = parser.value(baudOpt).toInt();

        qDebug() << "Starting UART on port" << port << "baud" << baud;

        if (!uart.start(port, baud)) {
            qDebug() << "UART failed; continuing in dummy mode.";
        } else {
            qDebug() << "UART start() succeeded; entering app event loop.";
            return app.exec();
        }
    }

    qDebug() << "Running in dummy mode.";

    // Dummy polling @ ~60Hz
    QTimer tick;
    tick.setTimerType(Qt::PreciseTimer);

    QObject::connect(&tick, &QTimer::timeout, [&](){
        HudSample s = dummy.read();

        qDebug() << "DUMMY tick:"
                 << "hdg" << s.headingDeg
                 << "roll" << s.rollDeg
                 << "pitch" << s.pitchDeg
                 << "alt" << s.altitudeFt
                 << "vs" << s.vspeedFpm;

        hud.setHeadingDeg(s.headingDeg);
        hud.setRollDeg(s.rollDeg);
        hud.setPitchDeg(s.pitchDeg);
        hud.setAltitudeFt(s.altitudeFt);
        hud.setVSpeedFpm(s.vspeedFpm);
    });

    tick.start(16);
    return app.exec();
}