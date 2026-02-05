/*
 * ViewTouch V2 - Application Implementation
 */

#include "app/application.hpp"
#include "app/main_window.hpp"
#include "terminal/control.hpp"
#include "core/colors.hpp"
#include "core/fonts.hpp"
#include "render/textures.hpp"

#include <QDir>
#include <QStandardPaths>

namespace vt {

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
    , control_(std::make_unique<Control>(this))
    , fontManager_(std::make_unique<FontManager>())
    , textures_(std::make_unique<Textures>(this))
{
    setApplicationName(QStringLiteral("ViewTouch"));
    setOrganizationName(QStringLiteral("ViewTouch"));
    setApplicationVersion(QStringLiteral("2.0.0"));
}

Application::~Application() {
    if (mainWindow_) {
        delete mainWindow_;
    }
}

bool Application::initialize() {
    // Set default data path if not specified
    if (dataPath_.isEmpty()) {
        // Look for data in standard locations
        QStringList searchPaths = {
            QDir::currentPath() + QStringLiteral("/data"),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
            QStringLiteral("/usr/local/viewtouch/data"),
            QStringLiteral("/usr/share/viewtouch/data"),
        };
        
        for (const QString& path : searchPaths) {
            if (QDir(path).exists()) {
                dataPath_ = path;
                break;
            }
        }
    }
    
    // Initialize control
    control_->setDataPath(dataPath_);
    if (!control_->initialize()) {
        // Continue anyway - we can work without data files
    }
    
    // Load resources
    loadResources();
    
    return true;
}

bool Application::loadResources() {
    // Initialize fonts
    fontManager_->initialize();
    
    // Load textures
    if (!dataPath_.isEmpty()) {
        textures_->setBasePath(dataPath_ + QStringLiteral("/textures"));
    }
    textures_->loadAll();
    
    return true;
}

ColorPalette* Application::palette() const {
    return &ColorPalette::instance();
}

void Application::setDataPath(const QString& path) {
    dataPath_ = path;
    control_->setDataPath(path);
}

void Application::showMainWindow() {
    if (!mainWindow_) {
        mainWindow_ = new MainWindow(control_.get());
    }
    mainWindow_->show();
}

Application* app() {
    return qobject_cast<Application*>(QApplication::instance());
}

} // namespace vt
