// Plumb — quantity takeoff on construction drawings.

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "cli.h"
#include "exportjob.h"
#include "portalfilepicker.h"
#include "sheetimageprovider.h"
#include "takeoff.h"
#include "takeofflayer.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Commands and help draw no window, so they must not need a display, even
    // where the desktop exports QT_QPA_PLATFORM=wayland. Bare plumb prints
    // help, as a command line tool should; the launcher says plumb open.
    const QByteArray first = argc > 1 ? QByteArray(argv[1]) : QByteArray();
    const bool command = argc == 1 || isCliCommand(QString::fromLocal8Bit(first));
    bool windowless = command;
    for (int i = 1; i < argc; ++i) {
        const QByteArray argument(argv[i]);
        windowless = windowless || argument == "--help" || argument == "-h" || argument == "--version" || argument == "-v";
    }
    if (windowless)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("plumb");
    app.setApplicationVersion("0.1.0");

    // Matches the window to plumb.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("plumb");
    app.setWindowIcon(QIcon::fromTheme("plumb"));

    if (command)
        return runCli(app.arguments());

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Quantity takeoff on construction drawings.")
                                     + "\n\n" + cliSummary());
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("drawing.pdf", QCoreApplication::translate("main", "The drawing to open."));
    // "plumb open a.pdf" and "plumb a.pdf" both open the window.
    QStringList arguments = app.arguments();
    if (arguments.value(1) == "open")
        arguments.removeAt(1);
    parser.process(arguments);

    Theme theme;
    PortalFilePicker picker;
    Backend backend(&picker);

    // The takeoff follows whichever drawing is open.
    Takeoff takeoff;
    QObject::connect(&backend, &Backend::documentChanged, &takeoff,
                     [&] { takeoff.open(backend.filePath(), backend.sheetCount()); });
    QObject::connect(&takeoff, &Takeoff::failed, &backend, &Backend::reportError);
    qmlRegisterType<TakeoffLayer>("Plumb", 1, 0, "TakeoffLayer");
    qmlRegisterUncreatableType<Takeoff>("Plumb", 1, 0, "Takeoff", "The takeoff comes from the app");

    ExportJob exportJob(&backend, &takeoff, &picker);
    QObject::connect(&exportJob, &ExportJob::failed, &backend, &Backend::reportError);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    // The engine owns the provider and deletes it with itself.
    engine.addImageProvider("sheets", new SheetImageProvider(&backend));
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("takeoff", &takeoff);
    engine.rootContext()->setContextProperty("exportJob", &exportJob);

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty())
        backend.load(QUrl::fromUserInput(files.at(0), QDir::currentPath(), QUrl::AssumeLocalFile));

    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
