/**
 * @file application.cpp
 * @brief Application implementation
 */

#include "core/application.hpp"
#include "ui/main_window.hpp"
#include <QScreen>
#include <QStyleFactory>
#include <QDir>
#include <filesystem>

namespace vt2 {

Application& Application::instance() {
    static Application instance;
    return instance;
}

Application::~Application() = default;

Result<void> Application::init(int argc, char* argv[]) {
    if (initialized_) {
        return std::unexpected("Application already initialized");
    }
    
    // Initialize Qt application
    qtApp_ = std::make_unique<QApplication>(argc, argv);
    qtApp_->setApplicationName("ViewTouch");
    qtApp_->setApplicationVersion("2.0.0");
    qtApp_->setOrganizationName("ViewTouch");
    
    // Parse command line arguments
    if (auto result = parseArgs(argc, argv); !result) {
        return result;
    }
    
    // Initialize logging early (with defaults, will be reconfigured after config loads)
    Logger::instance().init(".", spdlog::level::info);
    
    // Initialize configuration
    if (auto result = initConfig(); !result) {
        return result;
    }
    
    // Reconfigure logging with config values
    Logger::instance().setLevel(
        config().debugMode() ? spdlog::level::debug : spdlog::level::info
    );
    
    VT_INFO("ViewTouch V2 starting...");
    VT_INFO("Qt version: {}", qVersion());
    
    // Initialize UI
    if (auto result = initUI(); !result) {
        return result;
    }
    
    // Load pages
    if (auto result = loadPages(); !result) {
        return result;
    }
    
    initialized_ = true;
    VT_INFO("Application initialized successfully");
    
    return {};
}

int Application::run() {
    if (!initialized_) {
        VT_ERROR("Application not initialized");
        return 1;
    }
    
    VT_INFO("Starting main event loop");
    
    // Show main window
    if (config().fullscreen()) {
        mainWindow_->showFullScreen();
    } else {
        mainWindow_->show();
    }
    
    // Navigate to initial page (index/login)
    navigateTo(PageId{1});
    
    // Run Qt event loop
    int result = qtApp_->exec();
    
    VT_INFO("Application exiting with code: {}", result);
    return result;
}

void Application::shutdown() {
    VT_INFO("Application shutting down...");
    
    emit aboutToQuit();
    
    // Save configuration
    if (auto result = config().save(); !result) {
        VT_ERROR("Failed to save configuration: {}", result.error());
    }
    
    mainWindow_.reset();
    qtApp_.reset();
    
    VT_INFO("Shutdown complete");
}

Result<void> Application::parseArgs(int argc, char* argv[]) {
    // Basic argument parsing
    // TODO: Use a proper argument parser like CLI11
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--debug" || arg == "-d") {
            // Will be handled after config load
        } else if (arg == "--help" || arg == "-h") {
            // Print help and exit
            qInfo() << "ViewTouch V2 - Modern POS System";
            qInfo() << "Usage: viewtouch2 [options]";
            qInfo() << "  --debug, -d     Enable debug mode";
            qInfo() << "  --config PATH   Specify config file";
            qInfo() << "  --help, -h      Show this help";
            return std::unexpected("Help requested");
        }
    }
    
    return {};
}

Result<void> Application::initConfig() {
    namespace fs = std::filesystem;
    
    // Try to load config from standard locations
    std::vector<fs::path> configPaths = {
        "./viewtouch.toml",
        "~/.config/viewtouch2/viewtouch.toml",
        "/etc/viewtouch2/viewtouch.toml",
        "/usr/viewtouch2/viewtouch.toml"
    };
    
    for (const auto& path : configPaths) {
        if (fs::exists(path)) {
            if (auto result = config().load(path); result) {
                return {};
            }
        }
    }
    
    // No config found, create defaults
    VT_INFO("No configuration file found, creating defaults");
    config().createDefaults();
    
    // Try to save to user config directory
    auto userConfigDir = fs::path(QDir::homePath().toStdString()) / ".config" / "viewtouch2";
    fs::create_directories(userConfigDir);
    
    auto userConfigPath = userConfigDir / "viewtouch.toml";
    if (auto result = config().save(userConfigPath); !result) {
        VT_WARN("Could not save default config: {}", result.error());
    }
    
    return {};
}

Result<void> Application::initUI() {
    VT_DEBUG("Initializing UI...");
    
    // Set application style
    qtApp_->setStyle(QStyleFactory::create("Fusion"));
    
    // Create main window
    mainWindow_ = std::make_unique<MainWindow>();
    
    // Configure window size
    int width = config().screenWidth();
    int height = config().screenHeight();
    
    // Apply scale factor for high DPI
    double scale = config().scaleFactor();
    if (scale != 1.0) {
        width = static_cast<int>(width * scale);
        height = static_cast<int>(height * scale);
    }
    
    mainWindow_->resize(width, height);
    
    // Center on screen
    if (auto* screen = QGuiApplication::primaryScreen()) {
        auto screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - width) / 2;
        int y = (screenGeometry.height() - height) / 2;
        mainWindow_->move(x, y);
    }
    
    VT_DEBUG("UI initialized: {}x{}", width, height);
    
    return {};
}

Result<void> Application::loadPages() {
    VT_DEBUG("Loading page definitions...");
    
    // TODO: Load page definitions from data files
    // For now, we'll create default pages programmatically in MainWindow
    
    return {};
}

void Application::navigateTo(PageId pageId) {
    if (pageId == currentPageId_) {
        return;
    }
    
    PageId oldPage = currentPageId_;
    
    // Save to history for back navigation
    if (currentPageId_.value != 0) {
        pageHistory_.push_back(currentPageId_);
        // Limit history size
        if (pageHistory_.size() > 50) {
            pageHistory_.erase(pageHistory_.begin());
        }
    }
    
    currentPageId_ = pageId;
    
    VT_DEBUG("Navigating to page {}", pageId.value);
    
    // Update main window
    if (mainWindow_) {
        mainWindow_->showPage(pageId);
    }
    
    emit pageChanged(pageId, oldPage);
}

void Application::navigateTo(const QString& pageName) {
    // TODO: Look up page ID by name
    VT_DEBUG("Navigating to page: {}", pageName.toStdString());
}

void Application::goBack() {
    if (pageHistory_.empty()) {
        VT_DEBUG("No page history to go back to");
        return;
    }
    
    PageId previousPage = pageHistory_.back();
    pageHistory_.pop_back();
    
    PageId oldPage = currentPageId_;
    currentPageId_ = previousPage;
    
    VT_DEBUG("Going back to page {}", previousPage.value);
    
    if (mainWindow_) {
        mainWindow_->showPage(previousPage);
    }
    
    emit pageChanged(previousPage, oldPage);
}

void Application::setCurrentEmployee(EmployeeId id) {
    currentEmployee_ = id;
    VT_INFO("Employee {} logged in", id.value);
    emit employeeChanged(currentEmployee_);
}

void Application::logout() {
    if (currentEmployee_) {
        VT_INFO("Employee {} logged out", currentEmployee_->value);
    }
    currentEmployee_ = std::nullopt;
    emit employeeChanged(std::nullopt);
}

bool Application::hasPermission(Permission perm) const {
    if (!currentEmployee_) {
        return false;
    }
    
    // TODO: Look up actual permissions for employee
    // For now, allow everything if logged in
    return true;
}

} // namespace vt2
