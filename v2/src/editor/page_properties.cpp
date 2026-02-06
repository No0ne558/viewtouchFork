/*
 * ViewTouch V2 - Page Properties Dialog Implementation
 */

#include "editor/page_properties.hpp"
#include "editor/zone_properties.hpp"
#include "zone/page.hpp"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>

namespace vt {

PagePropertiesDialog::PagePropertiesDialog(Page* page, QWidget* parent)
    : QDialog(parent)
    , page_(page)
{
    setWindowTitle(tr("Page Properties"));
    setMinimumSize(350, 400);
    
    setupUi();
    loadFromPage();
}

PagePropertiesDialog::~PagePropertiesDialog() = default;

void PagePropertiesDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout();
    
    idSpinBox_ = new QSpinBox();
    idSpinBox_->setRange(-9999, 9999);  // Allow negative IDs for system pages
    formLayout->addRow(tr("Page ID:"), idSpinBox_);
    
    nameEdit_ = new QLineEdit();
    formLayout->addRow(tr("Name:"), nameEdit_);
    
    typeCombo_ = new PageTypeComboBox();
    formLayout->addRow(tr("Type:"), typeCombo_);
    
    auto* sizeLayout = new QHBoxLayout();
    widthSpinBox_ = new QSpinBox();
    widthSpinBox_->setRange(100, 9999);
    sizeLayout->addWidget(new QLabel(tr("W:")));
    sizeLayout->addWidget(widthSpinBox_);
    heightSpinBox_ = new QSpinBox();
    heightSpinBox_->setRange(100, 9999);
    sizeLayout->addWidget(new QLabel(tr("H:")));
    sizeLayout->addWidget(heightSpinBox_);
    formLayout->addRow(tr("Size:"), sizeLayout);
    
    parentIdSpinBox_ = new QSpinBox();
    parentIdSpinBox_->setRange(-9999, 9999);  // Allow negative parent page IDs
    formLayout->addRow(tr("Parent ID:"), parentIdSpinBox_);
    
    indexSpinBox_ = new QSpinBox();
    indexSpinBox_->setRange(0, 9999);
    formLayout->addRow(tr("Index:"), indexSpinBox_);
    
    defaultTextureCombo_ = new TextureComboBox();
    formLayout->addRow(tr("Default Texture:"), defaultTextureCombo_);
    
    defaultColorCombo_ = new ColorComboBox();
    formLayout->addRow(tr("Default Color:"), defaultColorCombo_);
    
    titleColorCombo_ = new ColorComboBox();
    formLayout->addRow(tr("Title Color:"), titleColorCombo_);
    
    mainLayout->addLayout(formLayout);
    mainLayout->addStretch();
    
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PagePropertiesDialog::onOk);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &PagePropertiesDialog::onApply);
    mainLayout->addWidget(buttonBox);
}

void PagePropertiesDialog::loadFromPage() {
    if (!page_) return;
    
    idSpinBox_->setValue(page_->id());
    nameEdit_->setText(page_->name());
    static_cast<PageTypeComboBox*>(typeCombo_)->setCurrentPageType(page_->type());
    widthSpinBox_->setValue(page_->width());
    heightSpinBox_->setValue(page_->height());
    parentIdSpinBox_->setValue(page_->parentId());
    indexSpinBox_->setValue(page_->index());
    static_cast<TextureComboBox*>(defaultTextureCombo_)->setCurrentTextureId(page_->defaultTexture());
    static_cast<ColorComboBox*>(defaultColorCombo_)->setCurrentColorId(page_->defaultColor());
    static_cast<ColorComboBox*>(titleColorCombo_)->setCurrentColorId(page_->titleColor());
}

void PagePropertiesDialog::saveToPage() {
    if (!page_) return;
    
    page_->setId(idSpinBox_->value());
    page_->setName(nameEdit_->text());
    page_->setType(static_cast<PageTypeComboBox*>(typeCombo_)->currentPageType());
    page_->setSize(widthSpinBox_->value(), heightSpinBox_->value());
    page_->setParentId(parentIdSpinBox_->value());
    page_->setIndex(indexSpinBox_->value());
    page_->setDefaultTexture(static_cast<TextureComboBox*>(defaultTextureCombo_)->currentTextureId());
    page_->setDefaultColor(static_cast<ColorComboBox*>(defaultColorCombo_)->currentColorId());
    page_->setTitleColor(static_cast<ColorComboBox*>(titleColorCombo_)->currentColorId());
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

} // namespace vt
