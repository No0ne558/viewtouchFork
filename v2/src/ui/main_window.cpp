/**
 * @file main_window.cpp
 * @brief Main window implementation
 */

#include "ui/main_window.hpp"
#include "zones/button_zone.hpp"
#include "zones/login_zone.hpp"
#include "core/application.hpp"
#include "core/logger.hpp"
#include <QKeyEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>

namespace vt2 {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUI();
    applyTheme();
    updateScaleFactors();
    createDemoPages();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUI() {
    setWindowTitle("ViewTouch V2");
    
    // Set minimum size to 1920x1080
    setMinimumSize(MIN_WIDTH, MIN_HEIGHT);
    
    // Get primary screen and set initial size
    if (auto* screen = QGuiApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        
        // If screen is large enough, start at screen size
        // Otherwise start at minimum size
        int startWidth = qMax(screenGeometry.width(), MIN_WIDTH);
        int startHeight = qMax(screenGeometry.height(), MIN_HEIGHT);
        
        resize(startWidth, startHeight);
        
        VT_INFO("Screen: {}x{}, Window: {}x{}", 
                screenGeometry.width(), screenGeometry.height(),
                startWidth, startHeight);
    } else {
        resize(MIN_WIDTH, MIN_HEIGHT);
    }
    
    // Create central widget with page stack
    pageStack_ = new QStackedWidget(this);
    setCentralWidget(pageStack_);
    
    // Remove window decorations for POS kiosk mode (can be toggled)
    // setWindowFlags(Qt::FramelessWindowHint);
}

void MainWindow::updateScaleFactors() {
    scaleX_ = static_cast<qreal>(width()) / BASE_WIDTH;
    scaleY_ = static_cast<qreal>(height()) / BASE_HEIGHT;
    VT_DEBUG("Scale factors: X={:.3f}, Y={:.3f}", scaleX_, scaleY_);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    
    qreal oldScaleX = scaleX_;
    qreal oldScaleY = scaleY_;
    
    updateScaleFactors();
    
    // Only rebuild if scale changed significantly
    if (qAbs(scaleX_ - oldScaleX) > 0.01 || qAbs(scaleY_ - oldScaleY) > 0.01) {
        rebuildPages();
    }
}

void MainWindow::rebuildPages() {
    VT_INFO("Rebuilding pages for new resolution: {}x{}", width(), height());
    
    // Remember current page
    PageId currentId{0};
    if (auto* current = currentPage()) {
        currentId = current->id();
    }
    
    // Clear all pages
    while (pageStack_->count() > 0) {
        QWidget* w = pageStack_->widget(0);
        pageStack_->removeWidget(w);
        delete w;
    }
    pages_.clear();
    nextPageId_ = 1;
    
    // Recreate pages with new scale
    createDemoPages();
    
    // Restore current page
    if (currentId.value != 0 && page(currentId)) {
        showPage(currentId);
    }
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
    VT_INFO("Creating application pages at scale {:.2f}x{:.2f}...", scaleX_, scaleY_);
    
    // Helper lambdas for scaling - all coordinates based on 1920x1080 base
    auto X = [this](int v) { return sx(v); };
    auto Y = [this](int v) { return sy(v); };
    
    // ========================================================================
    // Page 1: Login Page (PIN + Actions on same page)
    // ========================================================================
    auto loginPage = std::make_unique<Page>(PageType::Login);
    loginPage->setId(PageId{1});
    loginPage->setPageName("Login");
    loginPage->setBackgroundColor(QColor(25, 25, 35));
    
    // Company title at top
    auto companyTitle = std::make_unique<ButtonZone>();
    companyTitle->setText("ViewTouch POS");
    companyTitle->setBackgroundColor(QColor(25, 25, 35));
    companyTitle->setForegroundColor(colors::White);
    companyTitle->setFontSize(FontSize::Huge);
    companyTitle->setBorderWidth(0);
    loginPage->addZone(std::move(companyTitle), X(20), Y(15), X(1880), Y(80));
    
    // Left side: PIN Entry
    // PIN display field label
    auto pinLabel = std::make_unique<ButtonZone>();
    pinLabel->setText("Enter PIN:");
    pinLabel->setBackgroundColor(QColor(25, 25, 35));
    pinLabel->setForegroundColor(colors::LightGray);
    pinLabel->setFontSize(FontSize::Large);
    pinLabel->setBorderWidth(0);
    pinLabel->setAlignment(HAlign::Center, VAlign::Bottom);
    loginPage->addZone(std::move(pinLabel), X(60), Y(100), X(550), Y(60));
    
    // Login zone with PIN keypad (left side)
    auto loginZone = std::make_unique<LoginZone>();
    loginZone->setBackgroundColor(QColor(35, 35, 45));
    loginZone->setBorderWidth(0);
    LoginZone* loginZonePtr = loginZone.get();
    loginPage->addZone(std::move(loginZone), X(60), Y(160), X(550), Y(800));
    
    // Right side: Action buttons
    int rightX = 680;
    int btnWidth = 560;
    int btnHeight = 180;
    int spacing = 25;
    int startY = 120;
    
    // Sign In/Out button
    auto signInOutBtn = std::make_unique<ButtonZone>();
    signInOutBtn->setText("Sign In / Out");
    signInOutBtn->setBackgroundColor(colors::VTBlue);
    signInOutBtn->setFontSize(FontSize::Huge);
    signInOutBtn->setAction([loginZonePtr]() {
        VT_INFO("Sign In/Out pressed - validating PIN");
        emit loginZonePtr->pinEntered("SIGNOUT");
    });
    loginPage->addZone(std::move(signInOutBtn), X(rightX), Y(startY), X(btnWidth), Y(btnHeight));
    
    // Second column
    int rightX2 = rightX + btnWidth + spacing;
    
    // Tables button
    auto tablesBtn = std::make_unique<ButtonZone>();
    tablesBtn->setText("Tables");
    tablesBtn->setBackgroundColor(colors::Teal);
    tablesBtn->setFontSize(FontSize::Huge);
    tablesBtn->setAction([loginZonePtr]() {
        VT_INFO("Tables pressed - validating PIN");
        emit loginZonePtr->pinEntered("TABLES");
    });
    loginPage->addZone(std::move(tablesBtn), X(rightX2), Y(startY), X(btnWidth), Y(btnHeight));
    
    // Row 2
    int row2Y = startY + btnHeight + spacing;
    
    // Takeout button
    auto takeoutBtn = std::make_unique<ButtonZone>();
    takeoutBtn->setText("Takeout");
    takeoutBtn->setBackgroundColor(colors::Orange);
    takeoutBtn->setFontSize(FontSize::Huge);
    takeoutBtn->setAction([loginZonePtr]() {
        VT_INFO("Takeout pressed - validating PIN");
        emit loginZonePtr->pinEntered("TAKEOUT");
    });
    loginPage->addZone(std::move(takeoutBtn), X(rightX), Y(row2Y), X(btnWidth), Y(btnHeight));
    
    // Quick Dine In button
    auto quickDineInBtn = std::make_unique<ButtonZone>();
    quickDineInBtn->setText("Quick Dine In");
    quickDineInBtn->setBackgroundColor(colors::VTGreen);
    quickDineInBtn->setFontSize(FontSize::Huge);
    quickDineInBtn->setAction([loginZonePtr]() {
        VT_INFO("Quick Dine In pressed - validating PIN");
        emit loginZonePtr->pinEntered("QUICKDINE");
    });
    loginPage->addZone(std::move(quickDineInBtn), X(rightX2), Y(row2Y), X(btnWidth), Y(btnHeight));
    
    // Row 3: Additional options (smaller)
    int row3Y = row2Y + btnHeight + spacing + 30;
    int smallBtnHeight = 130;
    
    auto reportsBtn = std::make_unique<ButtonZone>();
    reportsBtn->setText("Reports");
    reportsBtn->setBackgroundColor(colors::Purple);
    reportsBtn->setFontSize(FontSize::XLarge);
    reportsBtn->setAction([loginZonePtr]() {
        emit loginZonePtr->pinEntered("REPORTS");
    });
    loginPage->addZone(std::move(reportsBtn), X(rightX), Y(row3Y), X(btnWidth), Y(smallBtnHeight));
    
    auto checksBtn = std::make_unique<ButtonZone>();
    checksBtn->setText("Open Checks");
    checksBtn->setBackgroundColor(colors::VTYellow);
    checksBtn->setFontSize(FontSize::XLarge);
    checksBtn->setAction([loginZonePtr]() {
        emit loginZonePtr->pinEntered("CHECKS");
    });
    loginPage->addZone(std::move(checksBtn), X(rightX2), Y(row3Y), X(btnWidth), Y(smallBtnHeight));
    
    // Row 4
    int row4Y = row3Y + smallBtnHeight + spacing;
    
    auto settingsBtn = std::make_unique<ButtonZone>();
    settingsBtn->setText("Settings");
    settingsBtn->setBackgroundColor(colors::Gray);
    settingsBtn->setFontSize(FontSize::XLarge);
    settingsBtn->setAction([loginZonePtr]() {
        emit loginZonePtr->pinEntered("SETTINGS");
    });
    loginPage->addZone(std::move(settingsBtn), X(rightX), Y(row4Y), X(btnWidth), Y(smallBtnHeight));
    
    auto managerBtn = std::make_unique<ButtonZone>();
    managerBtn->setText("Manager");
    managerBtn->setBackgroundColor(colors::VTRed);
    managerBtn->setFontSize(FontSize::XLarge);
    managerBtn->setAction([loginZonePtr]() {
        emit loginZonePtr->pinEntered("MANAGER");
    });
    loginPage->addZone(std::move(managerBtn), X(rightX2), Y(row4Y), X(btnWidth), Y(smallBtnHeight));
    
    // Connect login zone signal to handle actions
    connect(loginZonePtr, &LoginZone::pinEntered, this, [loginZonePtr](const QString& action) {
        // Get the actual PIN that was entered
        QString pin = loginZonePtr->property("enteredPin").toString();
        
        if (pin.length() < 4 && action != "SIGNOUT") {
            loginZonePtr->setErrorMessage("Enter your PIN first");
            return;
        }
        
        VT_INFO("Action: {} with PIN ({} digits)", action.toStdString(), pin.length());
        
        // TODO: Real employee lookup by PIN
        // For now, accept any 4+ digit PIN
        
        loginZonePtr->clearPin();
        
        if (action == "SIGNOUT") {
            VT_INFO("Sign out - staying on login page");
            // Just clear and stay
        } else if (action == "TABLES") {
            app().navigateTo(PageId{2});  // Tables page
        } else if (action == "TAKEOUT" || action == "QUICKDINE") {
            app().navigateTo(PageId{3});  // Order page
        } else if (action == "REPORTS") {
            // TODO: Reports page
            VT_INFO("Reports not yet implemented");
        } else if (action == "CHECKS") {
            // TODO: Open checks page
            VT_INFO("Open Checks not yet implemented");
        } else if (action == "SETTINGS") {
            // TODO: Settings page
            VT_INFO("Settings not yet implemented");
        } else if (action == "MANAGER") {
            // TODO: Manager page
            VT_INFO("Manager not yet implemented");
        }
    });
    
    // Footer with version
    auto loginFooter = std::make_unique<ButtonZone>();
    loginFooter->setText("ViewTouch V2.0 | © 2026");
    loginFooter->setBackgroundColor(QColor(20, 20, 30));
    loginFooter->setForegroundColor(QColor(100, 100, 100));
    loginFooter->setFontSize(FontSize::Normal);
    loginFooter->setBorderWidth(0);
    loginPage->addZone(std::move(loginFooter), 0, Y(1020), X(1920), Y(60));
    
    addPage(std::move(loginPage));
    
    // ========================================================================
    // Page 2: Tables Page
    // ========================================================================
    auto tablesPage = std::make_unique<Page>(PageType::Table);
    tablesPage->setId(PageId{2});
    tablesPage->setPageName("Table Selection");
    tablesPage->setBackgroundColor(colors::VTBackground);
    
    // Header
    auto tablesHeader = std::make_unique<ButtonZone>();
    tablesHeader->setText("Select a Table");
    tablesHeader->setBackgroundColor(colors::Teal);
    tablesHeader->setFontSize(FontSize::XLarge);
    tablesHeader->setBorderWidth(0);
    tablesPage->addZone(std::move(tablesHeader), X(20), Y(15), X(1700), Y(70));
    
    // Back button
    auto tablesBackBtn = std::make_unique<ButtonZone>();
    tablesBackBtn->setText("← Back");
    tablesBackBtn->setBackgroundColor(colors::DarkGray);
    tablesBackBtn->setFontSize(FontSize::Large);
    tablesBackBtn->setAction([]() {
        app().navigateTo(PageId{1});  // Back to login
    });
    tablesPage->addZone(std::move(tablesBackBtn), X(1740), Y(15), X(160), Y(70));
    
    // Table grid - 5 columns, 4 rows
    int tableStartX = 20;
    int tableStartY = 110;
    int tableWidth = 370;
    int tableHeight = 220;
    int tableSpacingX = 10;
    int tableSpacingY = 15;
    
    for (int i = 0; i < 20; ++i) {
        auto tableBtn = std::make_unique<ButtonZone>();
        tableBtn->setText(QString("Table\n%1").arg(i + 1));
        
        // Vary colors to show status (green=available, yellow=occupied, red=dirty)
        QColor color = colors::VTGreen;  // Available
        if (i % 5 == 0) color = colors::VTYellow;  // Occupied
        if (i % 7 == 0) color = colors::VTRed;     // Dirty
        
        tableBtn->setBackgroundColor(color);
        tableBtn->setFontSize(FontSize::XLarge);
        
        // Click to go to order entry for that table
        int tableNum = i + 1;
        tableBtn->setAction([tableNum]() {
            VT_INFO("Table {} selected", tableNum);
            app().navigateTo(PageId{3});  // Go to order page
        });
        
        int col = i % 5;
        int row = i / 5;
        int posX = tableStartX + col * (tableWidth + tableSpacingX);
        int posY = tableStartY + row * (tableHeight + tableSpacingY);
        tablesPage->addZone(std::move(tableBtn), X(posX), Y(posY), X(tableWidth), Y(tableHeight));
    }
    
    addPage(std::move(tablesPage));
    
    // ========================================================================
    // Page 3: Order Entry Page
    // ========================================================================
    auto orderPage = std::make_unique<Page>(PageType::Order);
    orderPage->setId(PageId{3});
    orderPage->setPageName("Order Entry");
    orderPage->setBackgroundColor(colors::VTBackground);
    
    // Header
    auto orderHeader = std::make_unique<ButtonZone>();
    orderHeader->setText("Order Entry");
    orderHeader->setBackgroundColor(colors::VTBlue);
    orderHeader->setFontSize(FontSize::XLarge);
    orderHeader->setBorderWidth(0);
    orderPage->addZone(std::move(orderHeader), X(20), Y(15), X(1700), Y(70));
    
    // Back button
    auto backBtn = std::make_unique<ButtonZone>();
    backBtn->setText("← Done");
    backBtn->setBackgroundColor(colors::DarkGray);
    backBtn->setFontSize(FontSize::Large);
    backBtn->setAction([]() {
        app().navigateTo(PageId{1});  // Back to login
    });
    orderPage->addZone(std::move(backBtn), X(1740), Y(15), X(160), Y(70));
    
    // Order list area (left side - takes about 40% of width)
    auto orderList = std::make_unique<ButtonZone>();
    orderList->setText("Order Items\n\n(Order display area)");
    orderList->setBackgroundColor(QColor(40, 40, 40));
    orderList->setAlignment(HAlign::Left, VAlign::Top);
    orderList->setFontSize(FontSize::Large);
    orderPage->addZone(std::move(orderList), X(20), Y(100), X(700), Y(850));
    
    // Menu category buttons (middle column)
    QStringList categories = {"Appetizers", "Entrees", "Sides", "Drinks", "Desserts"};
    int catStartX = 740;
    int catWidth = 280;
    int catHeight = 100;
    int catSpacing = 15;
    
    for (int i = 0; i < categories.size(); ++i) {
        auto catBtn = std::make_unique<ButtonZone>();
        catBtn->setText(categories[i]);
        catBtn->setBackgroundColor(colors::VTGreen);
        catBtn->setFontSize(FontSize::Large);
        orderPage->addZone(std::move(catBtn), X(catStartX), Y(100 + i * (catHeight + catSpacing)), 
                          X(catWidth), Y(catHeight));
    }
    
    // Menu items grid (right side - 4x4 grid)
    QStringList items = {"Burger", "Pizza", "Salad", "Soup", 
                         "Steak", "Fish", "Pasta", "Tacos",
                         "Wings", "Fries", "Soda", "Beer",
                         "Wine", "Coffee", "Dessert", "Special"};
    int itemStartX = 1040;
    int itemWidth = 210;
    int itemHeight = 120;
    int itemSpacing = 10;
    
    for (int i = 0; i < items.size(); ++i) {
        auto itemBtn = std::make_unique<ButtonZone>();
        itemBtn->setText(items[i]);
        itemBtn->setBackgroundColor(colors::VTBlue);
        itemBtn->setFontSize(FontSize::Large);
        
        int col = i % 4;
        int row = i / 4;
        int posX = itemStartX + col * (itemWidth + itemSpacing);
        int posY = 100 + row * (itemHeight + itemSpacing);
        orderPage->addZone(std::move(itemBtn), X(posX), Y(posY), X(itemWidth), Y(itemHeight));
    }
    
    // Action buttons at bottom
    int actionY = 970;
    int actionWidth = 220;
    int actionHeight = 90;
    int actionSpacing = 20;
    
    auto sendBtn = std::make_unique<ButtonZone>();
    sendBtn->setText("SEND");
    sendBtn->setBackgroundColor(colors::VTGreen);
    sendBtn->setFontSize(FontSize::XLarge);
    orderPage->addZone(std::move(sendBtn), X(20), Y(actionY), X(actionWidth), Y(actionHeight));
    
    auto payBtn = std::make_unique<ButtonZone>();
    payBtn->setText("PAY");
    payBtn->setBackgroundColor(colors::VTYellow);
    payBtn->setFontSize(FontSize::XLarge);
    orderPage->addZone(std::move(payBtn), X(20 + actionWidth + actionSpacing), Y(actionY), 
                      X(actionWidth), Y(actionHeight));
    
    auto voidBtn = std::make_unique<ButtonZone>();
    voidBtn->setText("VOID");
    voidBtn->setBackgroundColor(colors::VTRed);
    voidBtn->setFontSize(FontSize::XLarge);
    orderPage->addZone(std::move(voidBtn), X(20 + 2*(actionWidth + actionSpacing)), Y(actionY), 
                      X(actionWidth), Y(actionHeight));
    
    addPage(std::move(orderPage));
    
    VT_INFO("Application pages created: {} pages total", pages_.size());
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
