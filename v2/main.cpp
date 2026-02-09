#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Set application properties
    app.setApplicationName("ViewTouch V2");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("ViewTouch");
    app.setOrganizationDomain("viewtouch.com");

    QQmlApplicationEngine engine;

    // Set context properties for QML
    engine.rootContext()->setContextProperty("screenWidth", 1920);
    engine.rootContext()->setContextProperty("screenHeight", 1080);

    const QUrl url(QStringLiteral("qrc:/ViewTouch/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl)
            QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);
    engine.load(url);

    // Force fullscreen on primary screen
    if (!engine.rootObjects().isEmpty()) {
        QQuickWindow *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (window) {
            window->showFullScreen();
        }
    }

    return app.exec();
}