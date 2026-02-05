/**
 * @file button_zone.cpp
 * @brief Button zone implementation
 */

#include "zones/button_zone.hpp"
#include "core/application.hpp"
#include "core/logger.hpp"
#include <QPainter>
#include <QFontMetrics>
#include <magic_enum.hpp>

namespace vt2 {

ButtonZone::ButtonZone(QWidget* parent)
    : Zone(ZoneType::Button, parent) {
    updateFont();
    initProperties();
}

ButtonZone::~ButtonZone() = default;

void ButtonZone::setText(const QString& text) {
    if (text_ != text) {
        text_ = text;
        update();
        emit textChanged(text);
    }
}

void ButtonZone::setIconPath(const QString& path) {
    if (iconPath_ != path) {
        iconPath_ = path;
        if (!path.isEmpty()) {
            icon_.load(path);
            scaledIcon_ = QPixmap();  // Will be recalculated
        } else {
            icon_ = QPixmap();
            scaledIcon_ = QPixmap();
        }
        update();
        emit iconPathChanged(path);
    }
}

void ButtonZone::setIcon(const QPixmap& icon) {
    icon_ = icon;
    scaledIcon_ = QPixmap();
    update();
}

void ButtonZone::setFontSize(FontSize size) {
    if (fontSize_ != size) {
        fontSize_ = size;
        updateFont();
        update();
    }
}

void ButtonZone::setFontWeight(FontWeight weight) {
    if (fontWeight_ != weight) {
        fontWeight_ = weight;
        updateFont();
        update();
    }
}

void ButtonZone::setFont(const QFont& font) {
    font_ = font;
    update();
}

void ButtonZone::setAlignment(HAlign h, VAlign v) {
    hAlign_ = h;
    vAlign_ = v;
    update();
}

void ButtonZone::updateFont() {
    font_ = QFont("Liberation Sans", static_cast<int>(fontSize_));
    
    switch (fontWeight_) {
        case FontWeight::Light:
            font_.setWeight(QFont::Light);
            break;
        case FontWeight::Normal:
            font_.setWeight(QFont::Normal);
            break;
        case FontWeight::Medium:
            font_.setWeight(QFont::Medium);
            break;
        case FontWeight::Bold:
            font_.setWeight(QFont::Bold);
            break;
        case FontWeight::Heavy:
            font_.setWeight(QFont::Black);
            break;
    }
}

void ButtonZone::initProperties() {
    Zone::initProperties();
    
    // Add button-specific properties
    addProperty({"text", "Text", text_, "", "string", "Button label text", true, {}});
    addProperty({"iconPath", "Icon", iconPath_, "", "file", "Path to icon image", true, {}});
    addProperty({"fontSize", "Font Size", static_cast<int>(fontSize_), 12, "enum", "Text size", true,
        QVariantList{"Tiny", "Small", "Normal", "Medium", "Large", "XLarge", "Huge", "Giant"}});
    addProperty({"fontWeight", "Font Weight", static_cast<int>(fontWeight_), 1, "enum", "Text weight", true,
        QVariantList{"Light", "Normal", "Medium", "Bold", "Heavy"}});
    addProperty({"hAlign", "H Align", static_cast<int>(hAlign_), 1, "enum", "Horizontal alignment", true,
        QVariantList{"Left", "Center", "Right"}});
    addProperty({"vAlign", "V Align", static_cast<int>(vAlign_), 1, "enum", "Vertical alignment", true,
        QVariantList{"Top", "Center", "Bottom"}});
    addProperty({"jumpPage", "Jump To", 0, 0, "page", "Page to navigate to on click", true, {}});
}

void ButtonZone::drawContent(QPainter& painter) {
    QRect contentRect = rect().adjusted(padding_, padding_, -padding_, -padding_);
    
    // Draw icon if present
    QRect textRect = contentRect;
    if (!icon_.isNull()) {
        // Scale icon if needed
        int maxIconSize = qMin(contentRect.height() - 4, 64);
        if (scaledIcon_.isNull() || scaledIcon_.height() != maxIconSize) {
            scaledIcon_ = icon_.scaledToHeight(maxIconSize, Qt::SmoothTransformation);
        }
        
        // Position icon
        int iconX, iconY;
        if (text_.isEmpty()) {
            // Center icon if no text
            iconX = contentRect.x() + (contentRect.width() - scaledIcon_.width()) / 2;
            iconY = contentRect.y() + (contentRect.height() - scaledIcon_.height()) / 2;
        } else {
            // Icon on left, text on right
            iconX = contentRect.x();
            iconY = contentRect.y() + (contentRect.height() - scaledIcon_.height()) / 2;
            textRect.setLeft(iconX + scaledIcon_.width() + padding_);
        }
        
        painter.drawPixmap(iconX, iconY, scaledIcon_);
    }
    
    // Draw text
    if (!text_.isEmpty()) {
        painter.setFont(font_);
        painter.setPen(fgColor_);
        
        // Calculate alignment flags
        int flags = Qt::TextWordWrap;
        switch (hAlign_) {
            case HAlign::Left:   flags |= Qt::AlignLeft;   break;
            case HAlign::Center: flags |= Qt::AlignHCenter; break;
            case HAlign::Right:  flags |= Qt::AlignRight;  break;
        }
        switch (vAlign_) {
            case VAlign::Top:    flags |= Qt::AlignTop;    break;
            case VAlign::Center: flags |= Qt::AlignVCenter; break;
            case VAlign::Bottom: flags |= Qt::AlignBottom; break;
        }
        
        painter.drawText(textRect, flags, text_);
    }
}

QJsonObject ButtonZone::toJson() const {
    QJsonObject json = Zone::toJson();
    
    json["text"] = text_;
    if (!iconPath_.isEmpty()) {
        json["iconPath"] = iconPath_;
    }
    json["fontSize"] = QString::fromStdString(std::string(magic_enum::enum_name(fontSize_)));
    json["fontWeight"] = QString::fromStdString(std::string(magic_enum::enum_name(fontWeight_)));
    json["hAlign"] = QString::fromStdString(std::string(magic_enum::enum_name(hAlign_)));
    json["vAlign"] = QString::fromStdString(std::string(magic_enum::enum_name(vAlign_)));
    
    if (jumpPageId_) {
        json["jumpPage"] = static_cast<int>(jumpPageId_->value);
    }
    
    return json;
}

void ButtonZone::fromJson(const QJsonObject& json) {
    Zone::fromJson(json);
    
    if (json.contains("text")) {
        setText(json["text"].toString());
    }
    
    if (json.contains("iconPath")) {
        setIconPath(json["iconPath"].toString());
    }
    
    if (json.contains("fontSize")) {
        auto name = json["fontSize"].toString().toStdString();
        if (auto parsed = magic_enum::enum_cast<FontSize>(name)) {
            setFontSize(*parsed);
        }
    }
    
    if (json.contains("fontWeight")) {
        auto name = json["fontWeight"].toString().toStdString();
        if (auto parsed = magic_enum::enum_cast<FontWeight>(name)) {
            setFontWeight(*parsed);
        }
    }
    
    if (json.contains("hAlign")) {
        auto name = json["hAlign"].toString().toStdString();
        if (auto parsed = magic_enum::enum_cast<HAlign>(name)) {
            hAlign_ = *parsed;
        }
    }
    
    if (json.contains("vAlign")) {
        auto name = json["vAlign"].toString().toStdString();
        if (auto parsed = magic_enum::enum_cast<VAlign>(name)) {
            vAlign_ = *parsed;
        }
    }
    
    if (json.contains("jumpPage")) {
        jumpPageId_ = PageId{static_cast<uint32_t>(json["jumpPage"].toInt())};
    }
}

} // namespace vt2
