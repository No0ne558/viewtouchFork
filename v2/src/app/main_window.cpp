/*
 * ViewTouch V2 - Main Window Implementation
 */

#include "app/main_window.hpp"
#include "app/application.hpp"
#include "terminal/control.hpp"
#include "terminal/terminal.hpp"
#include "zone/zone.hpp"
#include "zone/zone_types.hpp"
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
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>

#include <algorithm>

namespace vt {

/*************************************************************
 * applyZoneDefaults - Set zone appearance based on type & page
 * Matches v1 zone type defaults with page-aware fallbacks
 *************************************************************/
static void applyZoneDefaults(Zone* zone, ZoneType type, Page* page) {
    // Defaults: v1 ZoneDB fallback values
    ZoneFrame frame    = ZoneFrame::Default;
    uint8_t   texture  = TEXTURE_DEFAULT;
    uint8_t   color    = COLOR_DEFAULT;
    FontId    font     = FontId::Default;
    ZoneBehavior behave = ZoneBehavior::Blink;
    int w = 140, h = 100;

    switch (type) {
        case ZoneType::Simple:
        case ZoneType::Standard:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            break;
        case ZoneType::Toggle:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::GreenTexture);
            break;
        case ZoneType::Conditional:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            break;
        case ZoneType::Comment:
            frame = ZoneFrame::None;
            texture = TEXTURE_CLEAR;
            color = static_cast<uint8_t>(TextColor::Gray);
            behave = ZoneBehavior::None;
            w = 200; h = 40;
            break;
        case ZoneType::Switch:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::GrayParchment);
            break;
        case ZoneType::Item:
        case ZoneType::ItemNormal:
        case ZoneType::ItemModifier:
        case ZoneType::ItemMethod:
        case ZoneType::ItemSubstitute:
        case ZoneType::ItemPound:
        case ZoneType::ItemAdmission:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::GreenTexture);
            font = FontId::Times20;
            break;
        case ZoneType::Qualifier:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::GreenMarble);
            font = FontId::Times20;
            break;
        case ZoneType::Tender:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::DarkWood);
            font = FontId::Times24B;
            break;
        case ZoneType::TenderSet:
        case ZoneType::PaymentEntry:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::DarkWood);
            break;
        case ZoneType::Payout:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            break;
        case ZoneType::Table:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::GrayMarble);
            font = FontId::Times24B;
            w = 80; h = 80;
            break;
        case ZoneType::TableAssign:
        case ZoneType::CheckDisplay:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            break;
        case ZoneType::CheckList:
        case ZoneType::CheckEdit:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            behave = ZoneBehavior::None;
            w = 400; h = 500;
            break;
        case ZoneType::SplitCheck:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::GreenMarble);
            break;
        case ZoneType::Login:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            font = FontId::Times34B;
            behave = ZoneBehavior::None;
            w = 300; h = 200;
            break;
        case ZoneType::Logout:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            font = FontId::Times24B;
            break;
        case ZoneType::UserEdit:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            behave = ZoneBehavior::None;
            w = 400; h = 500;
            break;
        case ZoneType::GuestCount:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::GrayMarble);
            font = FontId::Times34B;
            w = 80; h = 80;
            break;
        case ZoneType::OrderEntry:
        case ZoneType::OrderDisplay:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::Parchment);
            color = static_cast<uint8_t>(TextColor::Black);
            behave = ZoneBehavior::None;
            w = 300; h = 500;
            break;
        case ZoneType::OrderPage:
        case ZoneType::OrderFlow:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            break;
        case ZoneType::OrderAdd:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::GreenTexture);
            break;
        case ZoneType::OrderDelete:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            break;
        case ZoneType::OrderComment:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::OrangeTexture);
            break;
        case ZoneType::Settings:
        case ZoneType::TaxSettings:
        case ZoneType::TaxSet:
        case ZoneType::MoneySet:
        case ZoneType::TimeSettings:
        case ZoneType::CCSettings:
        case ZoneType::CCMsgSettings:
        case ZoneType::ReceiptSet:
        case ZoneType::Receipts:
        case ZoneType::CalculationSettings:
        case ZoneType::JobSecurity:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::GrayParchment);
            behave = ZoneBehavior::None;
            w = 400; h = 500;
            break;
        case ZoneType::Developer:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            behave = ZoneBehavior::None;
            w = 400; h = 500;
            break;
        case ZoneType::Hardware:
        case ZoneType::PrintTarget:
        case ZoneType::ItemTarget:
        case ZoneType::VideoTarget:
        case ZoneType::SplitKitchen:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::GrayParchment);
            behave = ZoneBehavior::None;
            w = 400; h = 500;
            break;
        case ZoneType::CDU:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::Black);
            color = static_cast<uint8_t>(TextColor::Green);
            behave = ZoneBehavior::None;
            w = 300; h = 100;
            break;
        case ZoneType::DrawerManage:
        case ZoneType::DrawerAssign:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::DarkWood);
            break;
        case ZoneType::Report:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::Parchment);
            color = static_cast<uint8_t>(TextColor::Black);
            behave = ZoneBehavior::None;
            w = 500; h = 600;
            break;
        case ZoneType::Chart:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::WhiteTexture);
            color = static_cast<uint8_t>(TextColor::Black);
            behave = ZoneBehavior::None;
            w = 500; h = 400;
            break;
        case ZoneType::Search:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            break;
        case ZoneType::Read:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::Parchment);
            color = static_cast<uint8_t>(TextColor::Black);
            behave = ZoneBehavior::None;
            w = 400; h = 400;
            break;
        case ZoneType::Inventory:
        case ZoneType::Recipe:
        case ZoneType::Vendor:
        case ZoneType::ItemList:
        case ZoneType::Invoice:
        case ZoneType::Account:
        case ZoneType::RevenueGroups:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::TanParchment);
            color = static_cast<uint8_t>(TextColor::Black);
            behave = ZoneBehavior::None;
            w = 400; h = 500;
            break;
        case ZoneType::Expense:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            break;
        case ZoneType::Schedule:
        case ZoneType::Labor:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::TanParchment);
            color = static_cast<uint8_t>(TextColor::Black);
            behave = ZoneBehavior::None;
            w = 400; h = 500;
            break;
        case ZoneType::EndDay:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            font = FontId::Times24B;
            break;
        case ZoneType::CustomerInfo:
        case ZoneType::CreditCardList:
        case ZoneType::Merchant:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::TanParchment);
            color = static_cast<uint8_t>(TextColor::Black);
            behave = ZoneBehavior::None;
            w = 400; h = 500;
            break;
        case ZoneType::Command:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            break;
        case ZoneType::Phrase:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::TanParchment);
            behave = ZoneBehavior::None;
            w = 400; h = 500;
            break;
        case ZoneType::License:
        case ZoneType::ExpireMsg:
            frame = ZoneFrame::DoubleBorder;
            texture = static_cast<uint8_t>(TextureId::GrayParchment);
            behave = ZoneBehavior::None;
            w = 400; h = 300;
            break;
        case ZoneType::KillSystem:
        case ZoneType::ClearSystem:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::Lava);
            font = FontId::Times24B;
            break;
        case ZoneType::StatusButton:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            behave = ZoneBehavior::None;
            w = 200; h = 40;
            break;
        case ZoneType::ImageButton:
            frame = ZoneFrame::None;
            texture = TEXTURE_CLEAR;
            w = 200; h = 200;
            break;
        case ZoneType::IndexTab:
        case ZoneType::LanguageButton:
            frame = ZoneFrame::Border;
            texture = static_cast<uint8_t>(TextureId::BlueParchment);
            break;
        default:
            break;
    }

    zone->setRegion(Region{100, 100, w, h});
    zone->setBehavior(behave);
    zone->setFont(font);

    // Normal state
    ZoneState st0;
    st0.frame = frame;
    st0.texture = texture;
    st0.color = color;
    zone->setState(0, st0);

    // Selected state — highlighted
    ZoneState st1;
    st1.frame = frame;
    st1.texture = static_cast<uint8_t>(TextureId::LitSand);
    st1.color = color;
    zone->setState(1, st1);

    // Alternate state — same as normal
    ZoneState st2;
    st2.frame = frame;
    st2.texture = texture;
    st2.color = color;
    zone->setState(2, st2);
}

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
    editMenu->addAction(tr("&Delete Selected"), this, &MainWindow::onDeleteSelectedRequested, QKeySequence::Delete);
    editMenu->addSeparator();
    editMenu->addAction(tr("Page &Properties..."), [this]() {
        if (terminal_ && terminal_->currentPage()) {
            onPagePropertiesRequested(terminal_->currentPage());
        }
    });
    
    // View menu
    auto* viewMenu = menuBar->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Fullscreen"), this, &MainWindow::toggleFullscreen, QKeySequence("F11"));
    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Go to Page..."), this, &MainWindow::onGoToPageRequested, QKeySequence("Ctrl+G"));
    
    // Page menu (for quick navigation to system pages)
    auto* pageMenu = menuBar->addMenu(tr("&Pages"));
    pageMenu->addAction(tr("Login Page (-1)"), [this]() { if (terminal_) terminal_->jumpToPage(-1); if (terminalWidget_) terminalWidget_->update(); });
    pageMenu->addAction(tr("Tables Page (-3)"), [this]() { if (terminal_) terminal_->jumpToPage(-3); if (terminalWidget_) terminalWidget_->update(); });
    pageMenu->addAction(tr("Manager Page (-10)"), [this]() { if (terminal_) terminal_->jumpToPage(-10); if (terminalWidget_) terminalWidget_->update(); });
    pageMenu->addSeparator();
    pageMenu->addAction(tr("Home Page (1)"), [this]() { if (terminal_) terminal_->jumpToPage(1); if (terminalWidget_) terminalWidget_->update(); });
    
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
    // Start with 0 pages — pages are added via edit mode or loading saved UI
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
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            // Delete selected zones
            if (editMode_ && editMode_->isActive() && terminal_ && terminal_->currentPage()) {
                Page* page = terminal_->currentPage();
                auto selected = editMode_->selectedZones();
                for (Zone* zone : selected) {
                    editMode_->deleteZone(zone, page);
                }
                if (terminalWidget_) {
                    terminalWidget_->update();
                }
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
    
    Page* page = terminal_ ? terminal_->currentPage() : nullptr;
    ZonePropertiesDialog dlg(zone, page, this);
    if (dlg.exec() == QDialog::Accepted) {
        dlg.applyChanges();
        
        // If zone was replaced (type changed), update selection
        if (dlg.wasZoneReplaced() && editMode_ && dlg.replacementZone()) {
            editMode_->clearSelection();
            editMode_->selectZone(dlg.replacementZone());
        }
        
        if (terminalWidget_) {
            terminalWidget_->update();
        }
    }
}

void MainWindow::onPagePropertiesRequested(Page* page) {
    if (!page) return;
    
    PagePropertiesDialog dlg(page, this);
    if (dlg.exec() == QDialog::Accepted) {
        if (dlg.deleteRequested()) {
            // Delete the page (like original ViewTouch KillPage)
            int pageId = page->id();
            ZoneDB* db = control_->zoneDb();
            
            // Find another page to jump to
            auto ids = db->pageIds();
            int jumpId = 0;
            for (int id : ids) {
                if (id != pageId) {
                    jumpId = id;
                    break;
                }
            }
            
            db->removePage(pageId);
            
            if (terminal_ && jumpId != 0) {
                terminal_->jumpToPage(jumpId, JumpType::Stealth);
            }
        } else {
            dlg.applyChanges();
        }
        if (terminalWidget_) {
            terminalWidget_->update();
        }
    }
}

void MainWindow::onNewZoneRequested() {
    if (!terminal_ || !terminal_->currentPage()) return;
    
    Page* page = terminal_->currentPage();
    
    // Pick default zone type based on page type (matching v1 behavior)
    ZoneType defaultZoneType = ZoneType::Simple;
    switch (page->type()) {
        case PageType::Item:
        case PageType::Item2:
            defaultZoneType = ZoneType::ItemNormal;
            break;
        case PageType::Table:
        case PageType::Table2:
            defaultZoneType = ZoneType::Table;
            break;
        case PageType::Index:
        case PageType::IndexTabs:
            defaultZoneType = ZoneType::Simple;
            break;
        case PageType::Scripted:
        case PageType::Scripted2:
        case PageType::Scripted3:
        case PageType::ModifierKB:
            defaultZoneType = ZoneType::ItemModifier;
            break;
        case PageType::Checks:
            defaultZoneType = ZoneType::CheckList;
            break;
        case PageType::KitchenVid:
        case PageType::KitchenVid2:
            defaultZoneType = ZoneType::OrderDisplay;
            break;
        case PageType::Bar1:
        case PageType::Bar2:
            defaultZoneType = ZoneType::Simple;
            break;
        default:
            defaultZoneType = ZoneType::Simple;
            break;
    }
    
    // Create zone with type-appropriate defaults
    auto zone = std::make_unique<ButtonZone>();
    zone->setZoneType(defaultZoneType);
    zone->setName(QString());
    zone->setBehavior(ZoneBehavior::Blink);
    zone->setFont(FontId::Default);
    zone->setShape(ZoneShape::Rectangle);
    zone->setShadow(256);  // SHADOW_DEFAULT
    zone->setKey(0);
    zone->setPage(page);
    
    // Apply type-specific appearance defaults
    applyZoneDefaults(zone.get(), defaultZoneType, page);
    
    static_cast<ButtonZone*>(zone.get())->setLabel(QString());
    
    Zone* rawPtr = zone.get();
    page->addZone(std::move(zone));
    
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
    
    // Find next available page ID (positive IDs for user pages)
    int nextId = 1;
    while (db->page(nextId)) {
        nextId++;
    }
    
    // Default type: Item for normal editing (like original ViewTouch)
    PageType defaultType = PageType::Item;
    
    // Show page properties dialog for configuration (like original ViewTouch EditPage)
    PagePropertiesDialog dlg(nextId, defaultType, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;  // User cancelled
    }
    
    // Take ownership of the new page
    auto page = dlg.takeNewPage();
    if (!page) return;
    
    int pageId = page->id();
    
    // Validate: ID must not be 0
    if (pageId == 0) {
        QMessageBox::warning(this, tr("Invalid Page"),
            tr("Page ID cannot be 0."));
        return;
    }
    
    // Validate: ID must not already exist
    if (db->page(pageId)) {
        QMessageBox::warning(this, tr("Duplicate Page"),
            tr("A page with ID %1 already exists.").arg(pageId));
        return;
    }
    
    db->addPage(std::move(page));
    
    if (terminal_) {
        terminal_->jumpToPage(pageId);
    }
    
    if (terminalWidget_) {
        terminalWidget_->update();
    }
}

void MainWindow::onSaveRequested() {
    if (!control_) {
        QMessageBox::warning(this, tr("Error"), tr("No control object available."));
        return;
    }
    
    // Save to the default Ui file in the data directory
    if (control_->saveUi()) {
        statusBar()->showMessage(tr("UI saved successfully"), 3000);
    } else {
        QMessageBox::warning(this, tr("Save Error"),
            tr("Failed to save UI data."));
    }
}

void MainWindow::onLoadRequested() {
    if (!control_) {
        QMessageBox::warning(this, tr("Error"), tr("No control object available."));
        return;
    }
    
    // Load from the default Ui file
    if (control_->loadUi()) {
        // Refresh the display
        if (terminalWidget_) {
            terminalWidget_->update();
        }
        statusBar()->showMessage(tr("UI loaded successfully"), 3000);
    } else {
        QMessageBox::information(this, tr("No UI Data"),
            tr("No saved UI data found. Use File → Save to save current UI first."));
    }
}

void MainWindow::onGoToPageRequested() {
    bool ok;
    int pageId = QInputDialog::getInt(this, tr("Go to Page"),
        tr("Enter page ID (negative for system pages):"),
        terminal_ && terminal_->currentPage() ? terminal_->currentPage()->id() : 1,
        -9999, 9999, 1, &ok);
    
    if (ok) {
        if (!control_ || !control_->zoneDb()) return;
        
        Page* page = control_->zoneDb()->page(pageId);
        if (page) {
            if (terminal_) {
                terminal_->jumpToPage(pageId);
            }
            if (terminalWidget_) {
                terminalWidget_->update();
            }
        } else {
            QMessageBox::warning(this, tr("Page Not Found"),
                tr("No page exists with ID %1").arg(pageId));
        }
    }
}

void MainWindow::onDeleteSelectedRequested() {
    if (!editMode_ || !editMode_->isActive()) return;
    if (!terminal_ || !terminal_->currentPage()) return;
    
    Page* page = terminal_->currentPage();
    auto selected = editMode_->selectedZones();
    
    if (selected.empty()) {
        statusBar()->showMessage(tr("No zones selected"), 2000);
        return;
    }
    
    for (Zone* zone : selected) {
        editMode_->deleteZone(zone, page);
    }
    
    if (terminalWidget_) {
        terminalWidget_->update();
    }
    
    statusBar()->showMessage(tr("Deleted %1 zone(s)").arg(selected.size()), 2000);
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
        MainWindow* mw = qobject_cast<MainWindow*>(window());
        if (mw) terminal_ = mw->terminal();
    }
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
        MainWindow* mw = qobject_cast<MainWindow*>(window());
        if (mw) terminal_ = mw->terminal();
    }
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
        MainWindow* mw = qobject_cast<MainWindow*>(window());
        if (mw) terminal_ = mw->terminal();
    }
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
    if (!terminal_) {
        MainWindow* mw = qobject_cast<MainWindow*>(window());
        if (mw) terminal_ = mw->terminal();
    }
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
