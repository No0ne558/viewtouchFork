/*
 * ViewTouch V2 - Button Properties Dialog Implementation
 * Matches original ViewTouch "Button Properties Dialog"
 */

#include "editor/zone_properties.hpp"
#include "editor/page_properties.hpp"
#include "zone/zone.hpp"
#include "zone/zone_types.hpp"
#include "zone/page.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QScrollArea>

namespace vt {

// Helper function to check if zone type is an item type
static bool IsItemZoneType(ZoneType t) {
    return t == ZoneType::Item || t == ZoneType::ItemNormal ||
           t == ZoneType::ItemModifier || t == ZoneType::ItemMethod ||
           t == ZoneType::ItemSubstitute || t == ZoneType::ItemPound ||
           t == ZoneType::ItemAdmission;
}

ZonePropertiesDialog::ZonePropertiesDialog(Zone* zone, Page* page, QWidget* parent)
    : QDialog(parent)
    , zone_(zone)
    , page_(page)
    , originalType_(zone ? zone->zoneType() : ZoneType::Undefined)
{
    setWindowTitle(tr("Button Properties"));
    setMinimumSize(500, 600);
    
    setupUi();
    loadFromZone();
    updateFieldVisibility();
}

ZonePropertiesDialog::~ZonePropertiesDialog() = default;

void ZonePropertiesDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    
    mainTabWidget_ = new QTabWidget(this);
    
    // ============ General Tab ============
    auto* generalTab = new QWidget();
    auto* generalLayout = new QFormLayout(generalTab);
    
    zoneTypeCombo_ = new ZoneTypeComboBox();
    generalLayout->addRow(tr("Button's Type:"), zoneTypeCombo_);
    
    nameEdit_ = new QLineEdit();
    generalLayout->addRow(tr("Button's Name:"), nameEdit_);
    
    pageEdit_ = new QLineEdit();
    generalLayout->addRow(tr("Page Location:"), pageEdit_);
    
    groupSpinBox_ = new QSpinBox();
    groupSpinBox_->setRange(0, 999);
    generalLayout->addRow(tr("Group ID:"), groupSpinBox_);
    
    // Position and size
    auto* posLayout = new QHBoxLayout();
    xSpinBox_ = new QSpinBox();
    xSpinBox_->setRange(0, 9999);
    posLayout->addWidget(new QLabel(tr("X:")));
    posLayout->addWidget(xSpinBox_);
    ySpinBox_ = new QSpinBox();
    ySpinBox_->setRange(0, 9999);
    posLayout->addWidget(new QLabel(tr("Y:")));
    posLayout->addWidget(ySpinBox_);
    generalLayout->addRow(tr("Position:"), posLayout);
    
    auto* sizeLayout = new QHBoxLayout();
    widthSpinBox_ = new QSpinBox();
    widthSpinBox_->setRange(10, 9999);
    sizeLayout->addWidget(new QLabel(tr("W:")));
    sizeLayout->addWidget(widthSpinBox_);
    heightSpinBox_ = new QSpinBox();
    heightSpinBox_->setRange(10, 9999);
    sizeLayout->addWidget(new QLabel(tr("H:")));
    sizeLayout->addWidget(heightSpinBox_);
    generalLayout->addRow(tr("Size:"), sizeLayout);
    
    // Connect zone type change to update defaults and visibility
    connect(zoneTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ZonePropertiesDialog::onZoneTypeChanged);
    
    mainTabWidget_->addTab(generalTab, tr("General"));
    
    // ============ Appearance Tab ============
    auto* appearanceTab = new QWidget();
    auto* appearanceLayout = new QVBoxLayout(appearanceTab);
    
    // Behavior - moved to top of appearance
    auto* behaveLayout = new QFormLayout();
    behaviorCombo_ = new BehaviorComboBox();
    behaveLayout->addRow(tr("Button's Behavior:"), behaviorCombo_);
    
    fontCombo_ = new FontComboBox();
    behaveLayout->addRow(tr("Button's Font:"), fontCombo_);
    
    shapeCombo_ = new ShapeComboBox();
    behaveLayout->addRow(tr("Button's Shape:"), shapeCombo_);
    
    shadowCombo_ = new ShadowComboBox(true);  // Include "Default" option for zones
    behaveLayout->addRow(tr("Shadow Intensity:"), shadowCombo_);
    
    keySpinBox_ = new QSpinBox();
    keySpinBox_->setRange(0, 255);
    keySpinBox_->setSpecialValueText(tr("None"));
    behaveLayout->addRow(tr("Keyboard Shortcut:"), keySpinBox_);
    
    appearanceLayout->addLayout(behaveLayout);
    
    // State tabs (Normal, Selected, Disabled)
    stateTabWidget_ = new QTabWidget();
    
    const QString stateNames[] = {
        tr("Normal"), tr("When Selected"), tr("When Disabled")
    };
    for (int i = 0; i < 3; ++i) {
        auto* stateWidget = new QWidget();
        auto* stateLayout = new QFormLayout(stateWidget);
        
        stateWidgets_[i].frameCombo = new FrameComboBox();
        stateLayout->addRow(tr("Edge:"), stateWidgets_[i].frameCombo);
        
        stateWidgets_[i].textureCombo = new TextureComboBox();
        stateLayout->addRow(tr("Texture:"), stateWidgets_[i].textureCombo);
        
        stateWidgets_[i].colorCombo = new ColorComboBox();
        stateLayout->addRow(tr("Text Color:"), stateWidgets_[i].colorCombo);
        
        connect(stateWidgets_[i].frameCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ZonePropertiesDialog::updatePreview);
        connect(stateWidgets_[i].textureCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ZonePropertiesDialog::updatePreview);
        connect(stateWidgets_[i].colorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ZonePropertiesDialog::updatePreview);
        
        stateTabWidget_->addTab(stateWidget, stateNames[i]);
    }
    
    appearanceLayout->addWidget(stateTabWidget_);
    
    mainTabWidget_->addTab(appearanceTab, tr("Appearance"));
    
    // ============ Actions Tab ============
    actionsTab_ = new QWidget();
    auto* actionsScroll = new QScrollArea();
    actionsScroll->setWidgetResizable(true);
    actionsScroll->setWidget(actionsTab_);
    
    actionsLayout_ = new QFormLayout(actionsTab_);
    
    // Confirmation (for ZONE_STANDARD)
    confirmLabel_ = new QLabel(tr("Confirmation:"));
    confirmCheck_ = new QCheckBox(tr("Ask for confirmation"));
    actionsLayout_->addRow(confirmLabel_, confirmCheck_);
    
    confirmMsgLabel_ = new QLabel(tr("Confirm Message:"));
    confirmMsgEdit_ = new QLineEdit();
    actionsLayout_->addRow(confirmMsgLabel_, confirmMsgEdit_);
    
    // Expression (for ZONE_CONDITIONAL)
    expressionLabel_ = new QLabel(tr("Expression:"));
    expressionEdit_ = new QLineEdit();
    expressionEdit_->setPlaceholderText(tr("Conditional expression"));
    actionsLayout_->addRow(expressionLabel_, expressionEdit_);
    
    // Message (for ZONE_STANDARD, ZONE_CONDITIONAL, ZONE_TOGGLE)
    messageLabel_ = new QLabel(tr("Message:"));
    messageEdit_ = new QLineEdit();
    messageEdit_->setPlaceholderText(tr("Message to broadcast"));
    actionsLayout_->addRow(messageLabel_, messageEdit_);
    
    // Jump (for buttons)
    jumpTypeLabel_ = new QLabel(tr("Jump Options:"));
    jumpTypeCombo_ = new JumpTypeComboBox();
    actionsLayout_->addRow(jumpTypeLabel_, jumpTypeCombo_);
    
    jumpIdLabel_ = new QLabel(tr("Jump To Page:"));
    jumpIdSpinBox_ = new QSpinBox();
    jumpIdSpinBox_->setRange(-100, 9999);
    actionsLayout_->addRow(jumpIdLabel_, jumpIdSpinBox_);
    
    connect(jumpTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ZonePropertiesDialog::onJumpTypeChanged);
    
    // Drawer zone type
    drawerZoneTypeLabel_ = new QLabel(tr("Drawer Button Type:"));
    drawerZoneTypeCombo_ = new QComboBox();
    drawerZoneTypeCombo_->addItem(tr("Pull Drawer"), 0);
    drawerZoneTypeCombo_->addItem(tr("Balance Drawer"), 1);
    drawerZoneTypeCombo_->addItem(tr("Adjust Drawer"), 2);
    actionsLayout_->addRow(drawerZoneTypeLabel_, drawerZoneTypeCombo_);
    
    // Filename (for ZONE_READ)
    filenameLabel_ = new QLabel(tr("File Name:"));
    filenameEdit_ = new QLineEdit();
    actionsLayout_->addRow(filenameLabel_, filenameEdit_);
    
    // Image filename
    imageFilenameLabel_ = new QLabel(tr("Image File:"));
    imageFilenameCombo_ = new QComboBox();
    imageFilenameCombo_->setEditable(true);
    actionsLayout_->addRow(imageFilenameLabel_, imageFilenameCombo_);
    
    // Tender (for ZONE_TENDER)
    tenderTypeLabel_ = new QLabel(tr("Tender Type:"));
    tenderTypeCombo_ = new TenderTypeComboBox();
    actionsLayout_->addRow(tenderTypeLabel_, tenderTypeCombo_);
    
    tenderAmountLabel_ = new QLabel(tr("Tender Amount:"));
    tenderAmountSpinBox_ = new QDoubleSpinBox();
    tenderAmountSpinBox_->setRange(0, 99999.99);
    tenderAmountSpinBox_->setDecimals(2);
    tenderAmountSpinBox_->setPrefix(tr("$"));
    actionsLayout_->addRow(tenderAmountLabel_, tenderAmountSpinBox_);
    
    // Report (for ZONE_REPORT)
    reportTypeLabel_ = new QLabel(tr("Report Type:"));
    reportTypeCombo_ = new ReportTypeComboBox();
    actionsLayout_->addRow(reportTypeLabel_, reportTypeCombo_);
    
    connect(reportTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ZonePropertiesDialog::onReportTypeChanged);
    
    checkDispLabel_ = new QLabel(tr("Check to Display:"));
    checkDispSpinBox_ = new QSpinBox();
    checkDispSpinBox_->setRange(0, 99);
    actionsLayout_->addRow(checkDispLabel_, checkDispSpinBox_);
    
    videoTargetLabel_ = new QLabel(tr("Video Target:"));
    videoTargetCombo_ = new PrinterComboBox();
    actionsLayout_->addRow(videoTargetLabel_, videoTargetCombo_);
    
    reportPrintLabel_ = new QLabel(tr("Touch Print:"));
    reportPrintCombo_ = new QComboBox();
    reportPrintCombo_->addItem(tr("Don't Print"), 0);
    reportPrintCombo_->addItem(tr("Print Report"), 1);
    reportPrintCombo_->addItem(tr("Print Order"), 2);
    actionsLayout_->addRow(reportPrintLabel_, reportPrintCombo_);
    
    // Spacing (for list zones)
    spacingLabel_ = new QLabel(tr("Line Spacing:"));
    spacingSpinBox_ = new QSpinBox();
    spacingSpinBox_->setRange(0, 100);
    actionsLayout_->addRow(spacingLabel_, spacingSpinBox_);
    
    // Qualifier (for ZONE_QUALIFIER)
    qualifierLabel_ = new QLabel(tr("Qualifier:"));
    qualifierCombo_ = new QualifierComboBox();
    actionsLayout_->addRow(qualifierLabel_, qualifierCombo_);
    
    // Amount (for ZONE_ORDER_PAGE)
    amountLabel_ = new QLabel(tr("Amount:"));
    amountSpinBox_ = new QSpinBox();
    amountSpinBox_->setRange(0, 999);
    actionsLayout_->addRow(amountLabel_, amountSpinBox_);
    
    // Switch type (for ZONE_SWITCH)
    switchTypeLabel_ = new QLabel(tr("Switch Type:"));
    switchTypeCombo_ = new SwitchTypeComboBox();
    actionsLayout_->addRow(switchTypeLabel_, switchTypeCombo_);
    
    // Customer type (for ZONE_TABLE)
    customerTypeLabel_ = new QLabel(tr("Customer Type:"));
    customerTypeCombo_ = new CustomerTypeComboBox();
    actionsLayout_->addRow(customerTypeLabel_, customerTypeCombo_);
    
    mainTabWidget_->addTab(actionsScroll, tr("Actions"));
    
    // ============ Item Tab ============
    itemTab_ = new QWidget();
    auto* itemScroll = new QScrollArea();
    itemScroll->setWidgetResizable(true);
    itemScroll->setWidget(itemTab_);
    
    itemLayout_ = new QFormLayout(itemTab_);
    
    // Item type selector (only for generic ZONE_ITEM)
    itemTypeLabel_ = new QLabel(tr("Menu Type:"));
    itemTypeCombo_ = new ItemTypeComboBox();
    itemLayout_->addRow(itemTypeLabel_, itemTypeCombo_);
    
    connect(itemTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ZonePropertiesDialog::onItemTypeChanged);
    
    // Item names
    itemNameLabel_ = new QLabel(tr("True Name:"));
    itemNameEdit_ = new QLineEdit();
    itemLayout_->addRow(itemNameLabel_, itemNameEdit_);
    
    itemZoneNameLabel_ = new QLabel(tr("On-Screen Name:"));
    itemZoneNameEdit_ = new QLineEdit();
    itemZoneNameEdit_->setPlaceholderText(tr("If different from True Name"));
    itemLayout_->addRow(itemZoneNameLabel_, itemZoneNameEdit_);
    
    itemPrintNameLabel_ = new QLabel(tr("Print Name:"));
    itemPrintNameEdit_ = new QLineEdit();
    itemPrintNameEdit_->setPlaceholderText(tr("Abbreviation for remote printing"));
    itemLayout_->addRow(itemPrintNameLabel_, itemPrintNameEdit_);
    
    // Pricing
    itemPriceLabel_ = new QLabel(tr("Selling Price:"));
    itemPriceSpinBox_ = new QDoubleSpinBox();
    itemPriceSpinBox_->setRange(0, 99999.99);
    itemPriceSpinBox_->setDecimals(2);
    itemPriceSpinBox_->setPrefix(tr("$"));
    itemLayout_->addRow(itemPriceLabel_, itemPriceSpinBox_);
    
    itemSubpriceLabel_ = new QLabel(tr("Substitute Price:"));
    itemSubpriceSpinBox_ = new QDoubleSpinBox();
    itemSubpriceSpinBox_->setRange(0, 99999.99);
    itemSubpriceSpinBox_->setDecimals(2);
    itemSubpriceSpinBox_->setPrefix(tr("$"));
    itemLayout_->addRow(itemSubpriceLabel_, itemSubpriceSpinBox_);
    
    itemEmployeePriceLabel_ = new QLabel(tr("Employee Price:"));
    itemEmployeePriceSpinBox_ = new QDoubleSpinBox();
    itemEmployeePriceSpinBox_->setRange(0, 99999.99);
    itemEmployeePriceSpinBox_->setDecimals(2);
    itemEmployeePriceSpinBox_->setPrefix(tr("$"));
    itemLayout_->addRow(itemEmployeePriceLabel_, itemEmployeePriceSpinBox_);
    
    // Categories
    itemFamilyLabel_ = new QLabel(tr("Family:"));
    itemFamilyCombo_ = new ItemFamilyComboBox();
    itemLayout_->addRow(itemFamilyLabel_, itemFamilyCombo_);
    
    itemSalesLabel_ = new QLabel(tr("Tax/Discount:"));
    itemSalesCombo_ = new SalesTypeComboBox();
    itemLayout_->addRow(itemSalesLabel_, itemSalesCombo_);
    
    // Printing
    itemPrinterLabel_ = new QLabel(tr("Printer Target:"));
    itemPrinterCombo_ = new PrinterComboBox();
    itemLayout_->addRow(itemPrinterLabel_, itemPrinterCombo_);
    
    itemCallOrderLabel_ = new QLabel(tr("Call Order:"));
    itemCallOrderCombo_ = new CallOrderComboBox();
    itemLayout_->addRow(itemCallOrderLabel_, itemCallOrderCombo_);
    
    // Modifier script
    pageListLabel_ = new QLabel(tr("Modifier Pages:"));
    pageListEdit_ = new QLineEdit();
    pageListEdit_->setPlaceholderText(tr("Comma-separated page numbers"));
    itemLayout_->addRow(pageListLabel_, pageListEdit_);
    
    // Admission-specific fields
    itemLocationLabel_ = new QLabel(tr("Event Location:"));
    itemLocationEdit_ = new QLineEdit();
    itemLayout_->addRow(itemLocationLabel_, itemLocationEdit_);
    
    itemEventTimeLabel_ = new QLabel(tr("Event Time:"));
    itemEventTimeEdit_ = new QLineEdit();
    itemLayout_->addRow(itemEventTimeLabel_, itemEventTimeEdit_);
    
    itemTotalTicketsLabel_ = new QLabel(tr("Total Seats:"));
    itemTotalTicketsSpinBox_ = new QSpinBox();
    itemTotalTicketsSpinBox_->setRange(0, 99999);
    itemLayout_->addRow(itemTotalTicketsLabel_, itemTotalTicketsSpinBox_);
    
    itemPriceLabelLabel_ = new QLabel(tr("Price Class:"));
    itemPriceLabelEdit_ = new QLineEdit();
    itemLayout_->addRow(itemPriceLabelLabel_, itemPriceLabelEdit_);
    
    mainTabWidget_->addTab(itemScroll, tr("Item"));
    
    // ============ Options Tab ============
    auto* optionsTab = new QWidget();
    auto* optionsLayout = new QFormLayout(optionsTab);
    
    activeCheck_ = new QCheckBox(tr("Button is Active"));
    activeCheck_->setChecked(true);
    optionsLayout->addRow(activeCheck_);
    
    editCheck_ = new QCheckBox(tr("Editable in Edit Mode"));
    optionsLayout->addRow(editCheck_);
    
    stayLitCheck_ = new QCheckBox(tr("Stay Lit After Touch"));
    optionsLayout->addRow(stayLitCheck_);
    
    mainTabWidget_->addTab(optionsTab, tr("Options"));
    
    mainLayout->addWidget(mainTabWidget_);
    
    // Buttons
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ZonePropertiesDialog::onOk);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &ZonePropertiesDialog::onApply);
    mainLayout->addWidget(buttonBox);
}

void ZonePropertiesDialog::setupGeneralTab() {}
void ZonePropertiesDialog::setupAppearanceTab() {}
void ZonePropertiesDialog::setupBehaviorTab() {}
void ZonePropertiesDialog::setupActionsTab() {}
void ZonePropertiesDialog::setupItemTab() {}

void ZonePropertiesDialog::loadFromZone() {
    if (!zone_) return;
    
    // General
    zoneTypeCombo_->setCurrentZoneType(zone_->zoneType());
    nameEdit_->setText(zone_->name());
    // pageEdit_->setText(...);  // TODO: page location
    groupSpinBox_->setValue(zone_->groupId());
    xSpinBox_->setValue(zone_->x());
    ySpinBox_->setValue(zone_->y());
    widthSpinBox_->setValue(zone_->w());
    heightSpinBox_->setValue(zone_->h());
    
    // Appearance - states
    for (int i = 0; i < 3; ++i) {
        const ZoneState& st = zone_->state(i);
        stateWidgets_[i].frameCombo->setCurrentFrame(st.frame);
        stateWidgets_[i].textureCombo->setCurrentTextureId(st.texture);
        stateWidgets_[i].colorCombo->setCurrentColorId(st.color);
    }
    
    behaviorCombo_->setCurrentBehavior(zone_->behavior());
    fontCombo_->setCurrentFontId(zone_->font());
    shapeCombo_->setCurrentShape(zone_->shape());
    shadowCombo_->setCurrentShadow(zone_->shadow());
    keySpinBox_->setValue(zone_->key());
    
    // Options
    activeCheck_->setChecked(zone_->isActive());
    editCheck_->setChecked(zone_->isEdit());
    stayLitCheck_->setChecked(zone_->stayLit());
    
    // Try to load type-specific properties from derived classes
    // ButtonZone properties
    if (auto* btn = dynamic_cast<ButtonZone*>(zone_)) {
        jumpTypeCombo_->setCurrentJumpType(btn->jumpType());
        jumpIdSpinBox_->setValue(btn->jumpPageId());
    }
    
    // MessageButtonZone properties
    if (auto* msgBtn = dynamic_cast<MessageButtonZone*>(zone_)) {
        messageEdit_->setText(msgBtn->message());
        confirmCheck_->setChecked(msgBtn->needsConfirm());
        confirmMsgEdit_->setText(msgBtn->confirmMessage());
    }
    
    // ConditionalZone properties
    if (auto* condZone = dynamic_cast<ConditionalZone*>(zone_)) {
        expressionEdit_->setText(condZone->expression());
    }
    
    // ItemZone properties  
    if (auto* item = dynamic_cast<ItemZone*>(zone_)) {
        itemNameEdit_->setText(item->name());
        itemPriceSpinBox_->setValue(item->price() / 100.0);
    }
}

void ZonePropertiesDialog::saveToZone() {
    if (!zone_) return;
    
    // General
    zone_->setName(nameEdit_->text());
    zone_->setRegion(xSpinBox_->value(), ySpinBox_->value(),
                     widthSpinBox_->value(), heightSpinBox_->value());
    zone_->setGroupId(groupSpinBox_->value());
    zone_->setZoneType(zoneTypeCombo_->currentZoneType());
    
    // Appearance - states
    for (int i = 0; i < 3; ++i) {
        ZoneState st;
        st.frame = stateWidgets_[i].frameCombo->currentFrame();
        st.texture = stateWidgets_[i].textureCombo->currentTextureId();
        st.color = stateWidgets_[i].colorCombo->currentColorId();
        zone_->setState(i, st);
    }
    
    zone_->setBehavior(behaviorCombo_->currentBehavior());
    zone_->setFont(fontCombo_->currentFontId());
    zone_->setShape(shapeCombo_->currentShape());
    zone_->setShadow(shadowCombo_->currentShadow());
    zone_->setKey(keySpinBox_->value());
    
    // Options
    zone_->setActive(activeCheck_->isChecked());
    zone_->setEdit(editCheck_->isChecked());
    zone_->setStayLit(stayLitCheck_->isChecked());
    
    // Save type-specific properties to derived classes
    // ButtonZone properties
    if (auto* btn = dynamic_cast<ButtonZone*>(zone_)) {
        btn->setJumpTarget(jumpIdSpinBox_->value(), jumpTypeCombo_->currentJumpType());
    }
    
    // MessageButtonZone properties
    if (auto* msgBtn = dynamic_cast<MessageButtonZone*>(zone_)) {
        msgBtn->setMessage(messageEdit_->text());
        msgBtn->setConfirm(confirmCheck_->isChecked(), confirmMsgEdit_->text());
    }
    
    // ConditionalZone properties
    if (auto* condZone = dynamic_cast<ConditionalZone*>(zone_)) {
        condZone->setExpression(expressionEdit_->text());
    }
    
    // ItemZone properties
    if (auto* item = dynamic_cast<ItemZone*>(zone_)) {
        item->setName(itemNameEdit_->text());
        item->setPrice(static_cast<int>(itemPriceSpinBox_->value() * 100));
    }
}

void ZonePropertiesDialog::replaceZoneIfTypeChanged() {
    if (!zone_ || !page_) return;
    
    ZoneType newType = zoneTypeCombo_->currentZoneType();
    
    // If type hasn't changed, no replacement needed
    if (newType == originalType_) {
        zoneReplaced_ = false;
        return;
    }
    
    // Create new zone of correct type using factory
    auto newZone = ZoneFactory::create(newType);
    if (!newZone) return;
    
    // Copy common properties from old zone
    newZone->setName(nameEdit_->text());
    newZone->setRegion(xSpinBox_->value(), ySpinBox_->value(),
                       widthSpinBox_->value(), heightSpinBox_->value());
    newZone->setGroupId(groupSpinBox_->value());
    newZone->setZoneType(newType);
    
    // Copy appearance states
    for (int i = 0; i < 3; ++i) {
        ZoneState st;
        st.frame = stateWidgets_[i].frameCombo->currentFrame();
        st.texture = stateWidgets_[i].textureCombo->currentTextureId();
        st.color = stateWidgets_[i].colorCombo->currentColorId();
        newZone->setState(i, st);
    }
    
    newZone->setBehavior(behaviorCombo_->currentBehavior());
    newZone->setFont(fontCombo_->currentFontId());
    newZone->setShape(shapeCombo_->currentShape());
    newZone->setShadow(shadowCombo_->currentShadow());
    newZone->setKey(keySpinBox_->value());
    newZone->setActive(activeCheck_->isChecked());
    newZone->setEdit(editCheck_->isChecked());
    newZone->setStayLit(stayLitCheck_->isChecked());
    
    // Copy ButtonZone properties if applicable
    if (auto* newBtn = dynamic_cast<ButtonZone*>(newZone.get())) {
        newBtn->setJumpTarget(jumpIdSpinBox_->value(), jumpTypeCombo_->currentJumpType());
        newBtn->setLabel(nameEdit_->text());
    }
    
    // Remove old zone and add new one
    page_->removeZone(zone_);
    replacementZone_ = newZone.get();
    page_->addZone(std::move(newZone));
    
    zoneReplaced_ = true;
}

void ZonePropertiesDialog::applyChanges() {
    if (page_) {
        // Check if type changed and we need to replace the zone
        replaceZoneIfTypeChanged();
        if (!zoneReplaced_) {
            // Just update existing zone
            saveToZone();
        }
    } else {
        // No page, just save to existing zone
        saveToZone();
    }
}

void ZonePropertiesDialog::onApply() {
    applyChanges();
}

void ZonePropertiesDialog::onOk() {
    applyChanges();
    accept();
}

void ZonePropertiesDialog::onStateTabChanged(int index) {
    updatePreview();
}

void ZonePropertiesDialog::onZoneTypeChanged(int index) {
    ZoneType type = zoneTypeCombo_->currentZoneType();
    applyZoneTypeDefaults(type);
    updateFieldVisibility();
    updatePreview();
}

void ZonePropertiesDialog::onJumpTypeChanged(int index) {
    // Show/hide jump ID based on jump type
    JumpType jt = jumpTypeCombo_->currentJumpType();
    bool showJumpId = (jt == JumpType::Normal || jt == JumpType::Stealth || jt == JumpType::Password);
    jumpIdLabel_->setVisible(showJumpId);
    jumpIdSpinBox_->setVisible(showJumpId);
}

void ZonePropertiesDialog::onItemTypeChanged(int index) {
    updateFieldVisibility();
}

void ZonePropertiesDialog::onReportTypeChanged(int index) {
    // Show check display and video target only for check report
    int rt = reportTypeCombo_->currentReportType();
    bool isCheckReport = (rt == 5);  // REPORT_CHECK
    checkDispLabel_->setVisible(isCheckReport);
    checkDispSpinBox_->setVisible(isCheckReport);
    videoTargetLabel_->setVisible(isCheckReport);
    videoTargetCombo_->setVisible(isCheckReport);
}

void ZonePropertiesDialog::updateFieldVisibility() {
    // This matches original ViewTouch ZoneDialog::Correct()
    ZoneType t = zoneTypeCombo_->currentZoneType();
    bool isItem = IsItemZoneType(t);
    int itype = itemTypeCombo_->currentItemType();
    
    // Derive itype from zone type for specific item zones
    if (t == ZoneType::ItemNormal) itype = 0;  // ITEM_NORMAL
    else if (t == ZoneType::ItemModifier) itype = 1;  // ITEM_MODIFIER
    else if (t == ZoneType::ItemMethod) itype = 2;  // ITEM_METHOD
    else if (t == ZoneType::ItemSubstitute) itype = 3;  // ITEM_SUBSTITUTE
    else if (t == ZoneType::ItemPound) itype = 4;  // ITEM_POUND
    else if (t == ZoneType::ItemAdmission) itype = 5;  // ITEM_ADMISSION
    
    // --- Name field ---
    bool showName = (t != ZoneType::Command && t != ZoneType::GuestCount &&
                     t != ZoneType::UserEdit && t != ZoneType::Inventory && t != ZoneType::Recipe &&
                     t != ZoneType::Vendor && t != ZoneType::ItemList && t != ZoneType::Invoice &&
                     t != ZoneType::Qualifier && t != ZoneType::Labor && t != ZoneType::Login &&
                     t != ZoneType::Logout && t != ZoneType::OrderEntry && t != ZoneType::OrderPage &&
                     t != ZoneType::OrderFlow && t != ZoneType::PaymentEntry && t != ZoneType::Switch &&
                     t != ZoneType::JobSecurity && t != ZoneType::TenderSet && t != ZoneType::Hardware &&
                     !isItem && t != ZoneType::OrderAdd && t != ZoneType::OrderDelete &&
                     t != ZoneType::OrderComment);
    nameEdit_->setVisible(showName);
    
    // --- Confirmation fields (ZONE_STANDARD only) ---
    bool showConfirm = (t == ZoneType::Standard);
    confirmLabel_->setVisible(showConfirm);
    confirmCheck_->setVisible(showConfirm);
    confirmMsgLabel_->setVisible(showConfirm);
    confirmMsgEdit_->setVisible(showConfirm);
    
    // --- Expression (ZONE_CONDITIONAL) ---
    bool showExpression = (t == ZoneType::Conditional);
    expressionLabel_->setVisible(showExpression);
    expressionEdit_->setVisible(showExpression);
    
    // --- Message (STANDARD, CONDITIONAL, TOGGLE) ---
    bool showMessage = (t == ZoneType::Standard || t == ZoneType::Conditional || t == ZoneType::Toggle);
    messageLabel_->setVisible(showMessage);
    messageEdit_->setVisible(showMessage);
    
    // --- Drawer zone type ---
    bool showDrawerType = (t == ZoneType::DrawerManage);
    drawerZoneTypeLabel_->setVisible(showDrawerType);
    drawerZoneTypeCombo_->setVisible(showDrawerType);
    
    // --- Filename (ZONE_READ) ---
    bool showFilename = (t == ZoneType::Read);
    filenameLabel_->setVisible(showFilename);
    filenameEdit_->setVisible(showFilename);
    
    // --- Image filename ---
    bool showImage = (t == ZoneType::Simple || t == ZoneType::IndexTab || isItem ||
                      t == ZoneType::Qualifier || t == ZoneType::Table || t == ZoneType::ImageButton);
    imageFilenameLabel_->setVisible(showImage);
    imageFilenameCombo_->setVisible(showImage);
    
    // --- Tender fields ---
    bool showTender = (t == ZoneType::Tender);
    tenderTypeLabel_->setVisible(showTender);
    tenderTypeCombo_->setVisible(showTender);
    tenderAmountLabel_->setVisible(showTender);
    tenderAmountSpinBox_->setVisible(showTender);
    
    // --- Report fields ---
    bool showReport = (t == ZoneType::Report);
    reportTypeLabel_->setVisible(showReport);
    reportTypeCombo_->setVisible(showReport);
    reportPrintLabel_->setVisible(showReport);
    reportPrintCombo_->setVisible(showReport);
    // Check display and video target only for check report - handled in onReportTypeChanged
    if (showReport) {
        onReportTypeChanged(reportTypeCombo_->currentIndex());
    } else {
        checkDispLabel_->setVisible(false);
        checkDispSpinBox_->setVisible(false);
        videoTargetLabel_->setVisible(false);
        videoTargetCombo_->setVisible(false);
    }
    
    // --- Spacing (list zones) ---
    bool showSpacing = (t == ZoneType::CheckList || t == ZoneType::DrawerManage ||
                        t == ZoneType::UserEdit || t == ZoneType::Inventory || t == ZoneType::Recipe ||
                        t == ZoneType::Vendor || t == ZoneType::ItemList || t == ZoneType::Invoice ||
                        t == ZoneType::Labor || t == ZoneType::OrderEntry || t == ZoneType::PaymentEntry ||
                        t == ZoneType::Payout || t == ZoneType::Report || t == ZoneType::Hardware ||
                        t == ZoneType::TenderSet || t == ZoneType::Merchant);
    spacingLabel_->setVisible(showSpacing);
    spacingSpinBox_->setVisible(showSpacing);
    
    // --- Qualifier (ZONE_QUALIFIER) ---
    bool showQualifier = (t == ZoneType::Qualifier);
    qualifierLabel_->setVisible(showQualifier);
    qualifierCombo_->setVisible(showQualifier);
    
    // --- Amount (ZONE_ORDER_PAGE) ---
    bool showAmount = (t == ZoneType::OrderPage);
    amountLabel_->setVisible(showAmount);
    amountSpinBox_->setVisible(showAmount);
    
    // --- Switch type (ZONE_SWITCH) ---
    bool showSwitch = (t == ZoneType::Switch);
    switchTypeLabel_->setVisible(showSwitch);
    switchTypeCombo_->setVisible(showSwitch);
    
    // --- Customer type (ZONE_TABLE) ---
    bool showCustomer = (t == ZoneType::Table);
    customerTypeLabel_->setVisible(showCustomer);
    customerTypeCombo_->setVisible(showCustomer);
    
    // --- Jump fields ---
    bool showJump = (isItem || t == ZoneType::Simple || t == ZoneType::IndexTab ||
                     t == ZoneType::Standard || t == ZoneType::Conditional || t == ZoneType::Qualifier);
    jumpTypeLabel_->setVisible(showJump);
    jumpTypeCombo_->setVisible(showJump);
    if (showJump) {
        onJumpTypeChanged(jumpTypeCombo_->currentIndex());
    } else {
        jumpIdLabel_->setVisible(false);
        jumpIdSpinBox_->setVisible(false);
    }
    
    // --- Key shortcut ---
    bool showKey = (t == ZoneType::Simple || t == ZoneType::IndexTab || t == ZoneType::Standard ||
                    t == ZoneType::Toggle || t == ZoneType::Conditional);
    keySpinBox_->setVisible(showKey);
    
    // ============ Item Tab Visibility ============
    // Show/hide the entire Item tab based on zone type
    int itemTabIndex = mainTabWidget_->indexOf(mainTabWidget_->findChild<QScrollArea*>());
    if (itemTabIndex < 0) {
        // Find by walking tabs
        for (int i = 0; i < mainTabWidget_->count(); ++i) {
            if (mainTabWidget_->tabText(i) == tr("Item")) {
                itemTabIndex = i;
                break;
            }
        }
    }
    if (itemTabIndex >= 0) {
        mainTabWidget_->setTabVisible(itemTabIndex, isItem);
    }
    
    // Item-specific field visibility
    itemTypeLabel_->setVisible(t == ZoneType::Item);  // Only for generic ZONE_ITEM
    itemTypeCombo_->setVisible(t == ZoneType::Item);
    
    itemNameLabel_->setVisible(isItem);
    itemNameEdit_->setVisible(isItem);
    itemZoneNameLabel_->setVisible(isItem);
    itemZoneNameEdit_->setVisible(isItem);
    itemPrintNameLabel_->setVisible(isItem);
    itemPrintNameEdit_->setVisible(isItem);
    
    itemPriceLabel_->setVisible(isItem);
    itemPriceSpinBox_->setVisible(isItem);
    
    bool showSubprice = (isItem && itype == 3);  // ITEM_SUBSTITUTE
    itemSubpriceLabel_->setVisible(showSubprice);
    itemSubpriceSpinBox_->setVisible(showSubprice);
    
    itemEmployeePriceLabel_->setVisible(isItem);
    itemEmployeePriceSpinBox_->setVisible(isItem);
    
    bool showFamily = (isItem && itype != 5);  // Not ITEM_ADMISSION
    itemFamilyLabel_->setVisible(showFamily);
    itemFamilyCombo_->setVisible(showFamily);
    itemSalesLabel_->setVisible(showFamily);
    itemSalesCombo_->setVisible(showFamily);
    
    bool showPrinter = (isItem && (itype == 0 || itype == 3 || itype == 4 || itype == 5));
    itemPrinterLabel_->setVisible(showPrinter);
    itemPrinterCombo_->setVisible(showPrinter);
    
    bool showCallOrder = (isItem && itype != 0 && itype != 4);
    itemCallOrderLabel_->setVisible(showCallOrder);
    itemCallOrderCombo_->setVisible(showCallOrder);
    
    pageListLabel_->setVisible(isItem);
    pageListEdit_->setVisible(isItem);
    
    // Admission-specific fields
    bool showAdmission = (isItem && itype == 5);  // ITEM_ADMISSION
    itemLocationLabel_->setVisible(showAdmission);
    itemLocationEdit_->setVisible(showAdmission);
    itemEventTimeLabel_->setVisible(showAdmission);
    itemEventTimeEdit_->setVisible(showAdmission);
    itemTotalTicketsLabel_->setVisible(showAdmission);
    itemTotalTicketsSpinBox_->setVisible(showAdmission);
    itemPriceLabelLabel_->setVisible(showAdmission);
    itemPriceLabelEdit_->setVisible(showAdmission);
}

void ZonePropertiesDialog::applyZoneTypeDefaults(ZoneType type) {
    // Apply default frame, texture, color, font, behavior, and size based on zone type
    // This matches the original ViewTouch zone defaults
    
    ZoneFrame defaultFrame = ZoneFrame::Border;
    uint8_t defaultTexture = static_cast<uint8_t>(TextureId::DarkWood);
    uint8_t defaultColor = static_cast<uint8_t>(TextColor::White);
    FontId defaultFont = FontId::Times24;
    ZoneBehavior defaultBehavior = ZoneBehavior::Blink;
    int defaultW = 140, defaultH = 100;
    
    switch (type) {
        // Basic buttons
        case ZoneType::Simple:
        case ZoneType::Standard:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::Toggle:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::GreenTexture);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::Conditional:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::Comment:
            defaultFrame = ZoneFrame::None;
            defaultTexture = TEXTURE_CLEAR;
            defaultColor = static_cast<uint8_t>(TextColor::Gray);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 200; defaultH = 40;
            break;
            
        case ZoneType::Switch:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::GrayParchment);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        // Menu items
        case ZoneType::Item:
        case ZoneType::ItemNormal:
        case ZoneType::ItemModifier:
        case ZoneType::ItemMethod:
        case ZoneType::ItemSubstitute:
        case ZoneType::ItemPound:
        case ZoneType::ItemAdmission:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::GreenTexture);
            defaultFont = FontId::Times20;
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::Qualifier:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::GreenMarble);
            defaultFont = FontId::Times20;
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        // Payments
        case ZoneType::Tender:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::DarkWood);
            defaultFont = FontId::Times24B;
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::TenderSet:
        case ZoneType::PaymentEntry:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::DarkWood);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::Payout:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        // Tables
        case ZoneType::Table:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::GrayMarble);
            defaultFont = FontId::Times24B;
            defaultBehavior = ZoneBehavior::Blink;
            defaultW = 80; defaultH = 80;
            break;
            
        case ZoneType::TableAssign:
        case ZoneType::CheckDisplay:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::CheckList:
        case ZoneType::CheckEdit:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 500;
            break;
            
        case ZoneType::SplitCheck:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::GreenMarble);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        // User management
        case ZoneType::Login:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultFont = FontId::Times34B;
            defaultBehavior = ZoneBehavior::None;
            defaultW = 300; defaultH = 200;
            break;
            
        case ZoneType::Logout:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            defaultFont = FontId::Times24B;
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::UserEdit:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 500;
            break;
            
        case ZoneType::GuestCount:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::GrayMarble);
            defaultFont = FontId::Times34B;
            defaultBehavior = ZoneBehavior::Blink;
            defaultW = 80; defaultH = 80;
            break;
            
        // Order entry
        case ZoneType::OrderEntry:
        case ZoneType::OrderDisplay:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::Parchment);
            defaultColor = static_cast<uint8_t>(TextColor::Black);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 300; defaultH = 500;
            break;
            
        case ZoneType::OrderPage:
        case ZoneType::OrderFlow:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::OrderAdd:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::GreenTexture);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::OrderDelete:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::OrderComment:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::OrangeTexture);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        // Settings
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
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::GrayParchment);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 500;
            break;
            
        case ZoneType::Developer:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 500;
            break;
            
        // Hardware
        case ZoneType::Hardware:
        case ZoneType::PrintTarget:
        case ZoneType::ItemTarget:
        case ZoneType::VideoTarget:
        case ZoneType::SplitKitchen:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::GrayParchment);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 500;
            break;
            
        case ZoneType::CDU:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::Black);
            defaultColor = static_cast<uint8_t>(TextColor::Green);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 300; defaultH = 100;
            break;
            
        case ZoneType::DrawerManage:
        case ZoneType::DrawerAssign:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::DarkWood);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        // Reports
        case ZoneType::Report:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::Parchment);
            defaultColor = static_cast<uint8_t>(TextColor::Black);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 500; defaultH = 600;
            break;
            
        case ZoneType::Chart:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::WhiteTexture);
            defaultColor = static_cast<uint8_t>(TextColor::Black);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 500; defaultH = 400;
            break;
            
        case ZoneType::Search:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::Read:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::Parchment);
            defaultColor = static_cast<uint8_t>(TextColor::Black);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 400;
            break;
            
        // Inventory
        case ZoneType::Inventory:
        case ZoneType::Recipe:
        case ZoneType::Vendor:
        case ZoneType::ItemList:
        case ZoneType::Invoice:
        case ZoneType::Account:
        case ZoneType::RevenueGroups:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::TanParchment);
            defaultColor = static_cast<uint8_t>(TextColor::Black);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 500;
            break;
            
        case ZoneType::Expense:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        // Scheduling
        case ZoneType::Schedule:
        case ZoneType::Labor:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::TanParchment);
            defaultColor = static_cast<uint8_t>(TextColor::Black);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 500;
            break;
            
        case ZoneType::EndDay:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::DarkOrangeTexture);
            defaultFont = FontId::Times24B;
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        // Customer
        case ZoneType::CustomerInfo:
        case ZoneType::CreditCardList:
        case ZoneType::Merchant:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::TanParchment);
            defaultColor = static_cast<uint8_t>(TextColor::Black);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 500;
            break;
            
        // System
        case ZoneType::Command:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        case ZoneType::Phrase:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::TanParchment);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 500;
            break;
            
        case ZoneType::License:
        case ZoneType::ExpireMsg:
            defaultFrame = ZoneFrame::DoubleBorder;
            defaultTexture = static_cast<uint8_t>(TextureId::GrayParchment);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 400; defaultH = 300;
            break;
            
        case ZoneType::KillSystem:
        case ZoneType::ClearSystem:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::Lava);
            defaultFont = FontId::Times24B;
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        // Status/Image buttons
        case ZoneType::StatusButton:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultBehavior = ZoneBehavior::None;
            defaultW = 200; defaultH = 40;
            break;
            
        case ZoneType::ImageButton:
            defaultFrame = ZoneFrame::None;
            defaultTexture = TEXTURE_CLEAR;
            defaultBehavior = ZoneBehavior::Blink;
            defaultW = 200; defaultH = 200;
            break;
            
        case ZoneType::IndexTab:
        case ZoneType::LanguageButton:
            defaultFrame = ZoneFrame::Border;
            defaultTexture = static_cast<uint8_t>(TextureId::BlueParchment);
            defaultBehavior = ZoneBehavior::Blink;
            break;
            
        default:
            // Use defaults set above
            break;
    }
    
    // Apply to state widgets (Normal state)
    stateWidgets_[0].frameCombo->setCurrentFrame(defaultFrame);
    stateWidgets_[0].textureCombo->setCurrentTextureId(defaultTexture);
    stateWidgets_[0].colorCombo->setCurrentColorId(defaultColor);
    
    // Selected state - typically brighter/highlighted
    stateWidgets_[1].frameCombo->setCurrentFrame(defaultFrame);
    stateWidgets_[1].textureCombo->setCurrentTextureId(
        static_cast<uint8_t>(TextureId::LitSand));  // Highlighted
    stateWidgets_[1].colorCombo->setCurrentColorId(defaultColor);
    
    // Alternate state - same as normal
    stateWidgets_[2].frameCombo->setCurrentFrame(defaultFrame);
    stateWidgets_[2].textureCombo->setCurrentTextureId(defaultTexture);
    stateWidgets_[2].colorCombo->setCurrentColorId(defaultColor);
    
    // Apply font and behavior
    fontCombo_->setCurrentFontId(defaultFont);
    behaviorCombo_->setCurrentBehavior(defaultBehavior);
    
    // Apply size defaults
    widthSpinBox_->setValue(defaultW);
    heightSpinBox_->setValue(defaultH);
}

void ZonePropertiesDialog::updatePreview() {
    // TODO: Update preview widget
}

/*************************************************************
 * ColorComboBox Implementation
 *************************************************************/
ColorComboBox::ColorComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Black"), static_cast<int>(TextColor::Black));
    addItem(tr("White"), static_cast<int>(TextColor::White));
    addItem(tr("Red"), static_cast<int>(TextColor::Red));
    addItem(tr("Green"), static_cast<int>(TextColor::Green));
    addItem(tr("Blue"), static_cast<int>(TextColor::Blue));
    addItem(tr("Yellow"), static_cast<int>(TextColor::Yellow));
    addItem(tr("Brown"), static_cast<int>(TextColor::Brown));
    addItem(tr("Orange"), static_cast<int>(TextColor::Orange));
    addItem(tr("Purple"), static_cast<int>(TextColor::Purple));
    addItem(tr("Teal"), static_cast<int>(TextColor::Teal));
    addItem(tr("Gray"), static_cast<int>(TextColor::Gray));
    addItem(tr("Magenta"), static_cast<int>(TextColor::Magenta));
    addItem(tr("Red-Orange"), static_cast<int>(TextColor::RedOrange));
    addItem(tr("Sea Green"), static_cast<int>(TextColor::SeaGreen));
    addItem(tr("Light Blue"), static_cast<int>(TextColor::LtBlue));
    addItem(tr("Dark Red"), static_cast<int>(TextColor::DkRed));
    addItem(tr("Dark Green"), static_cast<int>(TextColor::DkGreen));
    addItem(tr("Dark Blue"), static_cast<int>(TextColor::DkBlue));
    addItem(tr("Dark Teal"), static_cast<int>(TextColor::DkTeal));
    addItem(tr("Dark Magenta"), static_cast<int>(TextColor::DkMagenta));
    addItem(tr("Dark Sea Green"), static_cast<int>(TextColor::DkSeaGreen));
}

void ColorComboBox::setCurrentColorId(uint8_t id) {
    int index = findData(static_cast<int>(id));
    if (index >= 0) setCurrentIndex(index);
}

uint8_t ColorComboBox::currentColorId() const {
    return static_cast<uint8_t>(currentData().toInt());
}

/*************************************************************
 * TextureComboBox Implementation
 *************************************************************/
TextureComboBox::TextureComboBox(QWidget* parent)
    : QComboBox(parent)
{
    // Textures matching original ViewTouch XPM files
    addItem(tr("Sand"), static_cast<int>(TextureId::Sand));
    addItem(tr("Lite Sand"), static_cast<int>(TextureId::LitSand));
    addItem(tr("Dark Sand"), static_cast<int>(TextureId::DarkSand));
    addItem(tr("Lite Wood"), static_cast<int>(TextureId::LiteWood));
    addItem(tr("Wood"), static_cast<int>(TextureId::Wood));
    addItem(tr("Dark Wood"), static_cast<int>(TextureId::DarkWood));
    addItem(tr("Gray Parchment"), static_cast<int>(TextureId::GrayParchment));
    addItem(tr("Gray Marble"), static_cast<int>(TextureId::GrayMarble));
    addItem(tr("Green Marble"), static_cast<int>(TextureId::GreenMarble));
    addItem(tr("Parchment"), static_cast<int>(TextureId::Parchment));
    addItem(tr("Pearl"), static_cast<int>(TextureId::Pearl));
    addItem(tr("Canvas"), static_cast<int>(TextureId::Canvas));
    addItem(tr("Tan Parchment"), static_cast<int>(TextureId::TanParchment));
    addItem(tr("Smoke"), static_cast<int>(TextureId::Smoke));
    addItem(tr("Leather"), static_cast<int>(TextureId::Leather));
    addItem(tr("Blue Parchment"), static_cast<int>(TextureId::BlueParchment));
    addItem(tr("Gradient"), static_cast<int>(TextureId::Gradient));
    addItem(tr("Brown Gradient"), static_cast<int>(TextureId::GradientBrown));
    addItem(tr("Black"), static_cast<int>(TextureId::Black));
    addItem(tr("Grey Sand"), static_cast<int>(TextureId::GreySand));
    addItem(tr("White Mesh"), static_cast<int>(TextureId::WhiteMesh));
    addItem(tr("Carbon Fiber"), static_cast<int>(TextureId::CarbonFiber));
    addItem(tr("White Texture"), static_cast<int>(TextureId::WhiteTexture));
    addItem(tr("Dark Orange"), static_cast<int>(TextureId::DarkOrangeTexture));
    addItem(tr("Yellow Texture"), static_cast<int>(TextureId::YellowTexture));
    addItem(tr("Green Texture"), static_cast<int>(TextureId::GreenTexture));
    addItem(tr("Orange Texture"), static_cast<int>(TextureId::OrangeTexture));
    addItem(tr("Blue Texture"), static_cast<int>(TextureId::BlueTexture));
    addItem(tr("Pool Table"), static_cast<int>(TextureId::PoolTable));
    addItem(tr("Test"), static_cast<int>(TextureId::Test));
    addItem(tr("Diamond Leather"), static_cast<int>(TextureId::DiamondLeather));
    addItem(tr("Bread"), static_cast<int>(TextureId::Bread));
    addItem(tr("Lava"), static_cast<int>(TextureId::Lava));
    addItem(tr("Dark Marble"), static_cast<int>(TextureId::DarkMarble));
}

void TextureComboBox::setCurrentTextureId(uint8_t id) {
    int index = findData(static_cast<int>(id));
    if (index >= 0) setCurrentIndex(index);
}

uint8_t TextureComboBox::currentTextureId() const {
    return static_cast<uint8_t>(currentData().toInt());
}

/*************************************************************
 * FrameComboBox Implementation
 *************************************************************/
FrameComboBox::FrameComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Default"), static_cast<int>(ZoneFrame::Default));
    addItem(tr("None"), static_cast<int>(ZoneFrame::None));
    addItem(tr("Hidden"), static_cast<int>(ZoneFrame::Hidden));
    addItem(tr("Raised"), static_cast<int>(ZoneFrame::Raised));
    addItem(tr("Raised 1"), static_cast<int>(ZoneFrame::Raised1));
    addItem(tr("Raised 2"), static_cast<int>(ZoneFrame::Raised2));
    addItem(tr("Raised 3"), static_cast<int>(ZoneFrame::Raised3));
    addItem(tr("Inset"), static_cast<int>(ZoneFrame::Inset));
    addItem(tr("Inset 1"), static_cast<int>(ZoneFrame::Inset1));
    addItem(tr("Inset 2"), static_cast<int>(ZoneFrame::Inset2));
    addItem(tr("Inset 3"), static_cast<int>(ZoneFrame::Inset3));
    addItem(tr("Double"), static_cast<int>(ZoneFrame::Double));
    addItem(tr("Double 1"), static_cast<int>(ZoneFrame::Double1));
    addItem(tr("Double 2"), static_cast<int>(ZoneFrame::Double2));
    addItem(tr("Double 3"), static_cast<int>(ZoneFrame::Double3));
    addItem(tr("Border"), static_cast<int>(ZoneFrame::Border));
    addItem(tr("Clear Border"), static_cast<int>(ZoneFrame::ClearBorder));
    addItem(tr("Sand Border"), static_cast<int>(ZoneFrame::SandBorder));
    addItem(tr("Inset Border"), static_cast<int>(ZoneFrame::InsetBorder));
    addItem(tr("Double Border"), static_cast<int>(ZoneFrame::DoubleBorder));
}

void FrameComboBox::setCurrentFrame(ZoneFrame frame) {
    int index = findData(static_cast<int>(frame));
    if (index >= 0) setCurrentIndex(index);
}

ZoneFrame FrameComboBox::currentFrame() const {
    return static_cast<ZoneFrame>(currentData().toInt());
}

/*************************************************************
 * FontComboBox Implementation
 *************************************************************/
FontComboBox::FontComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Default"), static_cast<int>(FontId::Default));
    addItem(tr("Times 14"), static_cast<int>(FontId::Times14));
    addItem(tr("Times 14 Bold"), static_cast<int>(FontId::Times14B));
    addItem(tr("Times 18"), static_cast<int>(FontId::Times18));
    addItem(tr("Times 18 Bold"), static_cast<int>(FontId::Times18B));
    addItem(tr("Times 20"), static_cast<int>(FontId::Times20));
    addItem(tr("Times 20 Bold"), static_cast<int>(FontId::Times20B));
    addItem(tr("Times 24"), static_cast<int>(FontId::Times24));
    addItem(tr("Times 24 Bold"), static_cast<int>(FontId::Times24B));
    addItem(tr("Times 34"), static_cast<int>(FontId::Times34));
    addItem(tr("Times 34 Bold"), static_cast<int>(FontId::Times34B));
    addItem(tr("Times 48"), static_cast<int>(FontId::Times48));
    addItem(tr("Times 48 Bold"), static_cast<int>(FontId::Times48B));
}

void FontComboBox::setCurrentFontId(FontId id) {
    int index = findData(static_cast<int>(id));
    if (index >= 0) setCurrentIndex(index);
}

FontId FontComboBox::currentFontId() const {
    return static_cast<FontId>(currentData().toInt());
}

/*************************************************************
 * BehaviorComboBox Implementation
 *************************************************************/
BehaviorComboBox::BehaviorComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("None"), static_cast<int>(ZoneBehavior::None));
    addItem(tr("Blink"), static_cast<int>(ZoneBehavior::Blink));
    addItem(tr("Toggle"), static_cast<int>(ZoneBehavior::Toggle));
    addItem(tr("Select"), static_cast<int>(ZoneBehavior::Select));
    addItem(tr("Double"), static_cast<int>(ZoneBehavior::Double));
    addItem(tr("Miss"), static_cast<int>(ZoneBehavior::Miss));
}

void BehaviorComboBox::setCurrentBehavior(ZoneBehavior behavior) {
    int index = findData(static_cast<int>(behavior));
    if (index >= 0) setCurrentIndex(index);
}

ZoneBehavior BehaviorComboBox::currentBehavior() const {
    return static_cast<ZoneBehavior>(currentData().toInt());
}

/*************************************************************
 * ShapeComboBox Implementation
 *************************************************************/
ShapeComboBox::ShapeComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Rectangle"), static_cast<int>(ZoneShape::Rectangle));
    addItem(tr("Diamond"), static_cast<int>(ZoneShape::Diamond));
    addItem(tr("Circle"), static_cast<int>(ZoneShape::Circle));
    addItem(tr("Hexagon"), static_cast<int>(ZoneShape::Hexagon));
    addItem(tr("Octagon"), static_cast<int>(ZoneShape::Octagon));
}

void ShapeComboBox::setCurrentShape(ZoneShape shape) {
    int index = findData(static_cast<int>(shape));
    if (index >= 0) setCurrentIndex(index);
}

ZoneShape ShapeComboBox::currentShape() const {
    return static_cast<ZoneShape>(currentData().toInt());
}

/*************************************************************
 * ZoneTypeComboBox Implementation
 *************************************************************/
ZoneTypeComboBox::ZoneTypeComboBox(QWidget* parent)
    : QComboBox(parent)
{
    // Basic buttons
    addItem(tr("Standard Button"), static_cast<int>(ZoneType::Standard));
    addItem(tr("Simple Button"), static_cast<int>(ZoneType::Simple));
    addItem(tr("Toggle Button"), static_cast<int>(ZoneType::Toggle));
    addItem(tr("Conditional"), static_cast<int>(ZoneType::Conditional));
    addItem(tr("Switch"), static_cast<int>(ZoneType::Switch));
    addItem(tr("Comment"), static_cast<int>(ZoneType::Comment));
    addItem(tr("Status Button"), static_cast<int>(ZoneType::StatusButton));
    addItem(tr("Image Button"), static_cast<int>(ZoneType::ImageButton));
    addItem(tr("Language Button"), static_cast<int>(ZoneType::LanguageButton));
    addItem(tr("Index Tab"), static_cast<int>(ZoneType::IndexTab));
    
    // Menu items
    addItem(tr("Menu Item"), static_cast<int>(ZoneType::Item));
    addItem(tr("Item Normal"), static_cast<int>(ZoneType::ItemNormal));
    addItem(tr("Item Modifier"), static_cast<int>(ZoneType::ItemModifier));
    addItem(tr("Item Method"), static_cast<int>(ZoneType::ItemMethod));
    addItem(tr("Item Substitute"), static_cast<int>(ZoneType::ItemSubstitute));
    addItem(tr("Item Pound"), static_cast<int>(ZoneType::ItemPound));
    addItem(tr("Item Admission"), static_cast<int>(ZoneType::ItemAdmission));
    addItem(tr("Qualifier"), static_cast<int>(ZoneType::Qualifier));
    
    // Order management
    addItem(tr("Order Entry"), static_cast<int>(ZoneType::OrderEntry));
    addItem(tr("Order Page"), static_cast<int>(ZoneType::OrderPage));
    addItem(tr("Order Flow"), static_cast<int>(ZoneType::OrderFlow));
    addItem(tr("Order Add"), static_cast<int>(ZoneType::OrderAdd));
    addItem(tr("Order Delete"), static_cast<int>(ZoneType::OrderDelete));
    addItem(tr("Order Comment"), static_cast<int>(ZoneType::OrderComment));
    addItem(tr("Order Display"), static_cast<int>(ZoneType::OrderDisplay));
    
    // Payments
    addItem(tr("Tender"), static_cast<int>(ZoneType::Tender));
    addItem(tr("Payment Entry"), static_cast<int>(ZoneType::PaymentEntry));
    addItem(tr("Tender Set"), static_cast<int>(ZoneType::TenderSet));
    addItem(tr("Payout"), static_cast<int>(ZoneType::Payout));
    
    // Tables & Checks
    addItem(tr("Table"), static_cast<int>(ZoneType::Table));
    addItem(tr("Table Assign"), static_cast<int>(ZoneType::TableAssign));
    addItem(tr("Check List"), static_cast<int>(ZoneType::CheckList));
    addItem(tr("Check Display"), static_cast<int>(ZoneType::CheckDisplay));
    addItem(tr("Check Edit"), static_cast<int>(ZoneType::CheckEdit));
    addItem(tr("Split Check"), static_cast<int>(ZoneType::SplitCheck));
    
    // User management
    addItem(tr("Login"), static_cast<int>(ZoneType::Login));
    addItem(tr("Logout"), static_cast<int>(ZoneType::Logout));
    addItem(tr("User Edit"), static_cast<int>(ZoneType::UserEdit));
    addItem(tr("Guest Count"), static_cast<int>(ZoneType::GuestCount));
    
    // Settings
    addItem(tr("Settings"), static_cast<int>(ZoneType::Settings));
    addItem(tr("Tax Settings"), static_cast<int>(ZoneType::TaxSettings));
    addItem(tr("Tax Set"), static_cast<int>(ZoneType::TaxSet));
    addItem(tr("Money Set"), static_cast<int>(ZoneType::MoneySet));
    addItem(tr("Time Settings"), static_cast<int>(ZoneType::TimeSettings));
    addItem(tr("CC Settings"), static_cast<int>(ZoneType::CCSettings));
    addItem(tr("CC Messages"), static_cast<int>(ZoneType::CCMsgSettings));
    addItem(tr("Receipt Settings"), static_cast<int>(ZoneType::ReceiptSet));
    addItem(tr("Receipts"), static_cast<int>(ZoneType::Receipts));
    addItem(tr("Calculation Settings"), static_cast<int>(ZoneType::CalculationSettings));
    addItem(tr("Job Security"), static_cast<int>(ZoneType::JobSecurity));
    addItem(tr("Developer"), static_cast<int>(ZoneType::Developer));
    
    // Hardware
    addItem(tr("Hardware"), static_cast<int>(ZoneType::Hardware));
    addItem(tr("Print Target"), static_cast<int>(ZoneType::PrintTarget));
    addItem(tr("Item Target"), static_cast<int>(ZoneType::ItemTarget));
    addItem(tr("Video Target"), static_cast<int>(ZoneType::VideoTarget));
    addItem(tr("CDU"), static_cast<int>(ZoneType::CDU));
    addItem(tr("Split Kitchen"), static_cast<int>(ZoneType::SplitKitchen));
    addItem(tr("Drawer Manage"), static_cast<int>(ZoneType::DrawerManage));
    addItem(tr("Drawer Assign"), static_cast<int>(ZoneType::DrawerAssign));
    
    // Reports
    addItem(tr("Report"), static_cast<int>(ZoneType::Report));
    addItem(tr("Chart"), static_cast<int>(ZoneType::Chart));
    addItem(tr("Search"), static_cast<int>(ZoneType::Search));
    addItem(tr("Read"), static_cast<int>(ZoneType::Read));
    
    // Inventory
    addItem(tr("Inventory"), static_cast<int>(ZoneType::Inventory));
    addItem(tr("Recipe"), static_cast<int>(ZoneType::Recipe));
    addItem(tr("Vendor"), static_cast<int>(ZoneType::Vendor));
    addItem(tr("Item List"), static_cast<int>(ZoneType::ItemList));
    addItem(tr("Invoice"), static_cast<int>(ZoneType::Invoice));
    addItem(tr("Expense"), static_cast<int>(ZoneType::Expense));
    addItem(tr("Account"), static_cast<int>(ZoneType::Account));
    addItem(tr("Revenue Groups"), static_cast<int>(ZoneType::RevenueGroups));
    
    // Scheduling
    addItem(tr("Schedule"), static_cast<int>(ZoneType::Schedule));
    addItem(tr("Labor"), static_cast<int>(ZoneType::Labor));
    addItem(tr("End Day"), static_cast<int>(ZoneType::EndDay));
    
    // Customer
    addItem(tr("Customer Info"), static_cast<int>(ZoneType::CustomerInfo));
    addItem(tr("Credit Card List"), static_cast<int>(ZoneType::CreditCardList));
    addItem(tr("Merchant"), static_cast<int>(ZoneType::Merchant));
    
    // System
    addItem(tr("Command"), static_cast<int>(ZoneType::Command));
    addItem(tr("Phrase"), static_cast<int>(ZoneType::Phrase));
    addItem(tr("License"), static_cast<int>(ZoneType::License));
    addItem(tr("Expire Message"), static_cast<int>(ZoneType::ExpireMsg));
    addItem(tr("Kill System"), static_cast<int>(ZoneType::KillSystem));
    addItem(tr("Clear System"), static_cast<int>(ZoneType::ClearSystem));
}

void ZoneTypeComboBox::setCurrentZoneType(ZoneType type) {
    int index = findData(static_cast<int>(type));
    if (index >= 0) setCurrentIndex(index);
}

ZoneType ZoneTypeComboBox::currentZoneType() const {
    return static_cast<ZoneType>(currentData().toInt());
}

/*************************************************************
 * JumpTypeComboBox Implementation
 *************************************************************/
JumpTypeComboBox::JumpTypeComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Remain On This Page"), static_cast<int>(JumpType::None));
    addItem(tr("Jump To A Modifier Page"), static_cast<int>(JumpType::Normal));
    addItem(tr("Move To A Menu Item Page"), static_cast<int>(JumpType::Stealth));
    addItem(tr("Return From A Jump"), static_cast<int>(JumpType::Return));
    addItem(tr("Follow The Script"), static_cast<int>(JumpType::Script));
    addItem(tr("Return to Index"), static_cast<int>(JumpType::Index));
    addItem(tr("Return To The Starting Page"), static_cast<int>(JumpType::Home));
    addItem(tr("Query Password Then Jump"), static_cast<int>(JumpType::Password));
}

void JumpTypeComboBox::setCurrentJumpType(JumpType type) {
    int index = findData(static_cast<int>(type));
    if (index >= 0) setCurrentIndex(index);
}

JumpType JumpTypeComboBox::currentJumpType() const {
    return static_cast<JumpType>(currentData().toInt());
}

/*************************************************************
 * TenderTypeComboBox Implementation
 *************************************************************/
TenderTypeComboBox::TenderTypeComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Cash"), 0);
    addItem(tr("Check"), 1);
    addItem(tr("Credit Card"), 2);
    addItem(tr("Charge"), 3);
    addItem(tr("Gift Certificate"), 4);
    addItem(tr("Coupon"), 5);
    addItem(tr("Discount"), 6);
    addItem(tr("Comp"), 7);
    addItem(tr("Employee Meal"), 8);
    addItem(tr("Gratuity"), 9);
    addItem(tr("Money Order"), 10);
    addItem(tr("Room Charge"), 11);
    addItem(tr("Debit Card"), 12);
    addItem(tr("Expense"), 13);
    addItem(tr("Account"), 14);
    addItem(tr("Gift Card"), 15);
    addItem(tr("Captured Tip"), 16);
    addItem(tr("Change"), 17);
    addItem(tr("Overage"), 18);
}

void TenderTypeComboBox::setCurrentTenderType(int type) {
    int index = findData(type);
    if (index >= 0) setCurrentIndex(index);
}

int TenderTypeComboBox::currentTenderType() const {
    return currentData().toInt();
}

/*************************************************************
 * ReportTypeComboBox Implementation
 *************************************************************/
ReportTypeComboBox::ReportTypeComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Server Report"), 0);
    addItem(tr("Drawer Report"), 1);
    addItem(tr("Audit Report"), 2);
    addItem(tr("System Report"), 3);
    addItem(tr("Balance Report"), 4);
    addItem(tr("Check Display"), 5);
    addItem(tr("Deposit Report"), 6);
    addItem(tr("Work Order"), 7);
    addItem(tr("Customer Report"), 8);
    addItem(tr("Expense Report"), 9);
    addItem(tr("Royalty Report"), 10);
    addItem(tr("Exception Report"), 11);
    addItem(tr("Table Status"), 12);
    addItem(tr("Item Report"), 13);
    addItem(tr("Zone Report"), 14);
    addItem(tr("Credit Card Report"), 15);
    addItem(tr("Data Report"), 16);
}

void ReportTypeComboBox::setCurrentReportType(int type) {
    int index = findData(type);
    if (index >= 0) setCurrentIndex(index);
}

int ReportTypeComboBox::currentReportType() const {
    return currentData().toInt();
}

/*************************************************************
 * SwitchTypeComboBox Implementation
 *************************************************************/
SwitchTypeComboBox::SwitchTypeComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Seat"), 0);
    addItem(tr("Drawer"), 1);
    addItem(tr("Page"), 2);
    addItem(tr("User"), 3);
    addItem(tr("Terminal"), 4);
    addItem(tr("Printer"), 5);
    addItem(tr("Video"), 6);
    addItem(tr("Language"), 7);
}

void SwitchTypeComboBox::setCurrentSwitchType(int type) {
    int index = findData(type);
    if (index >= 0) setCurrentIndex(index);
}

int SwitchTypeComboBox::currentSwitchType() const {
    return currentData().toInt();
}

/*************************************************************
 * QualifierComboBox Implementation
 *************************************************************/
QualifierComboBox::QualifierComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("No"), 0);
    addItem(tr("Extra"), 1);
    addItem(tr("Lite"), 2);
    addItem(tr("Only"), 3);
    addItem(tr("Side"), 4);
    addItem(tr("Sub"), 5);
    addItem(tr("Half 1"), 6);
    addItem(tr("Half 2"), 7);
}

void QualifierComboBox::setCurrentQualifier(int type) {
    int index = findData(type);
    if (index >= 0) setCurrentIndex(index);
}

int QualifierComboBox::currentQualifier() const {
    return currentData().toInt();
}

/*************************************************************
 * CustomerTypeComboBox Implementation
 *************************************************************/
CustomerTypeComboBox::CustomerTypeComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("None"), 0);
    addItem(tr("Take Out"), 1);
    addItem(tr("Delivery"), 2);
    addItem(tr("Fast Food"), 3);
    addItem(tr("Call In"), 4);
    addItem(tr("Tab"), 5);
    addItem(tr("Hotel"), 6);
    addItem(tr("Retail"), 7);
}

void CustomerTypeComboBox::setCurrentCustomerType(int type) {
    int index = findData(type);
    if (index >= 0) setCurrentIndex(index);
}

int CustomerTypeComboBox::currentCustomerType() const {
    return currentData().toInt();
}

/*************************************************************
 * ItemTypeComboBox Implementation
 *************************************************************/
ItemTypeComboBox::ItemTypeComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Normal"), 0);
    addItem(tr("Modifier"), 1);
    addItem(tr("Method"), 2);
    addItem(tr("Substitute"), 3);
    addItem(tr("By Weight"), 4);
    addItem(tr("Admission"), 5);
}

void ItemTypeComboBox::setCurrentItemType(int type) {
    int index = findData(type);
    if (index >= 0) setCurrentIndex(index);
}

int ItemTypeComboBox::currentItemType() const {
    return currentData().toInt();
}

/*************************************************************
 * ItemFamilyComboBox Implementation
 *************************************************************/
ItemFamilyComboBox::ItemFamilyComboBox(QWidget* parent)
    : QComboBox(parent)
{
    // These should be loaded from system settings, but for now use defaults
    addItem(tr("Appetizers"), 1);
    addItem(tr("Soups"), 2);
    addItem(tr("Salads"), 3);
    addItem(tr("Entrees"), 4);
    addItem(tr("Pizza"), 5);
    addItem(tr("Sandwiches"), 6);
    addItem(tr("Sides"), 7);
    addItem(tr("Desserts"), 8);
    addItem(tr("Beverages"), 9);
    addItem(tr("Beer"), 10);
    addItem(tr("Wine"), 11);
    addItem(tr("Liquor"), 12);
    addItem(tr("Breakfast"), 13);
    addItem(tr("Kids Menu"), 14);
    addItem(tr("Specials"), 15);
    addItem(tr("Retail"), 16);
}

void ItemFamilyComboBox::setCurrentFamily(int family) {
    int index = findData(family);
    if (index >= 0) setCurrentIndex(index);
}

int ItemFamilyComboBox::currentFamily() const {
    return currentData().toInt();
}

/*************************************************************
 * SalesTypeComboBox Implementation
 *************************************************************/
SalesTypeComboBox::SalesTypeComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Food"), 0);
    addItem(tr("Beverage"), 1);
    addItem(tr("Beer"), 2);
    addItem(tr("Wine"), 3);
    addItem(tr("Liquor"), 4);
    addItem(tr("Merchandise"), 5);
    addItem(tr("Room"), 6);
    addItem(tr("Tax Exempt"), 7);
}

void SalesTypeComboBox::setCurrentSalesType(int type) {
    int index = findData(type);
    if (index >= 0) setCurrentIndex(index);
}

int SalesTypeComboBox::currentSalesType() const {
    return currentData().toInt();
}

/*************************************************************
 * PrinterComboBox Implementation
 *************************************************************/
PrinterComboBox::PrinterComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("None"), 0);
    addItem(tr("Kitchen 1"), 1);
    addItem(tr("Kitchen 2"), 2);
    addItem(tr("Kitchen 3"), 3);
    addItem(tr("Bar"), 4);
    addItem(tr("Expediter"), 5);
    addItem(tr("Receipt"), 6);
    addItem(tr("Report"), 7);
}

void PrinterComboBox::setCurrentPrinter(int id) {
    int index = findData(id);
    if (index >= 0) setCurrentIndex(index);
}

int PrinterComboBox::currentPrinter() const {
    return currentData().toInt();
}

/*************************************************************
 * CallOrderComboBox Implementation
 *************************************************************/
CallOrderComboBox::CallOrderComboBox(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("No Call"), 0);
    addItem(tr("First"), 1);
    addItem(tr("Second"), 2);
    addItem(tr("Third"), 3);
    addItem(tr("As Entree"), 4);
    addItem(tr("At Once"), 5);
}

void CallOrderComboBox::setCurrentCallOrder(int order) {
    int index = findData(order);
    if (index >= 0) setCurrentIndex(index);
}

int CallOrderComboBox::currentCallOrder() const {
    return currentData().toInt();
}

} // namespace vt
