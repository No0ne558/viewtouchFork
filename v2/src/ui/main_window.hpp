/**
 * @file main_window.hpp
 * @brief Main application window
 */

#pragma once

#include "core/types.hpp"
#include "ui/page.hpp"
#include <QMainWindow>
#include <QStackedWidget>
#include <map>
#include <memory>

namespace vt2 {

/**
 * @brief Main application window
 * 
 * Contains all pages and manages navigation between them.
 * This is the primary container for the ViewTouch UI.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    
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

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    
private:
    void setupUI();
    void applyTheme();
    
    QStackedWidget* pageStack_ = nullptr;
    std::map<PageId, Page*> pages_;
    uint32_t nextPageId_ = 1;
};

} // namespace vt2
