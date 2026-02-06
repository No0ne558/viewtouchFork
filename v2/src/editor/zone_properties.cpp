/*
 * ViewTouch V2 - Zone Properties Dialog Implementation
 */

#include "editor/zone_properties.hpp"
#include "zone/zone.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QDialogButtonBox>

namespace vt {

ZonePropertiesDialog::ZonePropertiesDialog(Zone* zone, QWidget* parent)
    : QDialog(parent)
    , zone_(zone)
{
    setWindowTitle(tr("Zone Properties"));
    setMinimumSize(450, 500);
    
    setupUi();
    loadFromZone();
}

ZonePropertiesDialog::~ZonePropertiesDialog() = default;

void ZonePropertiesDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    
    auto* tabWidget = new QTabWidget(this);
    
    // General tab
    auto* generalTab = new QWidget();
    setupGeneralTab();
    auto* generalLayout = new QFormLayout(generalTab);
    
    nameEdit_ = new QLineEdit();
    generalLayout->addRow(tr("Name:"), nameEdit_);
    
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
    
    groupSpinBox_ = new QSpinBox();
    groupSpinBox_->setRange(0, 999);
    generalLayout->addRow(tr("Group:"), groupSpinBox_);
    
    zoneTypeCombo_ = new ZoneTypeComboBox();
    generalLayout->addRow(tr("Zone Type:"), zoneTypeCombo_);
    
    tabWidget->addTab(generalTab, tr("General"));
    
    // Appearance tab
    auto* appearanceTab = new QWidget();
    setupAppearanceTab();
    auto* appearanceLayout = new QVBoxLayout(appearanceTab);
    
    // State tabs (Normal, Selected, Alternate)
    stateTabWidget_ = new QTabWidget();
    
    const QString stateNames[] = {tr("Normal"), tr("Selected"), tr("Alternate")};
    for (int i = 0; i < 3; ++i) {
        auto* stateWidget = new QWidget();
        auto* stateLayout = new QFormLayout(stateWidget);
        
        stateWidgets_[i].frameCombo = new FrameComboBox();
        stateLayout->addRow(tr("Frame:"), stateWidgets_[i].frameCombo);
        
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
    
    // Font and shape
    auto* fontShapeLayout = new QFormLayout();
    
    fontCombo_ = new FontComboBox();
    fontShapeLayout->addRow(tr("Font:"), fontCombo_);
    
    shapeCombo_ = new ShapeComboBox();
    fontShapeLayout->addRow(tr("Shape:"), shapeCombo_);
    
    shadowSpinBox_ = new QSpinBox();
    shadowSpinBox_->setRange(0, 1024);
    fontShapeLayout->addRow(tr("Shadow:"), shadowSpinBox_);
    
    appearanceLayout->addLayout(fontShapeLayout);
    
    tabWidget->addTab(appearanceTab, tr("Appearance"));
    
    // Behavior tab
    auto* behaviorTab = new QWidget();
    setupBehaviorTab();
    auto* behaviorLayout = new QFormLayout(behaviorTab);
    
    behaviorCombo_ = new BehaviorComboBox();
    behaviorLayout->addRow(tr("Behavior:"), behaviorCombo_);
    
    activeCheck_ = new QCheckBox(tr("Active"));
    behaviorLayout->addRow(activeCheck_);
    
    editCheck_ = new QCheckBox(tr("Editable"));
    behaviorLayout->addRow(editCheck_);
    
    stayLitCheck_ = new QCheckBox(tr("Stay Lit"));
    behaviorLayout->addRow(stayLitCheck_);
    
    keySpinBox_ = new QSpinBox();
    keySpinBox_->setRange(0, 255);
    behaviorLayout->addRow(tr("Key Code:"), keySpinBox_);
    
    tabWidget->addTab(behaviorTab, tr("Behavior"));
    
    mainLayout->addWidget(tabWidget);
    
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

void ZonePropertiesDialog::loadFromZone() {
    if (!zone_) return;
    
    // General
    nameEdit_->setText(zone_->name());
    xSpinBox_->setValue(zone_->x());
    ySpinBox_->setValue(zone_->y());
    widthSpinBox_->setValue(zone_->w());
    heightSpinBox_->setValue(zone_->h());
    groupSpinBox_->setValue(zone_->groupId());
    zoneTypeCombo_->setCurrentZoneType(zone_->zoneType());
    
    // Appearance - states
    for (int i = 0; i < 3; ++i) {
        const ZoneState& st = zone_->state(i);
        stateWidgets_[i].frameCombo->setCurrentFrame(st.frame);
        stateWidgets_[i].textureCombo->setCurrentTextureId(st.texture);
        stateWidgets_[i].colorCombo->setCurrentColorId(st.color);
    }
    
    fontCombo_->setCurrentFontId(zone_->font());
    shapeCombo_->setCurrentShape(zone_->shape());
    shadowSpinBox_->setValue(zone_->shadow());
    
    // Behavior
    behaviorCombo_->setCurrentBehavior(zone_->behavior());
    activeCheck_->setChecked(zone_->isActive());
    editCheck_->setChecked(zone_->isEdit());
    stayLitCheck_->setChecked(zone_->stayLit());
    keySpinBox_->setValue(zone_->key());
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
    
    zone_->setFont(fontCombo_->currentFontId());
    zone_->setShape(shapeCombo_->currentShape());
    zone_->setShadow(shadowSpinBox_->value());
    
    // Behavior
    zone_->setBehavior(behaviorCombo_->currentBehavior());
    zone_->setActive(activeCheck_->isChecked());
    zone_->setEdit(editCheck_->isChecked());
    zone_->setStayLit(stayLitCheck_->isChecked());
    zone_->setKey(keySpinBox_->value());
}

void ZonePropertiesDialog::applyChanges() {
    saveToZone();
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

} // namespace vt
