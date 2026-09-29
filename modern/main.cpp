#include "layoutcontroller.hh"
#include "storage/layout_store.hh"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(ViewTouchPlugin)

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"ViewTouch"_s);
    QGuiApplication::setOrganizationName(u"ViewTouch"_s);
    QQuickStyle::setStyle(u"Fusion"_s);   // editor chrome; POS pages draw themselves

    QCommandLineParser cli;
    cli.setApplicationDescription(u"ViewTouch point of sale"_s);
    cli.addHelpOption();
    const QString defaultDb = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                  .filePath(u"viewtouch.db"_s);
    const QCommandLineOption dbOpt(u"db"_s, u"SQLite database (default: %1)."_s.arg(defaultDb), u"file"_s, defaultDb);
    const QCommandLineOption layoutOpt(u"layout"_s,
        u"Show pages from <dir> instead of the database. Saving in the editor still writes to the database."_s,
        u"dir"_s);
    const QCommandLineOption resetOpt(u"reset-layout"_s, u"Replace the saved pages with the built-in starter pages."_s);
    const QCommandLineOption pageOpt(u"page"_s, u"Open page <id> at startup."_s, u"id"_s);
    const QCommandLineOption editOpt(u"edit"_s, u"Start in edit mode."_s);
    const QCommandLineOption selectOpt(u"select"_s, u"In edit mode, select these zones (comma separated)."_s, u"ids"_s);
    const QCommandLineOption sizeOpt(u"size"_s, u"Window size, e.g. 1280x720."_s, u"WxH"_s, u"1280x720"_s);
    const QCommandLineOption shotOpt(u"screenshot"_s, u"Render, save a PNG to <file>, and exit."_s, u"file"_s);
    cli.addOptions({dbOpt, layoutOpt, resetOpt, pageOpt, editOpt, selectOpt, sizeOpt, shotOpt});
    cli.process(app);

    vt::storage::LayoutStore store(cli.value(dbOpt));
    QString dbError;
    const bool haveStore = store.open(&dbError);
    if (!haveStore)
        qWarning().noquote() << "Pages will not be saved; cannot open database" << store.path() << ":" << dbError;

    // Source of pages: --layout dir, else the database, else the built-in seed.
    QStringList errors;
    std::optional<vt::layout::Layout> layout;
    if (cli.isSet(layoutOpt)) {
        layout = vt::layout::Layout::loadDirectory(cli.value(layoutOpt), &errors);
    } else if (haveStore && store.hasLayout() && !cli.isSet(resetOpt)) {
        layout = store.load(&errors);
    }
    if (!layout && !cli.isSet(layoutOpt)) {
        layout = vt::layout::Layout::loadDirectory(u":/seed"_s, &errors);
        if (layout && haveStore) {
            QString error;
            if (!store.save(*layout, &error))
                qWarning().noquote() << "Could not store starter pages:" << error;
        }
    }
    for (const QString &e : std::as_const(errors))
        qWarning().noquote() << "layout:" << e;
    if (!layout) {
        qCritical().noquote() << "Could not load any pages.";
        return 1;
    }
    for (const QString &issue : layout->validate())
        qWarning().noquote() << "layout issue:" << issue;

    LayoutController controller(std::move(*layout));
    if (haveStore)
        controller.setStore(&store);
    if (cli.isSet(pageOpt) && !controller.showPage(cli.value(pageOpt))) {
        qCritical().noquote() << "No page with id" << cli.value(pageOpt);
        return 1;
    }
    if (cli.isSet(editOpt)) {
        controller.enterEditMode();
        if (cli.isSet(selectOpt))
            controller.editor()->selectOnly(cli.value(selectOpt).split(u','));
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
        QTimer::singleShot(1000, window, [window, file] {
            const bool ok = window->grabWindow().save(file);
            if (!ok)
                qCritical().noquote() << "Could not write" << file;
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    return app.exec();
}
