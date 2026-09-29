#include <catch2/catch_session.hpp>

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
    return Catch::Session().run(argc, argv);
}
