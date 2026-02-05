/*
 * ViewTouch V2 - Application Class
 * Main application setup and management
 */

#pragma once

#include <QApplication>
#include <memory>

namespace vt {

class Control;
class ColorPalette;
class FontManager;
class Textures;

/*************************************************************
 * Application - Main application class
 *************************************************************/
class Application : public QApplication {
    Q_OBJECT
    
public:
    Application(int& argc, char** argv);
    ~Application();
    
    // Initialization
    bool initialize();
    
    // Access control
    Control* control() const { return control_.get(); }
    
    // Access resources
    ColorPalette* palette() const;
    FontManager* fontManager() const { return fontManager_.get(); }
    Textures* textures() const { return textures_.get(); }
    
    // Data paths
    void setDataPath(const QString& path);
    QString dataPath() const { return dataPath_; }
    
    // Show the main window
    void showMainWindow();
    
private:
    bool loadResources();
    
    std::unique_ptr<Control> control_;
    std::unique_ptr<FontManager> fontManager_;
    std::unique_ptr<Textures> textures_;
    
    QString dataPath_;
    
    class MainWindow* mainWindow_ = nullptr;
};

// Global application instance
Application* app();

} // namespace vt
