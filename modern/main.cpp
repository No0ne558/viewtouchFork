#include "layoutcontroller.hh"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"ViewTouch"_s);
    QGuiApplication::setOrganizationName(u"ViewTouch"_s);

    QCommandLineParser cli;
    cli.setApplicationDescription(u"ViewTouch point of sale"_s);
    cli.addHelpOption();
    const QCommandLineOption layoutOpt(u"layout"_s, u"Load pages from <dir> instead of the built-in seed."_s, u"dir"_s, u":/seed"_s);
    const QCommandLineOption pageOpt(u"page"_s, u"Open page <id> at startup."_s, u"id"_s);
    const QCommandLineOption sizeOpt(u"size"_s, u"Window size, e.g. 1280x720."_s, u"WxH"_s, u"1280x720"_s);
    const QCommandLineOption shotOpt(u"screenshot"_s, u"Render, save a PNG to <file>, and exit."_s, u"file"_s);
    cli.addOptions({layoutOpt, pageOpt, sizeOpt, shotOpt});
    cli.process(app);

    QStringList errors;
    auto layout = vt::layout::Layout::loadDirectory(cli.value(layoutOpt), &errors);
    for (const QString &e : std::as_const(errors))
        qWarning().noquote() << "layout:" << e;
    if (!layout) {
        qCritical().noquote() << "Could not load layout from" << cli.value(layoutOpt);
        return 1;
    }
    for (const QString &issue : layout->validate())
        qWarning().noquote() << "layout issue:" << issue;

    LayoutController controller(std::move(*layout));
    if (cli.isSet(pageOpt) && !controller.jumpTo(cli.value(pageOpt))) {
        qCritical().noquote() << "No page with id" << cli.value(pageOpt);
        return 1;
    }

    const QStringList size = cli.value(sizeOpt).split(u'x');
    const int width = size.value(0).toInt();
    const int height = size.value(1).toInt();

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.setInitialProperties({
        {u"controller"_s, QVariant::fromValue(&controller)},
        {u"width"_s, width > 0 ? width : 1280},
        {u"height"_s, height > 0 ? height : 720},
    });
    engine.loadFromModule("ViewTouch", "Main");

    if (cli.isSet(shotOpt)) {
        auto *window = engine.rootObjects().isEmpty()
            ? nullptr : qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window)
            return 1;
        const QString file = cli.value(shotOpt);
        QTimer::singleShot(800, window, [window, file] {
            const bool ok = window->grabWindow().save(file);
            if (!ok)
                qCritical().noquote() << "Could not write" << file;
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    return app.exec();
}
