/*
 * ViewTouch V2 - Main Entry Point
 * 
 * ViewTouch POS System - Qt6 Implementation
 * A faithful reimplementation of the original ViewTouch architecture
 */

#include "app/application.hpp"

#include <QDebug>
#include <QCommandLineParser>

int main(int argc, char* argv[])
{
    vt::Application app(argc, argv);
    
    // Parse command line
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("ViewTouch Point of Sale System"));
    parser.addHelpOption();
    parser.addVersionOption();
    
    QCommandLineOption dataPathOption(
        QStringList() << QStringLiteral("d") << QStringLiteral("data"),
        QStringLiteral("Path to data directory"),
        QStringLiteral("path"));
    parser.addOption(dataPathOption);
    
    QCommandLineOption fullscreenOption(
        QStringList() << QStringLiteral("f") << QStringLiteral("fullscreen"),
        QStringLiteral("Start in fullscreen mode"));
    parser.addOption(fullscreenOption);
    
    parser.process(app);
    
    // Set data path if provided
    if (parser.isSet(dataPathOption)) {
        app.setDataPath(parser.value(dataPathOption));
    }
    
    // Initialize
    if (!app.initialize()) {
        qCritical() << "Failed to initialize application";
        return 1;
    }
    
    // Show main window
    app.showMainWindow();
    
    qDebug() << "ViewTouch V2 started";
    
    return app.exec();
}
