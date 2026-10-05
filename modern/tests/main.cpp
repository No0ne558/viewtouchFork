#include <catch2/catch_session.hpp>

#include "app/i18n.hh"

#include <QGuiApplication>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(ViewTouchPlugin)

// Qt plugins (SQLite driver) need an application object, and the UI tests
// need a GUI one. Default to the offscreen platform so tests run headless.
int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    vt::i18n::install();   // as the app does: English plurals, Spanish for those who use it
    return Catch::Session().run(argc, argv);
}
