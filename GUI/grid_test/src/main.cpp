#include <QApplication>
#include <QCommandLineParser>
#include <QScreen>
#include <QTimer>

#include "GridWidget.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Grid Test");

    QCommandLineParser parser;
    parser.setApplicationDescription("Green grid projection test");
    parser.addHelpOption();

    QCommandLineOption devOpt(QStringList() << "d" << "dev",
                              "Developer mode (windowed/maximized on primary screen).");
    QCommandLineOption divisionsOpt(QStringList() << "n" << "divisions",
                                    "Grid divisions (squares across). Default 12.",
                                    "int", "12");
    QCommandLineOption noGlowOpt(QStringList() << "no-glow",
                                 "Disable faux glow.");
    QCommandLineOption noDiagOpt(QStringList() << "no-diagonal",
                                 "Disable diagonal line.");
    QCommandLineOption noRetOpt(QStringList() << "no-reticle",
                                 "Disable center reticle.");

    parser.addOption(devOpt);
    parser.addOption(divisionsOpt);
    parser.addOption(noGlowOpt);
    parser.addOption(noDiagOpt);
    parser.addOption(noRetOpt);
    parser.process(app);

    const bool devMode = parser.isSet(devOpt);

    GridWidget w;
    w.setGridDivisions(parser.value(divisionsOpt).toInt());
    w.setGlow(!parser.isSet(noGlowOpt));
    w.setShowDiagonal(!parser.isSet(noDiagOpt));
    w.setShowReticle(!parser.isSet(noRetOpt));

    w.resize(1280, 720);
    w.show();

    // After the window exists, choose display style
    QTimer::singleShot(0, [&](){
        if (devMode) {
            // windowed/maximized on primary screen
            if (auto* s = app.primaryScreen()) w.move(s->geometry().topLeft());
            w.showMaximized();
        } else {
            // fullscreen (use your system's chosen display)
            w.showFullScreen();
        }
    });

    return app.exec();
}