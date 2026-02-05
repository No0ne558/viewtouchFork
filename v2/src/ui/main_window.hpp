/**
 * @file main_window.hpp
 * @brief Main application window
 */

#pragma once

#include "core/types.hpp"
#include "ui/page.hpp"
#include "auth/auth_service.hpp"
#include "data/employee_store.hpp"
#include <QMainWindow>
#include <QStackedWidget>
#include <map>
#include <memory>

namespace vt2 {

class LoginZone;  // Forward declaration

/**
 * @brief Main application window
 * 
 * Contains all pages and manages navigation between them.
 * This is the primary container for the ViewTouch UI.
 * 
 * UI scales to any resolution 1920x1080 and above.
 * Base design resolution is 1920x1080.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    
    // ========================================================================
    // Scaling
    // ========================================================================
    
    static constexpr int BASE_WIDTH = 1920;
    static constexpr int BASE_HEIGHT = 1080;
    static constexpr int MIN_WIDTH = 1920;
    static constexpr int MIN_HEIGHT = 1080;
    
    /**
     * @brief Get current scale factors
     */
    [[nodiscard]] qreal scaleX() const { return scaleX_; }
    [[nodiscard]] qreal scaleY() const { return scaleY_; }
    
    /**
     * @brief Scale a value by X factor
     */
    [[nodiscard]] int sx(int value) const { return static_cast<int>(value * scaleX_); }
    
    /**
     * @brief Scale a value by Y factor
     */
    [[nodiscard]] int sy(int value) const { return static_cast<int>(value * scaleY_); }
    
    // ========================================================================
    // Authentication
    // ========================================================================
    
    /**
     * @brief Get the auth service
     */
    [[nodiscard]] AuthService* authService() { return authService_.get(); }
    
    /**
     * @brief Get the employee store
     */
    [[nodiscard]] EmployeeStore* employeeStore() { return employeeStore_.get(); }
    
    // ========================================================================
    // Page Management
    // ========================================================================
    
    /**
     * @brief Add a page to the window
     */
    void addPage(std::unique_ptr<Page> page);
    
    /**
     * @brief Remove a page
     */
    void removePage(PageId id);
    
    /**
     * @brief Get a page by ID
     */
    Page* page(PageId id);
    
    /**
     * @brief Get current page
     */
    Page* currentPage();
    
    /**
     * @brief Show a specific page
     */
    void showPage(PageId id);
    
    /**
     * @brief Get all page IDs
     */
    std::vector<PageId> pageIds() const;
    
    // ========================================================================
    // Demo/Default Pages
    // ========================================================================
    
    /**
     * @brief Create default demo pages
     */
    void createDemoPages();

signals:
    void pageChanged(PageId newPage);
    void userLoggedIn(const Employee* employee, bool isSuperuser);
    void userLoggedOut();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    
private slots:
    void onLoginAction(const QString& action);
    void onAuthenticationResult(bool success, const QString& message);
    
private:
    void setupUI();
    void setupAuth();
    void applyTheme();
    void updateScaleFactors();
    void rebuildPages();
    void attemptLogin(const QString& action);
    void showPostLoginPage(const QString& action);
    
    QStackedWidget* pageStack_ = nullptr;
    std::map<PageId, Page*> pages_;
    uint32_t nextPageId_ = 1;
    
    qreal scaleX_ = 1.0;
    qreal scaleY_ = 1.0;
    
    // Authentication
    std::unique_ptr<AuthService> authService_;
    std::unique_ptr<EmployeeStore> employeeStore_;
    LoginZone* loginZone_ = nullptr;  // Non-owning pointer
    QString pendingAction_;  // Action selected after PIN entry
};

} // namespace vt2
