/**
 * @file main_window.cpp
 * @brief Main window implementation
 */

#include "ui/main_window.hpp"
#include "zones/button_zone.hpp"
#include "core/application.hpp"
#include "core/logger.hpp"
#include <QKeyEvent>
#include <QCloseEvent>
#include <QVBoxLayout>

namespace vt2 {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUI();
    applyTheme();
    createDemoPages();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUI() {
    setWindowTitle("ViewTouch V2");
    
    // Create central widget with page stack
    pageStack_ = new QStackedWidget(this);
    setCentralWidget(pageStack_);
    
    // Remove window decorations for POS kiosk mode (can be toggled)
    // setWindowFlags(Qt::FramelessWindowHint);
}

void MainWindow::applyTheme() {
    // Apply dark theme
    setStyleSheet(R"(
        QMainWindow {
            background-color: #2d2d2d;
        }
        QWidget {
            font-family: 'Liberation Sans', 'DejaVu Sans', sans-serif;
            font-size: 14px;
        }
    )");
}

void MainWindow::addPage(std::unique_ptr<Page> page) {
    if (!page) return;
    
    if (page->id().value == 0) {
        page->setId(PageId{nextPageId_++});
    }
    
    PageId id = page->id();
    Page* pagePtr = page.get();
    
    pageStack_->addWidget(page.release());
    pages_[id] = pagePtr;
    
    VT_DEBUG("Page added: {} (id={})", pagePtr->pageName().toStdString(), id.value);
}

void MainWindow::removePage(PageId id) {
    auto it = pages_.find(id);
    if (it != pages_.end()) {
        pageStack_->removeWidget(it->second);
        delete it->second;
        pages_.erase(it);
        VT_DEBUG("Page removed: id={}", id.value);
    }
}

Page* MainWindow::page(PageId id) {
    auto it = pages_.find(id);
    return it != pages_.end() ? it->second : nullptr;
}

Page* MainWindow::currentPage() {
    return qobject_cast<Page*>(pageStack_->currentWidget());
}

void MainWindow::showPage(PageId id) {
    if (auto* p = page(id)) {
        // Notify current page it's leaving
        if (auto* current = currentPage()) {
            current->onExit();
        }
        
        pageStack_->setCurrentWidget(p);
        p->onEnter();
        
        emit pageChanged(id);
        VT_DEBUG("Showing page: {} (id={})", p->pageName().toStdString(), id.value);
    } else {
        VT_WARN("Page not found: id={}", id.value);
    }
}

std::vector<PageId> MainWindow::pageIds() const {
    std::vector<PageId> ids;
    ids.reserve(pages_.size());
    for (const auto& [id, _] : pages_) {
        ids.push_back(id);
    }
    return ids;
}

void MainWindow::createDemoPages() {
    VT_INFO("Creating demo pages...");
    
    // ========================================================================
    // Page 1: Main Index Page
    // ========================================================================
    auto indexPage = std::make_unique<Page>(PageType::Index);
    indexPage->setId(PageId{1});
    indexPage->setPageName("Main Menu");
    indexPage->setBackgroundColor(colors::VTBackground);
    
    // Title
    auto title = std::make_unique<ButtonZone>();
    title->setText("ViewTouch V2");
    title->setBackgroundColor(QColor(51, 51, 51));
    title->setForegroundColor(colors::White);
    title->setFontSize(FontSize::Huge);
    title->setBorderWidth(0);
    indexPage->addZone(std::move(title), 10, 10, 1004, 60);
    
    // Main menu buttons
    QStringList menuItems = {
        "Dine In", "Take Out", "Delivery", "Bar",
        "Tables", "Checks", "Reports", "Settings"
    };
    
    QList<QColor> buttonColors = {
        colors::VTBlue, colors::VTGreen, colors::Orange, colors::Purple,
        colors::Teal, colors::VTYellow, colors::Gray, colors::DarkGray
    };
    
    int startX = 10;
    int startY = 90;
    int btnWidth = 245;
    int btnHeight = 160;
    int spacing = 10;
    
    for (int i = 0; i < menuItems.size(); ++i) {
        auto btn = std::make_unique<ButtonZone>();
        btn->setText(menuItems[i]);
        btn->setBackgroundColor(buttonColors[i % buttonColors.size()]);
        btn->setFontSize(FontSize::XLarge);
        
        int col = i % 4;
        int row = i / 4;
        int x = startX + col * (btnWidth + spacing);
        int y = startY + row * (btnHeight + spacing);
        
        // Add navigation action for certain buttons
        if (menuItems[i] == "Dine In") {
            btn->setAction([]() {
                app().navigateTo(PageId{2});  // Go to order page
            });
        } else if (menuItems[i] == "Tables") {
            btn->setAction([]() {
                app().navigateTo(PageId{3});  // Go to tables page
            });
        }
        
        indexPage->addZone(std::move(btn), x, y, btnWidth, btnHeight);
    }
    
    // Status bar at bottom
    auto statusBar = std::make_unique<ButtonZone>();
    statusBar->setText("Ready | No Employee Signed In | " + 
                       QDateTime::currentDateTime().toString("hh:mm AP"));
    statusBar->setBackgroundColor(QColor(30, 30, 30));
    statusBar->setForegroundColor(colors::LightGray);
    statusBar->setFontSize(FontSize::Normal);
    statusBar->setAlignment(HAlign::Left, VAlign::Center);
    statusBar->setBorderWidth(0);
    indexPage->addZone(std::move(statusBar), 0, 718, 1024, 50);
    
    addPage(std::move(indexPage));
    
    // ========================================================================
    // Page 2: Order Entry Page
    // ========================================================================
    auto orderPage = std::make_unique<Page>(PageType::Order);
    orderPage->setId(PageId{2});
    orderPage->setPageName("Order Entry");
    orderPage->setBackgroundColor(colors::VTBackground);
    
    // Header
    auto orderHeader = std::make_unique<ButtonZone>();
    orderHeader->setText("Order Entry - Table 1");
    orderHeader->setBackgroundColor(colors::VTBlue);
    orderHeader->setFontSize(FontSize::Large);
    orderHeader->setBorderWidth(0);
    orderPage->addZone(std::move(orderHeader), 10, 10, 700, 50);
    
    // Back button
    auto backBtn = std::make_unique<ButtonZone>();
    backBtn->setText("← Back");
    backBtn->setBackgroundColor(colors::DarkGray);
    backBtn->setAction([]() {
        app().goBack();
    });
    orderPage->addZone(std::move(backBtn), 720, 10, 100, 50);
    
    // Order list area (placeholder)
    auto orderList = std::make_unique<ButtonZone>();
    orderList->setText("Order Items\n\n(Order display area)");
    orderList->setBackgroundColor(QColor(40, 40, 40));
    orderList->setAlignment(HAlign::Left, VAlign::Top);
    orderPage->addZone(std::move(orderList), 10, 70, 500, 500);
    
    // Menu category buttons
    QStringList categories = {"Appetizers", "Entrees", "Sides", "Drinks", "Desserts"};
    for (int i = 0; i < categories.size(); ++i) {
        auto catBtn = std::make_unique<ButtonZone>();
        catBtn->setText(categories[i]);
        catBtn->setBackgroundColor(colors::VTGreen);
        orderPage->addZone(std::move(catBtn), 520, 70 + i * 55, 200, 50);
    }
    
    // Menu items grid (sample)
    QStringList items = {"Burger", "Pizza", "Salad", "Soup", "Steak", "Fish", 
                         "Pasta", "Tacos", "Wings"};
    for (int i = 0; i < items.size(); ++i) {
        auto itemBtn = std::make_unique<ButtonZone>();
        itemBtn->setText(items[i]);
        itemBtn->setBackgroundColor(colors::VTBlue);
        
        int col = i % 3;
        int row = i / 3;
        orderPage->addZone(std::move(itemBtn), 
                          730 + col * 100, 70 + row * 55, 95, 50);
    }
    
    // Action buttons at bottom
    auto sendBtn = std::make_unique<ButtonZone>();
    sendBtn->setText("SEND");
    sendBtn->setBackgroundColor(colors::VTGreen);
    sendBtn->setFontSize(FontSize::Large);
    orderPage->addZone(std::move(sendBtn), 10, 580, 150, 60);
    
    auto payBtn = std::make_unique<ButtonZone>();
    payBtn->setText("PAY");
    payBtn->setBackgroundColor(colors::VTYellow);
    payBtn->setFontSize(FontSize::Large);
    orderPage->addZone(std::move(payBtn), 170, 580, 150, 60);
    
    auto voidBtn = std::make_unique<ButtonZone>();
    voidBtn->setText("VOID");
    voidBtn->setBackgroundColor(colors::VTRed);
    voidBtn->setFontSize(FontSize::Large);
    orderPage->addZone(std::move(voidBtn), 330, 580, 150, 60);
    
    addPage(std::move(orderPage));
    
    // ========================================================================
    // Page 3: Tables Page
    // ========================================================================
    auto tablesPage = std::make_unique<Page>(PageType::Table);
    tablesPage->setId(PageId{3});
    tablesPage->setPageName("Table Selection");
    tablesPage->setBackgroundColor(colors::VTBackground);
    
    // Header
    auto tablesHeader = std::make_unique<ButtonZone>();
    tablesHeader->setText("Select a Table");
    tablesHeader->setBackgroundColor(colors::Teal);
    tablesHeader->setFontSize(FontSize::Large);
    tablesHeader->setBorderWidth(0);
    tablesPage->addZone(std::move(tablesHeader), 10, 10, 900, 50);
    
    // Back button
    auto tablesBackBtn = std::make_unique<ButtonZone>();
    tablesBackBtn->setText("← Back");
    tablesBackBtn->setBackgroundColor(colors::DarkGray);
    tablesBackBtn->setAction([]() {
        app().goBack();
    });
    tablesPage->addZone(std::move(tablesBackBtn), 920, 10, 94, 50);
    
    // Table grid
    for (int i = 0; i < 20; ++i) {
        auto tableBtn = std::make_unique<ButtonZone>();
        tableBtn->setText(QString("Table\n%1").arg(i + 1));
        
        // Vary colors to show status (green=available, yellow=occupied, red=dirty)
        QColor color = colors::VTGreen;  // Available
        if (i % 5 == 0) color = colors::VTYellow;  // Occupied
        if (i % 7 == 0) color = colors::VTRed;     // Dirty
        
        tableBtn->setBackgroundColor(color);
        tableBtn->setFontSize(FontSize::Medium);
        
        int col = i % 5;
        int row = i / 5;
        tablesPage->addZone(std::move(tableBtn), 
                           10 + col * 205, 80 + row * 160, 195, 150);
    }
    
    addPage(std::move(tablesPage));
    
    VT_INFO("Demo pages created: {} pages total", pages_.size());
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Handle global keyboard shortcuts
    switch (event->key()) {
        case Qt::Key_Escape:
            // In a real POS, might require manager override to exit
            if (event->modifiers() & Qt::ControlModifier) {
                close();
            } else {
                app().goBack();
            }
            break;
            
        case Qt::Key_F11:
            // Toggle fullscreen
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
            break;
            
        default:
            QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    VT_INFO("Main window closing");
    event->accept();
}

} // namespace vt2
