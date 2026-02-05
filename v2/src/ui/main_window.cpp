/**
 * @file main_window.cpp
 * @brief Main window implementation
 */

#include "ui/main_window.hpp"
#include "zones/button_zone.hpp"
#include "zones/login_zone.hpp"
#include "zones/settings_zone.hpp"
#include "core/application.hpp"
#include "core/logger.hpp"
#include <QKeyEvent>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QMessageBox>

namespace vt2 {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUI();
    setupAuth();
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

void MainWindow::setupAuth() {
    // Initialize employee store with demo data
    employeeStore_ = std::make_unique<EmployeeStore>(this);
    employeeStore_->loadDemoData();
    
    // Initialize auth service
    authService_ = std::make_unique<AuthService>(this);
    
    // Connect auth service to employee store for lookups
    authService_->setEmployeeLookup([this](const QString& pin) {
        return employeeStore_->findByPin(pin);
    });
    
    // Connect auth service signals
    connect(authService_.get(), &AuthService::userLoggedIn, 
            this, [this](const Employee* emp, bool isSuperuser) {
        VT_INFO("User logged in: {} (superuser: {})", 
                emp->fullName().toStdString(), isSuperuser);
        emit userLoggedIn(emp, isSuperuser);
    });
    
    connect(authService_.get(), &AuthService::userLoggedOut, 
            this, [this]() {
        VT_INFO("User logged out");
        emit userLoggedOut();
        showPage(PageId{1});  // Return to login
    });
    
    connect(authService_.get(), &AuthService::authenticationFailed,
            this, [this](const QString& reason) {
        VT_WARN("Authentication failed: {}", reason.toStdString());
        if (loginZone_) {
            loginZone_->setErrorMessage(reason);
        }
    });
    
    VT_INFO("Authentication system initialized with {} employees", 
            employeeStore_->activeCount());
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
    loginZone_ = nullptr;  // Will be recreated
    
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
    loginZone_ = loginZone.get();  // Store non-owning pointer
    loginPage->addZone(std::move(loginZone), X(60), Y(160), X(550), Y(800));
    
    // Connect login zone signal to handle actions
    connect(loginZone_, &LoginZone::pinEntered, this, &MainWindow::onLoginAction);
    
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
    signInOutBtn->setAction([this]() {
        VT_INFO("Sign In/Out pressed - validating PIN");
        emit loginZone_->pinEntered("SIGNOUT");
    });
    loginPage->addZone(std::move(signInOutBtn), X(rightX), Y(startY), X(btnWidth), Y(btnHeight));
    
    // Second column
    int rightX2 = rightX + btnWidth + spacing;
    
    // Tables button
    auto tablesBtn = std::make_unique<ButtonZone>();
    tablesBtn->setText("Tables");
    tablesBtn->setBackgroundColor(colors::Teal);
    tablesBtn->setFontSize(FontSize::Huge);
    tablesBtn->setAction([this]() {
        VT_INFO("Tables pressed - validating PIN");
        emit loginZone_->pinEntered("TABLES");
    });
    loginPage->addZone(std::move(tablesBtn), X(rightX2), Y(startY), X(btnWidth), Y(btnHeight));
    
    // Row 2
    int row2Y = startY + btnHeight + spacing;
    
    // Takeout button
    auto takeoutBtn = std::make_unique<ButtonZone>();
    takeoutBtn->setText("Takeout");
    takeoutBtn->setBackgroundColor(colors::Orange);
    takeoutBtn->setFontSize(FontSize::Huge);
    takeoutBtn->setAction([this]() {
        VT_INFO("Takeout pressed - validating PIN");
        emit loginZone_->pinEntered("TAKEOUT");
    });
    loginPage->addZone(std::move(takeoutBtn), X(rightX), Y(row2Y), X(btnWidth), Y(btnHeight));
    
    // Quick Dine In button
    auto quickDineInBtn = std::make_unique<ButtonZone>();
    quickDineInBtn->setText("Quick Dine In");
    quickDineInBtn->setBackgroundColor(colors::VTGreen);
    quickDineInBtn->setFontSize(FontSize::Huge);
    quickDineInBtn->setAction([this]() {
        VT_INFO("Quick Dine In pressed - validating PIN");
        emit loginZone_->pinEntered("QUICKDINE");
    });
    loginPage->addZone(std::move(quickDineInBtn), X(rightX2), Y(row2Y), X(btnWidth), Y(btnHeight));
    
    // Row 3: Additional options (smaller)
    int row3Y = row2Y + btnHeight + spacing + 30;
    int smallBtnHeight = 130;
    
    auto reportsBtn = std::make_unique<ButtonZone>();
    reportsBtn->setText("Reports");
    reportsBtn->setBackgroundColor(colors::Purple);
    reportsBtn->setFontSize(FontSize::XLarge);
    reportsBtn->setAction([this]() {
        emit loginZone_->pinEntered("REPORTS");
    });
    loginPage->addZone(std::move(reportsBtn), X(rightX), Y(row3Y), X(btnWidth), Y(smallBtnHeight));
    
    auto checksBtn = std::make_unique<ButtonZone>();
    checksBtn->setText("Open Checks");
    checksBtn->setBackgroundColor(colors::VTYellow);
    checksBtn->setFontSize(FontSize::XLarge);
    checksBtn->setAction([this]() {
        emit loginZone_->pinEntered("CHECKS");
    });
    loginPage->addZone(std::move(checksBtn), X(rightX2), Y(row3Y), X(btnWidth), Y(smallBtnHeight));
    
    // Row 4
    int row4Y = row3Y + smallBtnHeight + spacing;
    
    auto settingsBtn = std::make_unique<ButtonZone>();
    settingsBtn->setText("Settings");
    settingsBtn->setBackgroundColor(colors::Gray);
    settingsBtn->setFontSize(FontSize::XLarge);
    settingsBtn->setAction([this]() {
        emit loginZone_->pinEntered("SETTINGS");
    });
    loginPage->addZone(std::move(settingsBtn), X(rightX), Y(row4Y), X(btnWidth), Y(smallBtnHeight));
    
    auto managerBtn = std::make_unique<ButtonZone>();
    managerBtn->setText("Manager");
    managerBtn->setBackgroundColor(colors::VTRed);
    managerBtn->setFontSize(FontSize::XLarge);
    managerBtn->setAction([this]() {
        emit loginZone_->pinEntered("MANAGER");
    });
    loginPage->addZone(std::move(managerBtn), X(rightX2), Y(row4Y), X(btnWidth), Y(smallBtnHeight));
    
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
    
    // ========================================================================
    // Page 4: Settings Page (Superuser Only)
    // ========================================================================
    auto settingsPage = std::make_unique<Page>(PageType::Settings);
    settingsPage->setId(PageId{4});
    settingsPage->setPageName("Settings");
    settingsPage->setBackgroundColor(QColor(30, 30, 40));
    
    // Settings zone takes most of the page
    auto settingsZone = std::make_unique<SettingsZone>();
    settingsZone->setBackgroundColor(QColor(30, 30, 40));
    settingsZone->setBorderWidth(0);
    
    // Connect settings zone signals
    connect(settingsZone.get(), &SettingsZone::hardwareRequested, this, [this]() {
        VT_INFO("Hardware settings requested");
        app().navigateTo(PageId{5});  // Hardware page
    });
    
    connect(settingsZone.get(), &SettingsZone::taxRequested, this, [this]() {
        VT_INFO("Tax settings requested");
        app().navigateTo(PageId{6});  // Tax page
    });
    
    connect(settingsZone.get(), &SettingsZone::clearSystemRequested, this, [this]() {
        VT_INFO("Clear system requested");
        app().navigateTo(PageId{7});  // Clear system page
    });
    
    connect(settingsZone.get(), &SettingsZone::backRequested, this, [this]() {
        authService_->logout();  // Log out superuser
        app().navigateTo(PageId{1});  // Back to login
    });
    
    settingsPage->addZone(std::move(settingsZone), X(20), Y(20), X(1880), Y(1040));
    
    addPage(std::move(settingsPage));
    
    // ========================================================================
    // Page 5: Hardware Settings Page
    // ========================================================================
    auto hardwarePage = std::make_unique<Page>(PageType::Settings);
    hardwarePage->setId(PageId{5});
    hardwarePage->setPageName("Hardware Settings");
    hardwarePage->setBackgroundColor(QColor(30, 30, 40));
    
    // Header
    auto hwHeader = std::make_unique<ButtonZone>();
    hwHeader->setText("Hardware Settings");
    hwHeader->setBackgroundColor(QColor(0, 150, 136));
    hwHeader->setFontSize(FontSize::XLarge);
    hwHeader->setBorderWidth(0);
    hardwarePage->addZone(std::move(hwHeader), X(20), Y(15), X(1700), Y(70));
    
    // Back button
    auto hwBackBtn = std::make_unique<ButtonZone>();
    hwBackBtn->setText("← Back");
    hwBackBtn->setBackgroundColor(colors::DarkGray);
    hwBackBtn->setFontSize(FontSize::Large);
    hwBackBtn->setAction([]() {
        app().navigateTo(PageId{4});  // Back to settings
    });
    hardwarePage->addZone(std::move(hwBackBtn), X(1740), Y(15), X(160), Y(70));
    
    // Display section
    auto displayLabel = std::make_unique<ButtonZone>();
    displayLabel->setText("Displays");
    displayLabel->setBackgroundColor(QColor(40, 40, 50));
    displayLabel->setFontSize(FontSize::Large);
    displayLabel->setBorderWidth(0);
    displayLabel->setAlignment(HAlign::Left, VAlign::Center);
    hardwarePage->addZone(std::move(displayLabel), X(20), Y(110), X(400), Y(50));
    
    auto addDisplayBtn = std::make_unique<ButtonZone>();
    addDisplayBtn->setText("+ Add Display");
    addDisplayBtn->setBackgroundColor(colors::Teal);
    addDisplayBtn->setFontSize(FontSize::Large);
    addDisplayBtn->setAction([]() {
        VT_INFO("Add display - not yet implemented");
    });
    hardwarePage->addZone(std::move(addDisplayBtn), X(20), Y(170), X(400), Y(120));
    
    // Display list placeholder
    auto displayList = std::make_unique<ButtonZone>();
    displayList->setText("No displays configured\n\n(Display list will appear here)");
    displayList->setBackgroundColor(QColor(40, 40, 50));
    displayList->setFontSize(FontSize::Normal);
    displayList->setAlignment(HAlign::Center, VAlign::Center);
    hardwarePage->addZone(std::move(displayList), X(20), Y(310), X(400), Y(400));
    
    // Printer section
    auto printerLabel = std::make_unique<ButtonZone>();
    printerLabel->setText("Printers");
    printerLabel->setBackgroundColor(QColor(40, 40, 50));
    printerLabel->setFontSize(FontSize::Large);
    printerLabel->setBorderWidth(0);
    printerLabel->setAlignment(HAlign::Left, VAlign::Center);
    hardwarePage->addZone(std::move(printerLabel), X(460), Y(110), X(400), Y(50));
    
    auto addPrinterBtn = std::make_unique<ButtonZone>();
    addPrinterBtn->setText("+ Add Printer");
    addPrinterBtn->setBackgroundColor(colors::Purple);
    addPrinterBtn->setFontSize(FontSize::Large);
    addPrinterBtn->setAction([]() {
        VT_INFO("Add printer - not yet implemented");
    });
    hardwarePage->addZone(std::move(addPrinterBtn), X(460), Y(170), X(400), Y(120));
    
    // Printer list placeholder
    auto printerList = std::make_unique<ButtonZone>();
    printerList->setText("No printers configured\n\n(Printer list will appear here)");
    printerList->setBackgroundColor(QColor(40, 40, 50));
    printerList->setFontSize(FontSize::Normal);
    printerList->setAlignment(HAlign::Center, VAlign::Center);
    hardwarePage->addZone(std::move(printerList), X(460), Y(310), X(400), Y(400));
    
    addPage(std::move(hardwarePage));
    
    // ========================================================================
    // Page 6: Tax Settings Page
    // ========================================================================
    auto taxPage = std::make_unique<Page>(PageType::Settings);
    taxPage->setId(PageId{6});
    taxPage->setPageName("Tax Settings");
    taxPage->setBackgroundColor(QColor(30, 30, 40));
    
    // Header
    auto taxHeader = std::make_unique<ButtonZone>();
    taxHeader->setText("Tax Settings");
    taxHeader->setBackgroundColor(QColor(63, 81, 181));
    taxHeader->setFontSize(FontSize::XLarge);
    taxHeader->setBorderWidth(0);
    taxPage->addZone(std::move(taxHeader), X(20), Y(15), X(1700), Y(70));
    
    // Back button
    auto taxBackBtn = std::make_unique<ButtonZone>();
    taxBackBtn->setText("← Back");
    taxBackBtn->setBackgroundColor(colors::DarkGray);
    taxBackBtn->setFontSize(FontSize::Large);
    taxBackBtn->setAction([]() {
        app().navigateTo(PageId{4});  // Back to settings
    });
    taxPage->addZone(std::move(taxBackBtn), X(1740), Y(15), X(160), Y(70));
    
    // Tax rate placeholder
    auto taxInfo = std::make_unique<ButtonZone>();
    taxInfo->setText("Tax Configuration\n\n"
                     "Sales Tax Rate: 8.25%\n\n"
                     "(Tax settings editor coming soon)");
    taxInfo->setBackgroundColor(QColor(40, 40, 50));
    taxInfo->setFontSize(FontSize::Large);
    taxInfo->setAlignment(HAlign::Center, VAlign::Center);
    taxPage->addZone(std::move(taxInfo), X(20), Y(110), X(900), Y(400));
    
    addPage(std::move(taxPage));
    
    // ========================================================================
    // Page 7: Clear System Page
    // ========================================================================
    auto clearPage = std::make_unique<Page>(PageType::Settings);
    clearPage->setId(PageId{7});
    clearPage->setPageName("Clear System");
    clearPage->setBackgroundColor(QColor(40, 20, 20));
    
    // Clear system zone
    auto clearZone = std::make_unique<ClearSystemZone>();
    clearZone->setBackgroundColor(QColor(40, 20, 20));
    clearZone->setBorderWidth(0);
    
    connect(clearZone.get(), &ClearSystemZone::clearConfirmed, this, [this]() {
        VT_WARN("SYSTEM CLEAR CONFIRMED - Would clear database here");
        // TODO: Actually clear the database (checks, orders, transactions)
        // Keep: menu items, employees, settings
        
        QMessageBox::information(this, "System Cleared",
            "Database has been cleared.\n\n"
            "Menu items, employees, and settings have been preserved.");
        
        app().navigateTo(PageId{4});  // Back to settings
    });
    
    connect(clearZone.get(), &ClearSystemZone::backRequested, this, []() {
        app().navigateTo(PageId{4});  // Back to settings
    });
    
    clearPage->addZone(std::move(clearZone), X(20), Y(20), X(1880), Y(1040));
    
    addPage(std::move(clearPage));
    
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

void MainWindow::onLoginAction(const QString& action) {
    if (!loginZone_) return;
    
    QString pin = loginZone_->enteredPin();
    
    // Sign out doesn't require a PIN
    if (action == "SIGNOUT") {
        if (authService_->isLoggedIn()) {
            authService_->logout();
            loginZone_->clearPin();
            VT_INFO("User signed out");
        } else {
            loginZone_->setErrorMessage("No user signed in");
        }
        return;
    }
    
    // All other actions require a valid PIN
    if (pin.isEmpty()) {
        loginZone_->setErrorMessage("Enter your PIN first");
        return;
    }
    
    // Store the pending action for after successful auth
    pendingAction_ = action;
    
    // Attempt authentication
    auto result = authService_->authenticate(pin);
    
    if (result.success) {
        loginZone_->clearPin();
        showPostLoginPage(pendingAction_);
        pendingAction_.clear();
    } else {
        // Error message is set via signal from auth service
        // Don't clear PIN so user can try again
    }
}

void MainWindow::onAuthenticationResult(bool success, const QString& message) {
    if (!loginZone_) return;
    
    if (success) {
        loginZone_->clearPin();
        loginZone_->clearError();
        
        if (!pendingAction_.isEmpty()) {
            showPostLoginPage(pendingAction_);
            pendingAction_.clear();
        }
    } else {
        loginZone_->setErrorMessage(message);
    }
}

void MainWindow::showPostLoginPage(const QString& action) {
    // Settings is SUPERUSER ONLY - not just SystemSettings permission
    if (action == "SETTINGS") {
        if (!authService_->isSuperuser()) {
            if (loginZone_) {
                loginZone_->setErrorMessage("Access denied: Superuser only");
            }
            VT_WARN("Settings access denied - superuser only");
            return;
        }
    } else if (action == "MANAGER") {
        if (!authService_->hasPermission(Permission::EditEmployees)) {
            if (loginZone_) {
                loginZone_->setErrorMessage("Permission denied: Manager");
            }
            return;
        }
    } else if (action == "REPORTS") {
        if (!authService_->hasPermission(Permission::ViewReports)) {
            if (loginZone_) {
                loginZone_->setErrorMessage("Permission denied: Reports");
            }
            return;
        }
    }
    
    // Navigate to the appropriate page
    if (action == "TABLES") {
        app().navigateTo(PageId{2});  // Tables page
    } else if (action == "TAKEOUT" || action == "QUICKDINE") {
        app().navigateTo(PageId{3});  // Order page
    } else if (action == "REPORTS") {
        VT_INFO("Reports page - {}",
                authService_->isSuperuser() ? "SUPERUSER ACCESS" : 
                authService_->currentEmployee()->fullName().toStdString());
        // TODO: Navigate to reports page when implemented
        app().navigateTo(PageId{3});  // Placeholder
    } else if (action == "CHECKS") {
        VT_INFO("Open Checks - logged in as {}", 
                authService_->currentEmployee()->fullName().toStdString());
        // TODO: Open checks page
        app().navigateTo(PageId{3});  // Placeholder
    } else if (action == "SETTINGS") {
        VT_INFO("Settings page - SUPERUSER ACCESS GRANTED");
        app().navigateTo(PageId{4});  // Settings page
    } else if (action == "MANAGER") {
        VT_INFO("Manager page - logged in as {}",
                authService_->currentEmployee()->fullName().toStdString());
        // TODO: Navigate to manager page when implemented
        app().navigateTo(PageId{3});  // Placeholder
    }
}

} // namespace vt2
