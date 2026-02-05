/**
 * @file zone.cpp
 * @brief Zone implementation
 */

#include "ui/zone.hpp"
#include "ui/page.hpp"
#include "core/logger.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QJsonArray>
#include <magic_enum.hpp>

namespace vt2 {

Zone::Zone(QWidget* parent)
    : QWidget(parent) {
    initProperties();
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
}

Zone::Zone(ZoneType type, QWidget* parent)
    : QWidget(parent), type_(type) {
    initProperties();
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
}

Zone::~Zone() = default;

void Zone::setZoneName(const QString& name) {
    if (name_ != name) {
        name_ = name;
        emit zoneNameChanged(name);
    }
}

void Zone::setBackgroundColor(const QColor& color) {
    if (bgColor_ != color) {
        bgColor_ = color;
        update();
        emit backgroundColorChanged(color);
    }
}

void Zone::setForegroundColor(const QColor& color) {
    if (fgColor_ != color) {
        fgColor_ = color;
        update();
        emit foregroundColorChanged(color);
    }
}

void Zone::setBorderColor(const QColor& color) {
    if (borderColor_ != color) {
        borderColor_ = color;
        update();
        emit borderColorChanged(color);
    }
}

void Zone::setBorderWidth(int width) {
    if (borderWidth_ != width) {
        borderWidth_ = width;
        update();
        emit borderWidthChanged(width);
    }
}

void Zone::setBorderRadius(int radius) {
    if (borderRadius_ != radius) {
        borderRadius_ = radius;
        update();
    }
}

void Zone::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        update();
        emit selectedChanged(selected);
    }
}

QVariant Zone::property(const QString& name) const {
    for (const auto& prop : properties_) {
        if (prop.name == name) {
            return prop.value;
        }
    }
    return QVariant();
}

void Zone::setProperty(const QString& name, const QVariant& value) {
    for (auto& prop : properties_) {
        if (prop.name == name) {
            if (prop.value != value) {
                prop.value = value;
                emit propertyChanged(name, value);
                update();
            }
            return;
        }
    }
}

void Zone::addProperty(const ZoneProperty& prop) {
    properties_.push_back(prop);
}

void Zone::initProperties() {
    // Add standard properties that all zones have
    properties_ = {
        {"name", "Name", name_, "", "string", "Zone identifier", true, {}},
        {"x", "X Position", x(), 0, "int", "Horizontal position", true, {}},
        {"y", "Y Position", y(), 0, "int", "Vertical position", true, {}},
        {"width", "Width", width(), 100, "int", "Zone width", true, {}},
        {"height", "Height", height(), 50, "int", "Zone height", true, {}},
        {"bgColor", "Background", bgColor_.name(), "#336699", "color", "Background color", true, {}},
        {"fgColor", "Foreground", fgColor_.name(), "#FFFFFF", "color", "Text/foreground color", true, {}},
        {"borderColor", "Border Color", borderColor_.name(), "#343a40", "color", "Border color", true, {}},
        {"borderWidth", "Border Width", borderWidth_, 1, "int", "Border width in pixels", true, {}},
        {"borderRadius", "Border Radius", borderRadius_, 4, "int", "Corner radius", true, {}},
        {"enabled", "Enabled", isEnabled(), true, "bool", "Whether zone is active", true, {}},
    };
}

QJsonObject Zone::toJson() const {
    QJsonObject json;
    
    json["id"] = static_cast<int>(id_.value);
    json["name"] = name_;
    json["type"] = QString::fromStdString(std::string(magic_enum::enum_name(type_)));
    json["behavior"] = QString::fromStdString(std::string(magic_enum::enum_name(behavior_)));
    
    // Geometry
    QJsonObject geom;
    geom["x"] = x();
    geom["y"] = y();
    geom["width"] = width();
    geom["height"] = height();
    json["geometry"] = geom;
    
    // Visual properties
    QJsonObject visual;
    visual["bgColor"] = bgColor_.name();
    visual["fgColor"] = fgColor_.name();
    visual["borderColor"] = borderColor_.name();
    visual["borderWidth"] = borderWidth_;
    visual["borderRadius"] = borderRadius_;
    json["visual"] = visual;
    
    // Custom properties
    QJsonArray props;
    for (const auto& prop : properties_) {
        if (prop.name != "name" && prop.name != "x" && prop.name != "y" &&
            prop.name != "width" && prop.name != "height") {
            QJsonObject p;
            p["name"] = prop.name;
            p["value"] = QJsonValue::fromVariant(prop.value);
            props.append(p);
        }
    }
    if (!props.isEmpty()) {
        json["properties"] = props;
    }
    
    return json;
}

void Zone::fromJson(const QJsonObject& json) {
    if (json.contains("id")) {
        id_ = ZoneId{static_cast<uint32_t>(json["id"].toInt())};
    }
    
    if (json.contains("name")) {
        setZoneName(json["name"].toString());
    }
    
    // Geometry
    if (json.contains("geometry")) {
        auto geom = json["geometry"].toObject();
        setGeometry(
            geom["x"].toInt(),
            geom["y"].toInt(),
            geom["width"].toInt(),
            geom["height"].toInt()
        );
    }
    
    // Visual properties
    if (json.contains("visual")) {
        auto visual = json["visual"].toObject();
        if (visual.contains("bgColor")) {
            setBackgroundColor(QColor(visual["bgColor"].toString()));
        }
        if (visual.contains("fgColor")) {
            setForegroundColor(QColor(visual["fgColor"].toString()));
        }
        if (visual.contains("borderColor")) {
            setBorderColor(QColor(visual["borderColor"].toString()));
        }
        if (visual.contains("borderWidth")) {
            setBorderWidth(visual["borderWidth"].toInt());
        }
        if (visual.contains("borderRadius")) {
            setBorderRadius(visual["borderRadius"].toInt());
        }
    }
    
    // Custom properties
    if (json.contains("properties")) {
        auto props = json["properties"].toArray();
        for (const auto& p : props) {
            auto obj = p.toObject();
            setProperty(obj["name"].toString(), obj["value"].toVariant());
        }
    }
}

void Zone::executeAction() {
    if (action_) {
        action_();
    }
    emit touched();
}

void Zone::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Calculate background color based on state
    QColor bg = bgColor_;
    if (pressed_) {
        bg = bg.darker(120);
    } else if (hovered_) {
        bg = bg.lighter(110);
    }
    if (selected_) {
        bg = bg.lighter(130);
    }
    
    // Draw background with rounded corners
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(borderWidth_/2, borderWidth_/2, 
                                        -borderWidth_/2, -borderWidth_/2),
                        borderRadius_, borderRadius_);
    
    painter.fillPath(path, bg);
    
    // Draw border
    if (borderWidth_ > 0) {
        QPen pen(selected_ ? fgColor_ : borderColor_, borderWidth_);
        painter.setPen(pen);
        painter.drawPath(path);
    }
    
    // Let subclasses draw their content
    drawContent(painter);
}

void Zone::drawContent(QPainter& /*painter*/) {
    // Base class does nothing - subclasses override
}

void Zone::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pressed_ = true;
        update();
        emit pressed();
    }
    QWidget::mousePressEvent(event);
}

void Zone::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && pressed_) {
        pressed_ = false;
        update();
        emit released();
        
        if (rect().contains(event->pos())) {
            // Handle toggle behavior
            if (behavior_ == ZoneBehavior::Toggle) {
                setSelected(!selected_);
            }
            executeAction();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void Zone::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void Zone::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
}

void Zone::updateStyleSheet() {
    // For widgets that use Qt stylesheets
    QString style = QString(
        "background-color: %1;"
        "color: %2;"
        "border: %3px solid %4;"
        "border-radius: %5px;"
    ).arg(bgColor_.name())
     .arg(fgColor_.name())
     .arg(borderWidth_)
     .arg(borderColor_.name())
     .arg(borderRadius_);
    
    setStyleSheet(style);
}

// ============================================================================
// ZoneFactory
// ============================================================================

ZoneFactory& ZoneFactory::instance() {
    static ZoneFactory instance;
    return instance;
}

ZoneFactory::ZoneFactory() {
    // Register base zone type
    creators_[ZoneType::Button] = []() { return std::make_unique<Zone>(ZoneType::Button); };
}

std::unique_ptr<Zone> ZoneFactory::create(ZoneType type) {
    if (auto it = creators_.find(type); it != creators_.end()) {
        return it->second();
    }
    VT_WARN("Unknown zone type: {}", static_cast<int>(type));
    return std::make_unique<Zone>(type);
}

std::unique_ptr<Zone> ZoneFactory::createFromJson(const QJsonObject& json) {
    ZoneType type = ZoneType::Button;
    
    if (json.contains("type")) {
        auto typeName = json["type"].toString().toStdString();
        if (auto parsed = magic_enum::enum_cast<ZoneType>(typeName)) {
            type = *parsed;
        }
    }
    
    auto zone = create(type);
    zone->fromJson(json);
    return zone;
}

} // namespace vt2
