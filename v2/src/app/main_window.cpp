/*
 * ViewTouch V2 - Main Window Implementation
 */

#include "app/main_window.hpp"
#include "app/application.hpp"
#include "terminal/control.hpp"
#include "terminal/terminal.hpp"
#include "zone/zone.hpp"
#include "zone/page.hpp"
#include "zone/zone_db.hpp"
#include "render/renderer.hpp"
#include "render/textures.hpp"
#include "core/colors.hpp"
#include "core/fonts.hpp"
#include "editor/edit_mode.hpp"
#include "editor/edit_toolbar.hpp"
#include "editor/zone_properties.hpp"
#include "editor/page_properties.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QMenuBar>
#include <QMenu>
#include <QFileDialog>
#include <QMessageBox>

#include <algorithm>

namespace vt {

/*************************************************************
 * ButtonZone - A simple touchable button
 *************************************************************/
class ButtonZone : public Zone {
public:
    ButtonZone() = default;
    
    void setLabel(const QString& label) { label_ = label; }
    QString label() const { return label_; }
    
    void setJumpTarget(int pageId, JumpType jt = JumpType::Normal) {
        jumpPageId_ = pageId;
        jumpType_ = jt;
    }
    
protected:
    void renderContent(Renderer& renderer, Terminal* term) override {
        QRect r(region().x, region().y, region().w, region().h);
        
        // Get color based on state
        uint8_t colorId = state(currentState()).color;
        if (colorId == 0) {
            colorId = static_cast<uint8_t>(TextColor::Black);
        }
        
        renderer.drawText(label_, r, 
                         static_cast<uint8_t>(FontId::Times_20),
                         colorId, TextAlign::Center);
    }
    
    int touch(Terminal* term, int tx, int ty) override {
        int result = Zone::touch(term, tx, ty);
        if (jumpPageId_ > 0 && term) {
            term->jumpToPage(jumpPageId_, jumpType_);
        }
        return result;
    }
    
private:
    QString label_;
    int jumpPageId_ = 0;
    JumpType jumpType_ = JumpType::Normal;
};

/*************************************************************
 * MainWindow Implementation
 *************************************************************/
MainWindow::MainWindow(Control* control, QWidget* parent)
    : QMainWindow(parent)
    , control_(control)
    , renderer_(std::make_unique<Renderer>(this))
    , editMode_(std::make_unique<EditMode>(this))
{
    setupUi();
    setupMenuBar();
    
    // Create a terminal
    terminal_ = control_->createTerminal();
    terminal_->setSize(1024, 768);
    
    // Connect redraw signal
    connect(terminal_, &Terminal::redrawRequested,
            this, &MainWindow::onRedrawRequested);
    
    // Connect edit mode signals
    connect(editMode_.get(), &EditMode::editModeChanged,
            this, &MainWindow::onEditModeChanged);
    connect(editMode_.get(), &EditMode::requestRedraw,
            this, &MainWindow::onRedrawRequested);
    
    // Setup renderer resources
    renderer_->setPalette(app()->palette());
    renderer_->setFontManager(app()->fontManager());
    renderer_->setTextures(app()->textures());
    renderer_->setDesignSize(1024, 768);
    
    // Create demo pages
    createDemoPages();
}

MainWindow::~MainWindow() = default;

bool MainWindow::isEditMode() const {
    return editMode_ && editMode_->isActive();
}

void MainWindow::setupMenuBar() {
    auto* menuBar = this->menuBar();
    
    // File menu
    auto* fileMenu = menuBar->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Save Pages"), this, &MainWindow::onSaveRequested, QKeySequence::Save);
    fileMenu->addAction(tr("&Load Pages"), this, &MainWindow::onLoadRequested, QKeySequence::Open);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close, QKeySequence::Quit);
    
    // Edit menu
    auto* editMenu = menuBar->addMenu(tr("&Edit"));
    auto* editModeAction = editMenu->addAction(tr("&Edit Mode"), [this]() {
        editMode_->toggle();
    });
    editModeAction->setShortcut(QKeySequence("F2"));
    editModeAction->setCheckable(true);
    connect(editMode_.get(), &EditMode::editModeChanged, editModeAction, &QAction::setChecked);
    
    editMenu->addSeparator();
    editMenu->addAction(tr("&New Zone"), this, &MainWindow::onNewZoneRequested, QKeySequence("Ctrl+N"));
    editMenu->addAction(tr("New &Page"), this, &MainWindow::onNewPageRequested, QKeySequence("Ctrl+Shift+N"));
    editMenu->addSeparator();
    editMenu->addAction(tr("Page &Properties..."), [this]() {
        if (terminal_ && terminal_->currentPage()) {
            onPagePropertiesRequested(terminal_->currentPage());
        }
    });
    
    // View menu
    auto* viewMenu = menuBar->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Fullscreen"), this, &MainWindow::toggleFullscreen, QKeySequence("F11"));
    
    // Help menu
    auto* helpMenu = menuBar->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), [this]() {
        QMessageBox::about(this, tr("About ViewTouch"),
            tr("ViewTouch V2\n\nA faithful Qt6 reimplementation of the classic ViewTouch POS system."));
    });
}

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("ViewTouch"));
    setMinimumSize(800, 600);
    resize(1024, 768);
    
    // Create terminal widget
    terminalWidget_ = new TerminalWidget(nullptr, renderer_.get(), this);
    setCentralWidget(terminalWidget_);
    
    // Set dark background
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(40, 40, 40));
    setPalette(pal);
}

void MainWindow::createDemoPages() {
    ZoneDB* db = control_->zoneDb();
    
    // Create home page
    auto homePage = std::make_unique<Page>();
    homePage->setId(1);
    homePage->setName(QStringLiteral("Home"));
    homePage->setType(PageType::Index);
    homePage->setSize(1024, 768);
    homePage->setDefaultTexture(static_cast<uint8_t>(TextureId::Sand));
    
    // Create some demo buttons
    int buttonW = 180;
    int buttonH = 60;
    int spacing = 20;
    int startX = (1024 - (3 * buttonW + 2 * spacing)) / 2;
    int startY = 150;
    
    // Title zone (just text, no interaction)
    auto titleZone = std::make_unique<ButtonZone>();
    titleZone->setRegion(Region{200, 40, 624, 80});
    titleZone->setName(QStringLiteral("Title"));
    titleZone->setBehavior(ZoneBehavior::None);
    
    ZoneState titleState;
    titleState.frame = ZoneFrame::Double;
    titleState.texture = static_cast<uint8_t>(TextureId::Wood);
    titleState.color = static_cast<uint8_t>(TextColor::White);
    titleZone->setState(0, titleState);
    static_cast<ButtonZone*>(titleZone.get())->setLabel(QStringLiteral("ViewTouch POS System"));
    
    homePage->addZone(std::move(titleZone));
    
    // Row 1 buttons
    const QString labels1[] = {QStringLiteral("Dine In"), 
                               QStringLiteral("Take Out"), 
                               QStringLiteral("Delivery")};
    for (int i = 0; i < 3; ++i) {
        auto btn = std::make_unique<ButtonZone>();
        btn->setRegion(Region{startX + i * (buttonW + spacing), startY, buttonW, buttonH});
        btn->setName(labels1[i]);
        btn->setBehavior(ZoneBehavior::Toggle);
        
        ZoneState normal;
        normal.frame = ZoneFrame::Raised;
        normal.texture = static_cast<uint8_t>(TextureId::DarkBlue);
        normal.color = static_cast<uint8_t>(TextColor::White);
        btn->setState(0, normal);
        
        ZoneState selected;
        selected.frame = ZoneFrame::Inset;
        selected.texture = static_cast<uint8_t>(TextureId::Blue);
        selected.color = static_cast<uint8_t>(TextColor::Yellow);
        btn->setState(1, selected);
        
        btn->setLabel(labels1[i]);
        btn->setJumpTarget(2);
        
        homePage->addZone(std::move(btn));
    }
    
    // Row 2 - More options
    startY += buttonH + spacing;
    const QString labels2[] = {QStringLiteral("Tables"), 
                               QStringLiteral("Manager"), 
                               QStringLiteral("Reports")};
    for (int i = 0; i < 3; ++i) {
        auto btn = std::make_unique<ButtonZone>();
        btn->setRegion(Region{startX + i * (buttonW + spacing), startY, buttonW, buttonH});
        btn->setName(labels2[i]);
        btn->setBehavior(ZoneBehavior::Toggle);
        
        ZoneState normal;
        normal.frame = ZoneFrame::Raised;
        normal.texture = static_cast<uint8_t>(TextureId::DarkGreen);
        normal.color = static_cast<uint8_t>(TextColor::White);
        btn->setState(0, normal);
        
        ZoneState selected;
        selected.frame = ZoneFrame::Inset;
        selected.texture = static_cast<uint8_t>(TextureId::Green);
        selected.color = static_cast<uint8_t>(TextColor::White);
        btn->setState(1, selected);
        
        btn->setLabel(labels2[i]);
        
        homePage->addZone(std::move(btn));
    }
    
    // Row 3 - Utilities
    startY += buttonH + spacing;
    const QString labels3[] = {QStringLiteral("Timeclock"), 
                               QStringLiteral("Settings"), 
                               QStringLiteral("Exit")};
    for (int i = 0; i < 3; ++i) {
        auto btn = std::make_unique<ButtonZone>();
        btn->setRegion(Region{startX + i * (buttonW + spacing), startY, buttonW, buttonH});
        btn->setName(labels3[i]);
        btn->setBehavior(ZoneBehavior::Toggle);
        
        ZoneState normal;
        normal.frame = ZoneFrame::Raised;
        normal.texture = static_cast<uint8_t>(TextureId::DarkRed);
        normal.color = static_cast<uint8_t>(TextColor::White);
        btn->setState(0, normal);
        
        ZoneState selected;
        selected.frame = ZoneFrame::Inset;
        selected.texture = static_cast<uint8_t>(TextureId::Red);
        selected.color = static_cast<uint8_t>(TextColor::White);
        btn->setState(1, selected);
        
        btn->setLabel(labels3[i]);
        
        homePage->addZone(std::move(btn));
    }
    
    // Status bar at bottom
    auto statusZone = std::make_unique<ButtonZone>();
    statusZone->setRegion(Region{20, 700, 984, 48});
    statusZone->setName(QStringLiteral("Status"));
    statusZone->setBehavior(ZoneBehavior::None);
    
    ZoneState statusState;
    statusState.frame = ZoneFrame::Inset;
    statusState.texture = static_cast<uint8_t>(TextureId::Parchment);
    statusState.color = static_cast<uint8_t>(TextColor::Black);
    statusZone->setState(0, statusState);
    static_cast<ButtonZone*>(statusZone.get())->setLabel(QStringLiteral("Ready - No Employee Signed In"));
    
    homePage->addZone(std::move(statusZone));
    
    // Add page to database
    int homeId = homePage->id();
    db->addPage(std::move(homePage));
    
    // Create order entry page
    auto orderPage = std::make_unique<Page>();
    orderPage->setId(2);
    orderPage->setName(QStringLiteral("Order Entry"));
    orderPage->setType(PageType::Item);
    orderPage->setSize(1024, 768);
    
    // Back button
    auto backBtn = std::make_unique<ButtonZone>();
    backBtn->setRegion(Region{20, 20, 100, 50});
    backBtn->setName(QStringLiteral("Back"));
    backBtn->setBehavior(ZoneBehavior::Toggle);
    
    ZoneState backNormal;
    backNormal.frame = ZoneFrame::Raised;
    backNormal.texture = static_cast<uint8_t>(TextureId::Gray);
    backNormal.color = static_cast<uint8_t>(TextColor::Black);
    backBtn->setState(0, backNormal);
    
    backBtn->setLabel(QStringLiteral("< Back"));
    backBtn->setJumpTarget(1, JumpType::Return);
    
    orderPage->addZone(std::move(backBtn));
    
    // Order page title
    auto orderTitle = std::make_unique<ButtonZone>();
    orderTitle->setRegion(Region{200, 20, 624, 50});
    orderTitle->setName(QStringLiteral("OrderTitle"));
    orderTitle->setBehavior(ZoneBehavior::None);
    
    ZoneState orderTitleState;
    orderTitleState.frame = ZoneFrame::Border;
    orderTitleState.texture = static_cast<uint8_t>(TextureId::DarkBlue);
    orderTitleState.color = static_cast<uint8_t>(TextColor::White);
    orderTitle->setState(0, orderTitleState);
    static_cast<ButtonZone*>(orderTitle.get())->setLabel(QStringLiteral("Order Entry"));
    
    orderPage->addZone(std::move(orderTitle));
    
    // Menu item buttons
    const QString menuItems[] = {
        QStringLiteral("Hamburger"), QStringLiteral("Cheeseburger"), 
        QStringLiteral("Chicken"), QStringLiteral("Fish"),
        QStringLiteral("Salad"), QStringLiteral("Soup"),
        QStringLiteral("Fries"), QStringLiteral("Onion Rings"),
        QStringLiteral("Soda"), QStringLiteral("Coffee"),
        QStringLiteral("Tea"), QStringLiteral("Water")
    };
    
    int cols = 4;
    int rows = 3;
    buttonW = 150;
    buttonH = 80;
    spacing = 15;
    startX = 50;
    startY = 100;
    
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int idx = row * cols + col;
            if (idx >= 12) break;
            
            auto btn = std::make_unique<ButtonZone>();
            btn->setRegion(Region{
                startX + col * (buttonW + spacing),
                startY + row * (buttonH + spacing),
                buttonW, buttonH
            });
            btn->setName(menuItems[idx]);
            btn->setBehavior(ZoneBehavior::Select);
            
            ZoneState normal;
            normal.frame = ZoneFrame::Raised;
            normal.texture = static_cast<uint8_t>(TextureId::Tan);
            normal.color = static_cast<uint8_t>(TextColor::Black);
            btn->setState(0, normal);
            
            ZoneState selected;
            selected.frame = ZoneFrame::Inset;
            selected.texture = static_cast<uint8_t>(TextureId::Yellow);
            selected.color = static_cast<uint8_t>(TextColor::Black);
            btn->setState(1, selected);
            
            btn->setLabel(menuItems[idx]);
            
            orderPage->addZone(std::move(btn));
        }
    }
    
    // Order list area
    auto orderList = std::make_unique<ButtonZone>();
    orderList->setRegion(Region{700, 100, 280, 500});
    orderList->setName(QStringLiteral("OrderList"));
    orderList->setBehavior(ZoneBehavior::None);
    
    ZoneState orderListState;
    orderListState.frame = ZoneFrame::Inset;
    orderListState.texture = static_cast<uint8_t>(TextureId::White);
    orderListState.color = static_cast<uint8_t>(TextColor::Black);
    orderList->setState(0, orderListState);
    static_cast<ButtonZone*>(orderList.get())->setLabel(QStringLiteral("Order Items"));
    
    orderPage->addZone(std::move(orderList));
    
    db->addPage(std::move(orderPage));
    
    // Jump to home page
    terminal_->jumpToPage(homeId, JumpType::Home);
    
    // Update terminal widget with terminal
    terminalWidget_->requestRedraw();
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_F11:
            toggleFullscreen();
            break;
        case Qt::Key_Escape:
            if (isFullScreen()) {
                showNormal();
            }
            break;
        default:
            QMainWindow::keyPressEvent(event);
            break;
    }
}

void MainWindow::onRedrawRequested() {
    if (terminalWidget_) {
        terminalWidget_->update();
    }
}

void MainWindow::onEditModeChanged(bool active) {
    if (terminalWidget_) {
        terminalWidget_->setEditMode(active ? editMode_.get() : nullptr);
        terminalWidget_->update();
    }
    
    // Show/hide edit toolbar
    if (active && !editToolbar_) {
        editToolbar_ = new EditToolbar(editMode_.get(), this);
        addToolBar(Qt::TopToolBarArea, editToolbar_);
        
        connect(editToolbar_, &EditToolbar::newZoneRequested,
                this, &MainWindow::onNewZoneRequested);
        connect(editToolbar_, &EditToolbar::newPageRequested,
                this, &MainWindow::onNewPageRequested);
        connect(editToolbar_, &EditToolbar::saveRequested,
                this, &MainWindow::onSaveRequested);
        connect(editToolbar_, &EditToolbar::loadRequested,
                this, &MainWindow::onLoadRequested);
        connect(editToolbar_, &EditToolbar::propertiesRequested, this, [this]() {
            if (!editMode_->selectedZones().empty()) {
                onZonePropertiesRequested(editMode_->selectedZones().front());
            } else if (terminal_ && terminal_->currentPage()) {
                onPagePropertiesRequested(terminal_->currentPage());
            }
        });
    }
    
    if (editToolbar_) {
        editToolbar_->setVisible(active);
    }
}

void MainWindow::onZonePropertiesRequested(Zone* zone) {
    if (!zone) return;
    
    ZonePropertiesDialog dlg(zone, this);
    if (dlg.exec() == QDialog::Accepted) {
        dlg.applyChanges();
        if (terminalWidget_) {
            terminalWidget_->update();
        }
    }
}

void MainWindow::onPagePropertiesRequested(Page* page) {
    if (!page) return;
    
    PagePropertiesDialog dlg(page, this);
    if (dlg.exec() == QDialog::Accepted) {
        dlg.applyChanges();
        if (terminalWidget_) {
            terminalWidget_->update();
        }
    }
}

void MainWindow::onNewZoneRequested() {
    if (!terminal_ || !terminal_->currentPage()) return;
    
    // Create a new button zone at a default position
    auto zone = std::make_unique<ButtonZone>();
    zone->setRegion(Region{100, 100, 150, 60});
    zone->setName(QStringLiteral("New Zone"));
    zone->setBehavior(ZoneBehavior::Toggle);
    
    ZoneState normal;
    normal.frame = ZoneFrame::Raised;
    normal.texture = static_cast<uint8_t>(TextureId::Gray);
    normal.color = static_cast<uint8_t>(TextColor::Black);
    zone->setState(0, normal);
    
    ZoneState selected;
    selected.frame = ZoneFrame::Inset;
    selected.texture = static_cast<uint8_t>(TextureId::Blue);
    selected.color = static_cast<uint8_t>(TextColor::Black);
    zone->setState(1, selected);
    
    static_cast<ButtonZone*>(zone.get())->setLabel(QStringLiteral("New Zone"));
    
    Zone* rawPtr = zone.get();
    terminal_->currentPage()->addZone(std::move(zone));
    
    // Select the new zone
    if (editMode_) {
        editMode_->selectZone(rawPtr);
    }
    
    if (terminalWidget_) {
        terminalWidget_->update();
    }
}

void MainWindow::onNewPageRequested() {
    if (!control_ || !control_->zoneDb()) return;
    
    ZoneDB* db = control_->zoneDb();
    
    // Find next available page ID
    int nextId = 100;
    while (db->page(nextId)) {
        nextId++;
    }
    
    auto page = std::make_unique<Page>();
    page->setId(nextId);
    page->setName(QStringLiteral("New Page"));
    page->setType(PageType::Index);
    page->setSize(1024, 768);
    page->setDefaultTexture(static_cast<uint8_t>(TextureId::Sand));
    
    db->addPage(std::move(page));
    
    if (terminal_) {
        terminal_->jumpToPage(nextId);
    }
    
    if (terminalWidget_) {
        terminalWidget_->update();
    }
}

void MainWindow::onSaveRequested() {
    QString filename = QFileDialog::getSaveFileName(this,
        tr("Save Pages"), QString(), tr("ViewTouch Pages (*.vtp);;All Files (*)"));
    
    if (!filename.isEmpty()) {
        // TODO: Implement page serialization
        QMessageBox::information(this, tr("Save"),
            tr("Page save not yet implemented.\nFile: %1").arg(filename));
    }
}

void MainWindow::onLoadRequested() {
    QString filename = QFileDialog::getOpenFileName(this,
        tr("Load Pages"), QString(), tr("ViewTouch Pages (*.vtp);;All Files (*)"));
    
    if (!filename.isEmpty()) {
        // TODO: Implement page deserialization
        QMessageBox::information(this, tr("Load"),
            tr("Page load not yet implemented.\nFile: %1").arg(filename));
    }
}

/*************************************************************
 * TerminalWidget Implementation
 *************************************************************/
TerminalWidget::TerminalWidget(Terminal* term, Renderer* renderer, QWidget* parent)
    : QWidget(parent)
    , terminal_(term)
    , renderer_(renderer)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

TerminalWidget::~TerminalWidget() = default;

void TerminalWidget::requestRedraw() {
    update();
}

QPoint TerminalWidget::screenToDesign(const QPoint& screen) const {
    if (!terminal_) return screen;
    return QPoint(
        (screen.x() * terminal_->width()) / width(),
        (screen.y() * terminal_->height()) / height()
    );
}

QPoint TerminalWidget::designToScreen(const QPoint& design) const {
    if (!terminal_) return design;
    return QPoint(
        (design.x() * width()) / terminal_->width(),
        (design.y() * height()) / terminal_->height()
    );
}

void TerminalWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    // Get terminal from main window if not set
    if (!terminal_) {
        MainWindow* mw = qobject_cast<MainWindow*>(window());
        if (mw) {
            terminal_ = mw->terminal();
        }
    }
    
    if (!renderer_ || !terminal_) {
        // Just fill with dark gray
        QPainter p(this);
        p.fillRect(rect(), QColor(60, 60, 60));
        return;
    }
    
    renderer_->setTargetSize(width(), height());
    renderer_->begin(this);
    renderer_->clear(QColor(60, 60, 60));
    
    terminal_->draw(*renderer_);
    
    renderer_->end();
    
    // Draw edit mode overlay
    if (editMode_ && editMode_->isActive()) {
        QPainter p(this);
        drawEditOverlay(p);
    }
}

void TerminalWidget::drawEditOverlay(QPainter& p) {
    if (!editMode_ || !terminal_) return;
    
    // Draw grid if enabled
    if (editMode_->isGridSnapEnabled()) {
        drawGrid(p);
    }
    
    // Draw selection for all selected zones
    for (Zone* zone : editMode_->selectedZones()) {
        drawSelectionHandles(p, zone);
    }
}

void TerminalWidget::drawSelectionHandles(QPainter& p, Zone* zone) {
    if (!zone || !terminal_) return;
    
    Region r = zone->region();
    
    // Convert to screen coordinates
    float scaleX = static_cast<float>(width()) / terminal_->width();
    float scaleY = static_cast<float>(height()) / terminal_->height();
    
    QRect screenRect(
        static_cast<int>(r.x * scaleX),
        static_cast<int>(r.y * scaleY),
        static_cast<int>(r.w * scaleX),
        static_cast<int>(r.h * scaleY)
    );
    
    // Draw selection outline
    p.setPen(QPen(Qt::cyan, 2, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(screenRect);
    
    // Draw resize handles
    const int handleSize = 8;
    p.setPen(QPen(Qt::white, 1));
    p.setBrush(Qt::cyan);
    
    // Corner handles
    QRect handles[] = {
        // TopLeft
        QRect(screenRect.left() - handleSize/2, screenRect.top() - handleSize/2, handleSize, handleSize),
        // TopRight
        QRect(screenRect.right() - handleSize/2, screenRect.top() - handleSize/2, handleSize, handleSize),
        // BottomLeft
        QRect(screenRect.left() - handleSize/2, screenRect.bottom() - handleSize/2, handleSize, handleSize),
        // BottomRight
        QRect(screenRect.right() - handleSize/2, screenRect.bottom() - handleSize/2, handleSize, handleSize),
        // Top
        QRect(screenRect.center().x() - handleSize/2, screenRect.top() - handleSize/2, handleSize, handleSize),
        // Bottom
        QRect(screenRect.center().x() - handleSize/2, screenRect.bottom() - handleSize/2, handleSize, handleSize),
        // Left
        QRect(screenRect.left() - handleSize/2, screenRect.center().y() - handleSize/2, handleSize, handleSize),
        // Right
        QRect(screenRect.right() - handleSize/2, screenRect.center().y() - handleSize/2, handleSize, handleSize)
    };
    
    for (const QRect& handle : handles) {
        p.drawRect(handle);
    }
}

void TerminalWidget::drawGrid(QPainter& p) {
    if (!editMode_ || !terminal_) return;
    
    int gridSize = editMode_->gridSize();
    
    float scaleX = static_cast<float>(width()) / terminal_->width();
    float scaleY = static_cast<float>(height()) / terminal_->height();
    
    p.setPen(QPen(QColor(100, 100, 100, 80), 1, Qt::DotLine));
    
    // Vertical lines
    for (int x = 0; x < terminal_->width(); x += gridSize) {
        int sx = static_cast<int>(x * scaleX);
        p.drawLine(sx, 0, sx, height());
    }
    
    // Horizontal lines
    for (int y = 0; y < terminal_->height(); y += gridSize) {
        int sy = static_cast<int>(y * scaleY);
        p.drawLine(0, sy, width(), sy);
    }
}

void TerminalWidget::mousePressEvent(QMouseEvent* event) {
    if (!terminal_) {
        QWidget::mousePressEvent(event);
        return;
    }
    
    // Convert to design coordinates
    int designX = (event->position().x() * terminal_->width()) / width();
    int designY = (event->position().y() * terminal_->height()) / height();
    
    // Handle edit mode
    if (editMode_ && editMode_->isActive() && event->button() == Qt::LeftButton) {
        Page* page = terminal_->currentPage();
        if (!page) return;
        
        EditTool tool = editMode_->currentTool();
        
        if (tool == EditTool::Select || tool == EditTool::Resize) {
            // Check if clicking on a selected zone's resize handle
            if (!editMode_->selectedZones().empty()) {
                Zone* zone = editMode_->selectedZones().front();
                ResizeHandle handle = editMode_->hitTestResizeHandle(zone, designX, designY);
                if (handle != ResizeHandle::None) {
                    editMode_->startDrag(zone, designX, designY, handle);
                    return;
                }
            }
            
            // Find zone at click position
            Zone* hitZone = nullptr;
            for (Zone* z : page->zones()) {
                Region r = z->region();
                if (designX >= r.x && designX < r.x + r.w &&
                    designY >= r.y && designY < r.y + r.h) {
                    hitZone = z;
                }
            }
            
            if (hitZone) {
                // Multi-select with Ctrl
                if (event->modifiers() & Qt::ControlModifier) {
                    const auto& sel = editMode_->selectedZones();
                    if (std::find(sel.begin(), sel.end(), hitZone) != sel.end()) {
                        editMode_->deselectZone(hitZone);
                    } else {
                        editMode_->addToSelection(hitZone);
                    }
                } else {
                    editMode_->selectZone(hitZone);
                    editMode_->startDrag(hitZone, designX, designY, ResizeHandle::None);
                }
            } else {
                editMode_->clearSelection();
            }
        } else if (tool == EditTool::Create) {
            // Create new zone at click position
            MainWindow* mw = qobject_cast<MainWindow*>(window());
            if (mw) {
                // TODO: Use selected zone type from toolbar
                mw->onNewZoneRequested();
                
                // Position the new zone at click location
                if (!editMode_->selectedZones().empty()) {
                    Zone* newZone = editMode_->selectedZones().front();
                    Region r = newZone->region();
                    r.x = editMode_->snapToGrid(designX);
                    r.y = editMode_->snapToGrid(designY);
                    newZone->setRegion(r);
                }
            }
        } else if (tool == EditTool::Delete) {
            Zone* hitZone = nullptr;
            for (Zone* z : page->zones()) {
                Region r = z->region();
                if (designX >= r.x && designX < r.x + r.w &&
                    designY >= r.y && designY < r.y + r.h) {
                    hitZone = z;
                }
            }
            if (hitZone) {
                editMode_->deleteZone(hitZone, page);
            }
        } else if (tool == EditTool::Properties) {
            Zone* hitZone = nullptr;
            for (Zone* z : page->zones()) {
                Region r = z->region();
                if (designX >= r.x && designX < r.x + r.w &&
                    designY >= r.y && designY < r.y + r.h) {
                    hitZone = z;
                }
            }
            if (hitZone) {
                MainWindow* mw = qobject_cast<MainWindow*>(window());
                if (mw) {
                    mw->onZonePropertiesRequested(hitZone);
                }
            }
        }
        
        update();
        return;
    }
    
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    
    terminal_->touch(designX, designY);
    update();
}

void TerminalWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!terminal_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    
    int designX = (event->position().x() * terminal_->width()) / width();
    int designY = (event->position().y() * terminal_->height()) / height();
    
    // Handle edit mode dragging
    if (editMode_ && editMode_->isActive() && editMode_->isDragging()) {
        editMode_->updateDrag(designX, designY);
        update();
        return;
    }
    
    // Update cursor in edit mode
    if (editMode_ && editMode_->isActive()) {
        Page* page = terminal_->currentPage();
        if (page) {
            // Check resize handles
            if (!editMode_->selectedZones().empty()) {
                Zone* zone = editMode_->selectedZones().front();
                ResizeHandle handle = editMode_->hitTestResizeHandle(zone, designX, designY);
                
                switch (handle) {
                    case ResizeHandle::TopLeft:
                    case ResizeHandle::BottomRight:
                        setCursor(Qt::SizeFDiagCursor);
                        return;
                    case ResizeHandle::TopRight:
                    case ResizeHandle::BottomLeft:
                        setCursor(Qt::SizeBDiagCursor);
                        return;
                    case ResizeHandle::Top:
                    case ResizeHandle::Bottom:
                        setCursor(Qt::SizeVerCursor);
                        return;
                    case ResizeHandle::Left:
                    case ResizeHandle::Right:
                        setCursor(Qt::SizeHorCursor);
                        return;
                    default:
                        break;
                }
            }
            
            // Check if over a zone
            for (Zone* z : page->zones()) {
                Region r = z->region();
                if (designX >= r.x && designX < r.x + r.w &&
                    designY >= r.y && designY < r.y + r.h) {
                    setCursor(Qt::SizeAllCursor);
                    return;
                }
            }
        }
        setCursor(Qt::ArrowCursor);
        return;
    }
    
    QWidget::mouseMoveEvent(event);
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (!terminal_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    
    // Handle edit mode
    if (editMode_ && editMode_->isActive()) {
        if (editMode_->isDragging()) {
            editMode_->endDrag();
        }
        update();
        return;
    }
    
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    
    int designX = (event->position().x() * terminal_->width()) / width();
    int designY = (event->position().y() * terminal_->height()) / height();
    
    terminal_->release(designX, designY);
    update();
}

void TerminalWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!terminal_ || !editMode_ || !editMode_->isActive()) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }
    
    int designX = (event->position().x() * terminal_->width()) / width();
    int designY = (event->position().y() * terminal_->height()) / height();
    
    // Double-click opens properties
    Page* page = terminal_->currentPage();
    if (page) {
        for (Zone* z : page->zones()) {
            Region r = z->region();
            if (designX >= r.x && designX < r.x + r.w &&
                designY >= r.y && designY < r.y + r.h) {
                MainWindow* mw = qobject_cast<MainWindow*>(window());
                if (mw) {
                    mw->onZonePropertiesRequested(z);
                }
                return;
            }
        }
    }
}

void TerminalWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update();
}

} // namespace vt
