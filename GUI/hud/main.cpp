#include <QApplication>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <QDebug>
#include <QCommandLineParser>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include "HudWidget.h"
#include "DummyDataSource.h"
#include "HudLogger.h"
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
    app.setApplicationVersion("dev");

    qDebug() << "APP STARTED FROM:" << QCoreApplication::applicationFilePath();
    qDebug() << "ARGS:" << app.arguments();

    QCommandLineParser parser;
    parser.setApplicationDescription("PEGASUS HUD");
    parser.addHelpOption();

    QCommandLineOption devOpt(QStringList() << "d" << "dev",
                              "Developer mode: render on primary display.");
    QCommandLineOption dummyOpt(QStringList() << "dummy",
                                "Use dummy values (no UART).");
    QCommandLineOption smallDisplayOpt(QStringList() << "small-display",
                                       "Tune text readability for the 3.5in 640x480 hardware display.");
    QCommandLineOption portOpt(QStringList() << "p" << "port",
                               "UART port.",
                               "path", "/dev/serial0");
    QCommandLineOption baudOpt(QStringList() << "b" << "baud",
                               "UART baud rate.",
                               "baud", "115200");

    parser.addOption(devOpt);
    parser.addOption(dummyOpt);
    parser.addOption(smallDisplayOpt);
    parser.addOption(portOpt);
    parser.addOption(baudOpt);

    parser.process(app);

    const bool devMode = parser.isSet(devOpt);
    const bool forceDummy = parser.isSet(dummyOpt);
    const bool smallDisplayMode = parser.isSet(smallDisplayOpt);

    qDebug() << "DEV mode:" << devMode;
    qDebug() << "Dummy flag:" << forceDummy;
    qDebug() << "Small display mode:" << smallDisplayMode;

    const QString appDataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString logDbPath = QDir(appDataDir).filePath("hud_logs.sqlite");
    QString gitBranch = qEnvironmentVariable("HUD_GIT_BRANCH");
    if (gitBranch.isEmpty()) gitBranch = qEnvironmentVariable("GIT_BRANCH");
    if (gitBranch.isEmpty()) gitBranch = "unknown";

    QString gitCommit = qEnvironmentVariable("HUD_GIT_COMMIT");
    if (gitCommit.isEmpty()) gitCommit = qEnvironmentVariable("GIT_COMMIT");
    if (gitCommit.isEmpty()) gitCommit = "unknown";

    HudLogger logger;
    const bool loggerReady = logger.initialize(logDbPath) &&
        logger.beginSession(app.applicationVersion(),
                            gitBranch,
                            gitCommit,
                            QString("dev_mode=%1 force_dummy=%2")
                                .arg(devMode ? "true" : "false")
                                .arg(forceDummy ? "true" : "false"));

    if (loggerReady) {
        logger.logEvent("INFO", "Application", "application_launched",
                        "Application launched",
                        QString("argv=%1").arg(app.arguments().join(' ')));
        logger.logEvent("INFO", "HudLogger", "database_open_success",
                        "Database opened successfully",
                        logger.databasePath());
        logger.logEvent("INFO", "HudLogger", "logger_initialized",
                        "Logger initialized",
                        "SQLite logging is active");
    } else {
        qCritical().noquote() << "HUD logging initialization failed for" << logDbPath;
    }

    HudWidget hud;
    hud.resize(1280, 720);
    if (smallDisplayMode) {
        //hud.setTextScale(1.0);
        //hud.setNumericScale(1.10);
    }

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
            if (smallDisplayMode) {
                hud.resize(640, 480);
                hud.showNormal();
                if (auto* primary = app.primaryScreen()) {
                    const QRect screen = primary->availableGeometry();
                    hud.move(screen.center() - hud.rect().center());
                }
                return;
            }
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
    uart.setLogger(loggerReady ? &logger : nullptr);

    QObject::connect(&hud, &HudWidget::frameRendered,
                     [&](double displayMs, double displayRateHz){
        if (!loggerReady) return;
        logger.recordDisplayMetrics(displayMs, displayRateHz);
    });

    QObject::connect(&uart, &UartCborSource::logLine, [&](const QString& s){
        qDebug().noquote() << s;
    });

    QObject::connect(&uart, &UartCborSource::sampleReady, [&](const HudSample& s){
       /* qDebug() << "UART sampleReady:"
                 << "hdg" << s.headingDeg
                 << "roll" << s.rollDeg
                 << "pitch" << s.pitchDeg
                 << "alt" << s.altitudeFt
                 << "vs" << s.vspeedFpm;*/

        if (loggerReady) {
            logger.updateLatestSample(s);
        }

        hud.setHeadingDeg(s.headingDeg);
        hud.setRollDeg(s.rollDeg);
        hud.setPitchDeg(s.pitchDeg);
        hud.setAltitudeFt(s.altitudeFt);
        hud.setVSpeedFpm(s.vspeedFpm);
        hud.setPressureHpa(s.pressureHpa);
    });

    if (!forceDummy) {
        const QString port = parser.value(portOpt);
        const int baud = parser.value(baudOpt).toInt();

        qDebug() << "Starting UART on port" << port << "baud" << baud;

        if (!uart.start(port, baud)) {
            qDebug() << "UART failed; continuing in dummy mode.";
            if (loggerReady) {
                logger.logEvent("ERROR", "Application", "uart_start_failure",
                                "UART failed; continuing in dummy mode",
                                QString("port=%1 baud=%2").arg(port).arg(baud));
            }
        } else {
            qDebug() << "UART start() succeeded; entering app event loop.";
            if (loggerReady) {
                logger.logEvent("INFO", "Application", "uart_start_success",
                                "UART start succeeded",
                                QString("port=%1 baud=%2").arg(port).arg(baud));
            }

            QObject::connect(&app, &QCoreApplication::aboutToQuit, [&](){
                uart.stop();
                if (!loggerReady) return;
                logger.logEvent("INFO", "Application", "normal_shutdown",
                                "Application shutting down normally");
                logger.endSession("normal_shutdown", "aboutToQuit emitted");
            });

            return app.exec();
        }
    }

    qDebug() << "Running in dummy mode.";

    // Dummy polling @ ~60Hz
    QTimer tick;
    tick.setTimerType(Qt::PreciseTimer);

    QObject::connect(&tick, &QTimer::timeout, [&](){
        HudSample s = dummy.read();
        s.data_valid = true;
        s.data_fresh = true;

        // Uncomment for raw data log
        /*qDebug() << "DUMMY tick:"
                 << "hdg" << s.headingDeg
                 << "roll" << s.rollDeg
                 << "pitch" << s.pitchDeg
                 << "alt" << s.altitudeFt
                 << "vs" << s.vspeedFpm;*/

        if (loggerReady) {
            logger.updateLatestSample(s);
        }

        hud.setHeadingDeg(s.headingDeg);
        hud.setRollDeg(s.rollDeg);
        hud.setPitchDeg(s.pitchDeg);
        hud.setAltitudeFt(s.altitudeFt);
        hud.setVSpeedFpm(s.vspeedFpm);
    });

    tick.start(16);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&](){
        uart.stop();
        if (!loggerReady) return;
        logger.logEvent("INFO", "Application", "normal_shutdown",
                        "Application shutting down normally");
        logger.endSession("normal_shutdown", "aboutToQuit emitted");
    });
    return app.exec();
}
