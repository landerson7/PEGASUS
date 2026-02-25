#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <QDebug>
#include <QCommandLineParser>
#include <QProcessEnvironment>

#include "HudWidget.h"
#include "DummyDataSource.h"
#include "UartCborSource.h"

static bool isDevMode(QApplication& app, QCommandLineParser& parser)
{
    QCommandLineOption devOpt(QStringList() << "d" << "dev",
                              "Developer mode: render on primary display (no HDMI fullscreen).");
    parser.addOption(devOpt);

    // Also allow env HUD_DEV=1
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString v = env.value("HUD_DEV").trimmed();
    const bool envDev = (v == "1" ||
                         v.compare("true", Qt::CaseInsensitive) == 0 ||
                         v.compare("yes", Qt::CaseInsensitive) == 0);

    return parser.isSet(devOpt) || envDev;
}

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
    qDebug() << "DEV mode:" << devMode;

    HudWidget hud;
    hud.resize(1280, 720);

    // Show once first so a native window exists (prevents WSL/Wayland segfaults)
    hud.show();

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
        hud.setHeadingDeg(s.headingDeg);
        hud.setRollDeg(s.rollDeg);
        hud.setPitchDeg(s.pitchDeg);
        hud.setAltitudeFt(s.altitudeFt);
        hud.setVSpeedFpm(s.vspeedFpm);
    });

    const bool forceDummy = parser.isSet(dummyOpt);
    if (!forceDummy) {
        const QString port = parser.value(portOpt);
        const int baud = parser.value(baudOpt).toInt();
        if (!uart.start(port, baud)) {
            qDebug() << "UART failed; continuing in dummy mode.";
        } else {
            // UART drives HUD via signal; no polling needed.
            return app.exec();
        }
    }

    // Dummy polling @ ~60Hz
    QTimer tick;
    tick.setTimerType(Qt::PreciseTimer);

    QObject::connect(&tick, &QTimer::timeout, [&](){
        HudSample s = dummy.read();
        hud.setHeadingDeg(s.headingDeg);
        hud.setRollDeg(s.rollDeg);
        hud.setPitchDeg(s.pitchDeg);
        hud.setAltitudeFt(s.altitudeFt);
        hud.setVSpeedFpm(s.vspeedFpm);
    });

    tick.start(16);
    return app.exec();
}