/**
 * @file application.hpp
 * @brief Main application class for ViewTouch V2
 */

#pragma once

#include "core/types.hpp"
#include "core/config.hpp"
#include "core/logger.hpp"
#include <QApplication>
#include <QObject>
#include <memory>
#include <vector>

namespace vt2 {

// Forward declarations
class MainWindow;
class Page;

/**
 * @brief Main ViewTouch application controller
 * 
 * This class manages the application lifecycle, including:
 * - Initialization and shutdown
 * - Configuration management
 * - Page navigation
 * - Global state management
 */
class Application : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Get the singleton application instance
     */
    static Application& instance();
    
    /**
     * @brief Initialize the application
     * @param argc Command line argument count
     * @param argv Command line arguments
     * @return Success or error message
     */
    Result<void> init(int argc, char* argv[]);
    
    /**
     * @brief Run the application main loop
     * @return Exit code
     */
    int run();
    
    /**
     * @brief Shutdown the application
     */
    void shutdown();
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    /**
     * @brief Get the Qt application instance
     */
    QApplication* qtApp() { return qtApp_.get(); }
    
    /**
     * @brief Get the main window
     */
    MainWindow* mainWindow() { return mainWindow_.get(); }
    
    /**
     * @brief Get configuration
     */
    Config& config() { return Config::instance(); }
    
    /**
     * @brief Get logger
     */
    Logger& logger() { return Logger::instance(); }
    
    // ========================================================================
    // Page Management
    // ========================================================================
    
    /**
     * @brief Navigate to a page by ID
     */
    void navigateTo(PageId pageId);
    
    /**
     * @brief Navigate to a page by name
     */
    void navigateTo(const QString& pageName);
    
    /**
     * @brief Go back to the previous page
     */
    void goBack();
    
    /**
     * @brief Get current page ID
     */
    PageId currentPageId() const { return currentPageId_; }
    
    // ========================================================================
    // State Management  
    // ========================================================================
    
    /**
     * @brief Get currently logged in employee
     */
    std::optional<EmployeeId> currentEmployee() const { return currentEmployee_; }
    
    /**
     * @brief Set current employee (login)
     */
    void setCurrentEmployee(EmployeeId id);
    
    /**
     * @brief Clear current employee (logout)
     */
    void logout();
    
    /**
     * @brief Check if user has permission
     */
    bool hasPermission(Permission perm) const;

signals:
    /**
     * @brief Emitted when page changes
     */
    void pageChanged(PageId newPage, PageId oldPage);
    
    /**
     * @brief Emitted when employee logs in/out
     */
    void employeeChanged(std::optional<EmployeeId> employee);
    
    /**
     * @brief Emitted on application shutdown
     */
    void aboutToQuit();

private:
    Application() = default;
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    
    /**
     * @brief Parse command line arguments
     */
    Result<void> parseArgs(int argc, char* argv[]);
    
    /**
     * @brief Load or create configuration
     */
    Result<void> initConfig();
    
    /**
     * @brief Initialize the UI
     */
    Result<void> initUI();
    
    /**
     * @brief Load page definitions
     */
    Result<void> loadPages();
    
    std::unique_ptr<QApplication> qtApp_;
    std::unique_ptr<MainWindow> mainWindow_;
    
    PageId currentPageId_{0};
    std::vector<PageId> pageHistory_;
    std::optional<EmployeeId> currentEmployee_;
    
    bool initialized_ = false;
};

/**
 * @brief Global application access helper
 */
inline Application& app() {
    return Application::instance();
}

} // namespace vt2
