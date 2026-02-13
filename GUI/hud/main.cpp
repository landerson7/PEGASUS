#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <QDebug>
#include <QCommandLineParser>
#include <QProcessEnvironment>

#include "HudWidget.h"
#include "DummyDataSource.h"

static bool isDevMode(QApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription("PEGASUS HUD");
    parser.addHelpOption();

    QCommandLineOption devOpt(QStringList() << "d" << "dev",
                              "Developer mode: render on primary display (no HDMI fullscreen).");
    parser.addOption(devOpt);
    parser.process(app);

    if (parser.isSet(devOpt)) return true;

    const auto env = QProcessEnvironment::systemEnvironment();
    const QString v = env.value("HUD_DEV").trimmed();
    return (v == "1" ||
            v.compare("true", Qt::CaseInsensitive) == 0 ||
            v.compare("yes", Qt::CaseInsensitive) == 0);
}

static QScreen* pickExternalScreen(QApplication& app)
{
    const auto screens = app.screens();
    if (screens.isEmpty()) return nullptr;

    // Prefer a non-primary screen if present
    if (screens.size() > 1) return screens[1];

    return app.primaryScreen();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("PEGASUS HUD");

    const bool devMode = isDevMode(app);
    qDebug() << "DEV mode:" << devMode;

    HudWidget hud;
    hud.resize(1280, 720);

    // Show once first so a native window exists (prevents WSL/Wayland segfaults)
    hud.show();

    // Configure dummy data generator
    DummyDataSource source(&app);
    source.baseAltFt = 35000.0;
    source.ampAltFt  = 600.0;  // make altitude motion obvious
    source.periodSec = 5.0;    // faster oscillation

    // Move to correct screen + fullscreen/maximized AFTER the window exists
    QTimer::singleShot(0, [&](){
        const auto screens = app.screens();
        qDebug() << "Detected screens:";
        for (int i = 0; i < screens.size(); ++i) {
            qDebug() << " " << i << screens[i]->name() << screens[i]->geometry();
        }

        if (devMode) {
            // Dev mode: stay on primary screen, not fullscreen
            if (auto* primary = app.primaryScreen()) {
                hud.move(primary->geometry().topLeft());
            }
            hud.showMaximized();
            return;
        }

        // HUD mode: try to move to external screen and fullscreen
        QScreen* target = pickExternalScreen(app);
        if (hud.windowHandle() && target) {
            hud.windowHandle()->setScreen(target);
            hud.move(target->geometry().topLeft()); // ensure it's on-screen
        }
        hud.showFullScreen();
    });

    // Periodic UI updates (dummy motion)
    QTimer tick;
    tick.setTimerType(Qt::PreciseTimer);

    QObject::connect(&tick, &QTimer::timeout, [&](){
        HudSample s = source.read(); // <-- correct: object, not pointer

        hud.setHeadingDeg(s.headingDeg);
        hud.setRollDeg(s.rollDeg);
        hud.setPitchDeg(s.pitchDeg);
        hud.setAltitudeFt(s.altitudeFt);
        hud.setVSpeedFpm(s.vspeedFpm);
    });

    tick.start(16); // ~60 Hz UI refresh
    return app.exec();
}
