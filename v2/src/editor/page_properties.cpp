/*
 * ViewTouch V2 - Page Properties Dialog Implementation
 * Matches original ViewTouch EditPage/ReadPage flow
 */

#include "editor/page_properties.hpp"
#include "editor/zone_properties.hpp"
#include "zone/page.hpp"
#include "core/colors.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>

namespace vt {

// --- SHADOW_DEFAULT sentinel ---
constexpr int SHADOW_DEFAULT = 256;

PagePropertiesDialog::PagePropertiesDialog(Page* page, QWidget* parent)
    : QDialog(parent)
    , page_(page)
    , isNewPage_(false)
{
    setWindowTitle(tr("Page Properties"));
    setMinimumSize(500, 600);
    
    setupUi();
    loadFromPage();
}

PagePropertiesDialog::PagePropertiesDialog(int suggestedId, PageType defaultType, QWidget* parent)
    : QDialog(parent)
    , isNewPage_(true)
{
    setWindowTitle(tr("New Page"));
    setMinimumSize(500, 600);
    
    // Create the new page object
    newPage_ = std::make_unique<Page>();
    page_ = newPage_.get();
    
    setupUi();
    initDefaults(suggestedId, defaultType);
}

PagePropertiesDialog::~PagePropertiesDialog() = default;

void PagePropertiesDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainTabWidget_ = new QTabWidget();
    mainLayout->addWidget(mainTabWidget_);
    
    // ===== General Tab =====
    auto* generalWidget = new QWidget();
    auto* generalLayout = new QFormLayout(generalWidget);
    
    idSpinBox_ = new QSpinBox();
    idSpinBox_->setRange(-9999, 9999);
    generalLayout->addRow(tr("Page ID:"), idSpinBox_);
    
    nameEdit_ = new QLineEdit();
    generalLayout->addRow(tr("Name:"), nameEdit_);
    
    typeCombo_ = new PageTypeComboBox();
    generalLayout->addRow(tr("Type:"), typeCombo_);
    connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PagePropertiesDialog::onPageTypeChanged);
    
    // Size preset (matching v1 SIZE_* constants)
    sizePresetCombo_ = new SizePresetComboBox();
    generalLayout->addRow(tr("Size Preset:"), sizePresetCombo_);
    connect(sizePresetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PagePropertiesDialog::onSizePresetChanged);
    
    // Custom size
    auto* sizeLayout = new QHBoxLayout();
    widthSpinBox_ = new QSpinBox();
    widthSpinBox_->setRange(100, 9999);
    sizeLayout->addWidget(new QLabel(tr("W:")));
    sizeLayout->addWidget(widthSpinBox_);
    heightSpinBox_ = new QSpinBox();
    heightSpinBox_->setRange(100, 9999);
    sizeLayout->addWidget(new QLabel(tr("H:")));
    sizeLayout->addWidget(heightSpinBox_);
    generalLayout->addRow(tr("Custom Size:"), sizeLayout);
    
    parentIdSpinBox_ = new QSpinBox();
    parentIdSpinBox_->setRange(-9999, 9999);
    generalLayout->addRow(tr("Parent ID:"), parentIdSpinBox_);
    
    indexCombo_ = new IndexComboBox();
    generalLayout->addRow(tr("Meal Period:"), indexCombo_);
    
    mainTabWidget_->addTab(generalWidget, tr("General"));
    
    // ===== Appearance Tab =====
    auto* appearWidget = new QWidget();
    auto* appearLayout = new QVBoxLayout(appearWidget);
    
    // Page background
    auto* bgGroup = new QGroupBox(tr("Page Background"));
    auto* bgLayout = new QFormLayout(bgGroup);
    
    bgTextureCombo_ = new TextureComboBox();
    bgLayout->addRow(tr("Background Texture:"), bgTextureCombo_);
    
    titleColorCombo_ = new ColorComboBox();
    bgLayout->addRow(tr("Title Color:"), titleColorCombo_);
    
    appearLayout->addWidget(bgGroup);
    
    mainTabWidget_->addTab(appearWidget, tr("Appearance"));
    
    // ===== Zone Defaults Tab =====
    auto* defaultsWidget = new QWidget();
    auto* defaultsLayout = new QVBoxLayout(defaultsWidget);
    
    // Per-state defaults (Normal / Selected / Alternate)
    auto* stateTabWidget = new QTabWidget();
    const char* stateNames[] = { "Normal", "Selected", "Alternate" };
    for (int i = 0; i < 3; ++i) {
        auto* stateWidget = new QWidget();
        auto* stateLayout = new QFormLayout(stateWidget);
        
        stateDefaults_[i].frameCombo = new FrameComboBox();
        stateLayout->addRow(tr("Frame:"), stateDefaults_[i].frameCombo);
        
        stateDefaults_[i].textureCombo = new TextureComboBox();
        stateLayout->addRow(tr("Texture:"), stateDefaults_[i].textureCombo);
        
        stateDefaults_[i].colorCombo = new ColorComboBox();
        stateLayout->addRow(tr("Color:"), stateDefaults_[i].colorCombo);
        
        stateTabWidget->addTab(stateWidget, tr(stateNames[i]));
    }
    defaultsLayout->addWidget(stateTabWidget);
    
    // Font, spacing, shadow
    auto* otherDefaultsLayout = new QFormLayout();
    
    defaultFontCombo_ = new FontComboBox();
    otherDefaultsLayout->addRow(tr("Default Font:"), defaultFontCombo_);
    
    defaultSpacingSpinBox_ = new QSpinBox();
    defaultSpacingSpinBox_->setRange(0, 100);
    otherDefaultsLayout->addRow(tr("Default Spacing:"), defaultSpacingSpinBox_);
    
    defaultShadowCombo_ = new ShadowComboBox(false);  // Page shadow has no "Default" option
    otherDefaultsLayout->addRow(tr("Default Shadow:"), defaultShadowCombo_);
    
    defaultsLayout->addLayout(otherDefaultsLayout);
    
    mainTabWidget_->addTab(defaultsWidget, tr("Zone Defaults"));
    
    // ===== Buttons =====
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    
    if (!isNewPage_) {
        buttonBox->addButton(QDialogButtonBox::Apply);
        connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
                this, &PagePropertiesDialog::onApply);
        
        auto* deleteBtn = buttonBox->addButton(tr("Delete"), QDialogButtonBox::DestructiveRole);
        connect(deleteBtn, &QPushButton::clicked, this, &PagePropertiesDialog::onDelete);
    }
    
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PagePropertiesDialog::onOk);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void PagePropertiesDialog::loadFromPage() {
    if (!page_) return;
    
    idSpinBox_->setValue(page_->id());
    nameEdit_->setText(page_->name());
    static_cast<PageTypeComboBox*>(typeCombo_)->setCurrentPageType(page_->type());
    
    sizePresetCombo_->setFromSize(page_->width(), page_->height());
    widthSpinBox_->setValue(page_->width());
    heightSpinBox_->setValue(page_->height());
    
    parentIdSpinBox_->setValue(page_->parentId());
    indexCombo_->setCurrentIndex2(page_->index());
    
    // Appearance
    bgTextureCombo_->setCurrentTextureId(page_->defaultTexture(0));  // Use state 0 texture as background
    titleColorCombo_->setCurrentColorId(page_->titleColor());
    
    // Per-state zone defaults
    for (int i = 0; i < 3; ++i) {
        stateDefaults_[i].frameCombo->setCurrentFrame(page_->defaultFrame(i));
        stateDefaults_[i].textureCombo->setCurrentTextureId(page_->defaultTexture(i));
        stateDefaults_[i].colorCombo->setCurrentColorId(page_->defaultColor(i));
    }
    
    defaultFontCombo_->setCurrentFontId(page_->defaultFont());
    defaultSpacingSpinBox_->setValue(page_->defaultSpacing());
    defaultShadowCombo_->setCurrentShadow(page_->defaultShadow());
}

void PagePropertiesDialog::saveToPage() {
    if (!page_) return;
    
    page_->setId(idSpinBox_->value());
    page_->setName(nameEdit_->text());
    page_->setType(static_cast<PageTypeComboBox*>(typeCombo_)->currentPageType());
    page_->setSize(widthSpinBox_->value(), heightSpinBox_->value());
    page_->setParentId(parentIdSpinBox_->value());
    page_->setIndex(indexCombo_->currentIndex2());
    
    // Appearance
    page_->setDefaultTexture(bgTextureCombo_->currentTextureId());
    page_->setTitleColor(titleColorCombo_->currentColorId());
    
    // Per-state zone defaults
    for (int i = 0; i < 3; ++i) {
        page_->setDefaultFrame(i, stateDefaults_[i].frameCombo->currentFrame());
        page_->setDefaultTexture(i, stateDefaults_[i].textureCombo->currentTextureId());
        page_->setDefaultColor(i, stateDefaults_[i].colorCombo->currentColorId());
    }
    
    page_->setDefaultFont(defaultFontCombo_->currentFontId());
    page_->setDefaultSpacing(defaultSpacingSpinBox_->value());
    page_->setDefaultShadow(defaultShadowCombo_->currentShadow());
}

void PagePropertiesDialog::applyChanges() {
    saveToPage();
}

void PagePropertiesDialog::onApply() {
    applyChanges();
}

void PagePropertiesDialog::onOk() {
    applyChanges();
    accept();
}

void PagePropertiesDialog::onDelete() {
    deleteRequested_ = true;
    accept();
}

void PagePropertiesDialog::onPageTypeChanged(int /*index*/) {
    if (!isNewPage_) return;  // Only auto-assign parent for new pages
    
    PageType type = static_cast<PageTypeComboBox*>(typeCombo_)->currentPageType();
    autoAssignParent(type);
}

void PagePropertiesDialog::onSizePresetChanged(int /*index*/) {
    int w = sizePresetCombo_->selectedWidth();
    int h = sizePresetCombo_->selectedHeight();
    if (w > 0 && h > 0) {
        widthSpinBox_->setValue(w);
        heightSpinBox_->setValue(h);
    }
}

void PagePropertiesDialog::autoAssignParent(PageType type) {
    // Matches original ViewTouch Page::Init() parent_id assignment
    switch (type) {
        case PageType::Index:      parentIdSpinBox_->setValue(-99); break;
        case PageType::IndexTabs:  parentIdSpinBox_->setValue(-94); break;
        case PageType::Item:       parentIdSpinBox_->setValue(-98); break;
        case PageType::Item2:      parentIdSpinBox_->setValue(-98); break;
        case PageType::Scripted:   parentIdSpinBox_->setValue(-98); break;
        case PageType::Scripted2:  parentIdSpinBox_->setValue(-99); break;
        case PageType::Scripted3:  parentIdSpinBox_->setValue(-97); break;
        case PageType::Table:      parentIdSpinBox_->setValue(-3);  break;  // PAGEID_TABLE
        case PageType::Table2:     parentIdSpinBox_->setValue(-4);  break;  // PAGEID_TABLE2
        case PageType::Library:    parentIdSpinBox_->setValue(0);   break;
        case PageType::ModifierKB: parentIdSpinBox_->setValue(-96); break;
        default:
            // System, Template, Checks, KitchenVid, etc. — keep current value
            break;
    }
}

void PagePropertiesDialog::initDefaults(int suggestedId, PageType defaultType) {
    // Match original ViewTouch EditPage(nullptr) defaults
    idSpinBox_->setValue(suggestedId);
    nameEdit_->setText(QString());
    static_cast<PageTypeComboBox*>(typeCombo_)->setCurrentPageType(defaultType);
    
    sizePresetCombo_->setFromSize(1024, 768);  // SIZE_1024x768
    widthSpinBox_->setValue(1024);
    heightSpinBox_->setValue(768);
    
    parentIdSpinBox_->setValue(0);
    indexCombo_->setCurrentIndex2(0);  // INDEX_GENERAL
    
    // Background
    bgTextureCombo_->setCurrentTextureId(TEXTURE_DEFAULT);
    titleColorCombo_->setCurrentColorId(COLOR_PAGE_DEFAULT);
    
    // Per-state zone defaults (matching v1 EditPage defaults)
    for (int i = 0; i < 3; ++i) {
        stateDefaults_[i].frameCombo->setCurrentFrame(ZoneFrame::Default);
        stateDefaults_[i].textureCombo->setCurrentTextureId(TEXTURE_DEFAULT);
        stateDefaults_[i].colorCombo->setCurrentColorId(COLOR_PAGE_DEFAULT);
    }
    
    defaultFontCombo_->setCurrentFontId(FontId::Default);
    defaultSpacingSpinBox_->setValue(0);
    defaultShadowCombo_->setCurrentShadow(SHADOW_DEFAULT);
    
    // Auto-assign parent based on initial type
    autoAssignParent(defaultType);
    
    // Apply defaults to the new page object
    saveToPage();
}

std::unique_ptr<Page> PagePropertiesDialog::takeNewPage() {
    return std::move(newPage_);
}

/*************************************************************
 * PageTypeComboBox Implementation
 *************************************************************/
PageTypeComboBox::PageTypeComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("System"), static_cast<int>(PageType::System));
    addItem(tr("Table"), static_cast<int>(PageType::Table));
    addItem(tr("Table 2"), static_cast<int>(PageType::Table2));
    addItem(tr("Index"), static_cast<int>(PageType::Index));
    addItem(tr("Index Tabs"), static_cast<int>(PageType::IndexTabs));
    addItem(tr("Item"), static_cast<int>(PageType::Item));
    addItem(tr("Item 2"), static_cast<int>(PageType::Item2));
    addItem(tr("Library"), static_cast<int>(PageType::Library));
    addItem(tr("Template"), static_cast<int>(PageType::Template));
    addItem(tr("Scripted"), static_cast<int>(PageType::Scripted));
    addItem(tr("Scripted 2"), static_cast<int>(PageType::Scripted2));
    addItem(tr("Scripted 3"), static_cast<int>(PageType::Scripted3));
    addItem(tr("Checks"), static_cast<int>(PageType::Checks));
    addItem(tr("Kitchen Video"), static_cast<int>(PageType::KitchenVid));
    addItem(tr("Kitchen Video 2"), static_cast<int>(PageType::KitchenVid2));
    addItem(tr("Bar 1"), static_cast<int>(PageType::Bar1));
    addItem(tr("Bar 2"), static_cast<int>(PageType::Bar2));
    addItem(tr("Modifier Keyboard"), static_cast<int>(PageType::ModifierKB));
}

void PageTypeComboBox::setCurrentPageType(PageType type) {
    int index = findData(static_cast<int>(type));
    if (index >= 0) setCurrentIndex(index);
}

PageType PageTypeComboBox::currentPageType() const {
    return static_cast<PageType>(currentData().toInt());
}

/*************************************************************
 * SizePresetComboBox Implementation
 * Matches original ViewTouch SIZE_* constants
 *************************************************************/
SizePresetComboBox::SizePresetComboBox(QWidget* parent)
    : QComboBox(parent)
{
    // value encodes (width << 16) | height
    addItem(tr("640 x 480"),     (640  << 16) | 480);
    addItem(tr("768 x 1024"),    (768  << 16) | 1024);
    addItem(tr("800 x 480"),     (800  << 16) | 480);
    addItem(tr("800 x 600"),     (800  << 16) | 600);
    addItem(tr("1024 x 600"),    (1024 << 16) | 600);
    addItem(tr("1024 x 768"),    (1024 << 16) | 768);
    addItem(tr("1280 x 800"),    (1280 << 16) | 800);
    addItem(tr("1280 x 1024"),   (1280 << 16) | 1024);
    addItem(tr("1366 x 768"),    (1366 << 16) | 768);
    addItem(tr("1440 x 900"),    (1440 << 16) | 900);
    addItem(tr("1600 x 900"),    (1600 << 16) | 900);
    addItem(tr("1600 x 1200"),   (1600 << 16) | 1200);
    addItem(tr("1680 x 1050"),   (1680 << 16) | 1050);
    addItem(tr("1920 x 1080"),   (1920 << 16) | 1080);
    addItem(tr("1920 x 1200"),   (1920 << 16) | 1200);
    addItem(tr("2560 x 1440"),   (2560 << 16) | 1440);
    addItem(tr("2560 x 1600"),   (2560 << 16) | 1600);
    addItem(tr("Custom"),        0);
}

void SizePresetComboBox::setFromSize(int w, int h) {
    int packed = (w << 16) | h;
    int idx = findData(packed);
    if (idx >= 0) {
        setCurrentIndex(idx);
    } else {
        // Select "Custom"
        int customIdx = findData(0);
        if (customIdx >= 0) setCurrentIndex(customIdx);
    }
}

int SizePresetComboBox::selectedWidth() const {
    int packed = currentData().toInt();
    if (packed == 0) return -1;  // Custom
    return (packed >> 16) & 0xFFFF;
}

int SizePresetComboBox::selectedHeight() const {
    int packed = currentData().toInt();
    if (packed == 0) return -1;  // Custom
    return packed & 0xFFFF;
}

/*************************************************************
 * IndexComboBox Implementation
 * Matches original ViewTouch INDEX_* constants
 *************************************************************/
IndexComboBox::IndexComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("General (All Day)"), 0);    // INDEX_GENERAL
    addItem(tr("Breakfast"),         1);    // INDEX_BREAKFAST
    addItem(tr("Brunch"),            2);    // INDEX_BRUNCH
    addItem(tr("Lunch"),             3);    // INDEX_LUNCH
    addItem(tr("Early Dinner"),      4);    // INDEX_EARLY_DINNER
    addItem(tr("Dinner"),            5);    // INDEX_DINNER
    addItem(tr("Late Night"),        6);    // INDEX_LATE_NIGHT
    addItem(tr("Bar"),               7);    // INDEX_BAR
    addItem(tr("Wine"),              8);    // INDEX_WINE
    addItem(tr("Cafe"),              9);    // INDEX_CAFE
    addItem(tr("Room"),              10);   // INDEX_ROOM
    addItem(tr("Retail"),            11);   // INDEX_RETAIL
}

void IndexComboBox::setCurrentIndex2(int index) {
    int idx = findData(index);
    if (idx >= 0) setCurrentIndex(idx);
}

int IndexComboBox::currentIndex2() const {
    return currentData().toInt();
}

/*************************************************************
 * ShadowComboBox Implementation
 * Matches original ViewTouch ShadowName/ShadowValue arrays
 *************************************************************/
ShadowComboBox::ShadowComboBox(bool includeDefault, QWidget* parent)
    : QComboBox(parent)
{
    if (includeDefault) {
        addItem(tr("Default"), SHADOW_DEFAULT);  // 256 — use page/system default
    }
    addItem(tr("No Shadow"), 0);
    addItem(tr("Minimal"),   4);
    addItem(tr("Normal"),    6);
    addItem(tr("Maximum"),   9);
}

void ShadowComboBox::setCurrentShadow(int value) {
    int idx = findData(value);
    if (idx >= 0) {
        setCurrentIndex(idx);
    } else {
        // Unknown value — select closest
        setCurrentIndex(0);
    }
}

int ShadowComboBox::currentShadow() const {
    return currentData().toInt();
}

} // namespace vt
