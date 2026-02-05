/**
 * @file property_editor.cpp
 */

#include "ui/property_editor.hpp"
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QPushButton>
#include <QLabel>

namespace vt2 {

PropertyEditor::PropertyEditor(QWidget* parent)
    : QWidget(parent) {
    layout_ = new QFormLayout(this);
    setLayout(layout_);
}

PropertyEditor::~PropertyEditor() = default;

void PropertyEditor::setZone(Zone* zone) {
    if (zone_ == zone) return;
    
    zone_ = zone;
    buildUI();
}

void PropertyEditor::clear() {
    while (layout_->count() > 0) {
        auto item = layout_->takeAt(0);
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }
}

void PropertyEditor::buildUI() {
    clear();
    
    if (!zone_) {
        layout_->addRow(new QLabel("No zone selected"));
        return;
    }
    
    layout_->addRow(new QLabel(QString("<b>%1</b>").arg(zone_->zoneName())));
    
    for (const auto& prop : zone_->properties()) {
        if (!prop.editable) continue;
        
        QWidget* editor = nullptr;
        
        if (prop.type == "string") {
            auto* lineEdit = new QLineEdit(prop.value.toString());
            connect(lineEdit, &QLineEdit::textChanged, this, [this, name = prop.name](const QString& text) {
                if (zone_) {
                    zone_->setProperty(name, text);
                    emit propertyChanged(name, text);
                }
            });
            editor = lineEdit;
        }
        else if (prop.type == "int") {
            auto* spinBox = new QSpinBox();
            spinBox->setRange(-10000, 10000);
            spinBox->setValue(prop.value.toInt());
            connect(spinBox, &QSpinBox::valueChanged, this, [this, name = prop.name](int value) {
                if (zone_) {
                    zone_->setProperty(name, value);
                    emit propertyChanged(name, value);
                }
            });
            editor = spinBox;
        }
        else if (prop.type == "bool") {
            auto* checkBox = new QCheckBox();
            checkBox->setChecked(prop.value.toBool());
            connect(checkBox, &QCheckBox::toggled, this, [this, name = prop.name](bool checked) {
                if (zone_) {
                    zone_->setProperty(name, checked);
                    emit propertyChanged(name, checked);
                }
            });
            editor = checkBox;
        }
        else if (prop.type == "color") {
            auto* colorBtn = new QPushButton();
            QColor color(prop.value.toString());
            colorBtn->setStyleSheet(QString("background-color: %1").arg(color.name()));
            connect(colorBtn, &QPushButton::clicked, this, [this, colorBtn, name = prop.name]() {
                QColor color = QColorDialog::getColor(Qt::white, this, "Select Color");
                if (color.isValid() && zone_) {
                    colorBtn->setStyleSheet(QString("background-color: %1").arg(color.name()));
                    zone_->setProperty(name, color.name());
                    emit propertyChanged(name, color.name());
                }
            });
            editor = colorBtn;
        }
        else if (prop.type == "enum" && !prop.options.isEmpty()) {
            auto* comboBox = new QComboBox();
            for (const auto& opt : prop.options) {
                comboBox->addItem(opt.toString());
            }
            comboBox->setCurrentIndex(prop.value.toInt());
            connect(comboBox, &QComboBox::currentIndexChanged, this, [this, name = prop.name](int index) {
                if (zone_) {
                    zone_->setProperty(name, index);
                    emit propertyChanged(name, index);
                }
            });
            editor = comboBox;
        }
        
        if (editor) {
            layout_->addRow(prop.displayName, editor);
        }
    }
}

} // namespace vt2
