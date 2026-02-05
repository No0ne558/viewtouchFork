/**
 * @file page.cpp
 * @brief Page implementation
 */

#include "ui/page.hpp"
#include "zones/button_zone.hpp"
#include "core/logger.hpp"
#include <QPainter>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <magic_enum.hpp>

namespace vt2 {

Page::Page(QWidget* parent)
    : QWidget(parent) {
    setAutoFillBackground(false);
}

Page::Page(PageType type, QWidget* parent)
    : QWidget(parent), type_(type) {
    setAutoFillBackground(false);
}

Page::~Page() = default;

void Page::setPageName(const QString& name) {
    if (name_ != name) {
        name_ = name;
        emit pageNameChanged(name);
    }
}

void Page::setBackgroundColor(const QColor& color) {
    bgColor_ = color;
    update();
}

void Page::setBackgroundImage(const QString& path) {
    bgImagePath_ = path;
    if (!path.isEmpty()) {
        bgImage_.load(path);
    } else {
        bgImage_ = QPixmap();
    }
    update();
}

void Page::addZone(std::unique_ptr<Zone> zone) {
    if (!zone) return;
    
    zone->setParent(this);
    zone->setPage(this);
    
    if (zone->id().value == 0) {
        zone->setId(ZoneId{nextZoneId_++});
    }
    
    zone->show();
    
    Zone* zonePtr = zone.get();
    zones_.push_back(std::move(zone));
    
    emit zoneAdded(zonePtr);
    VT_DEBUG("Zone added to page '{}': {} (id={})", 
             name_.toStdString(), zonePtr->zoneName().toStdString(), zonePtr->id().value);
}

void Page::addZone(std::unique_ptr<Zone> zone, int x, int y, int w, int h) {
    if (!zone) return;
    
    zone->setGeometry(x, y, w, h);
    addZone(std::move(zone));
}

void Page::removeZone(ZoneId id) {
    auto it = std::find_if(zones_.begin(), zones_.end(),
        [id](const auto& z) { return z->id() == id; });
    
    if (it != zones_.end()) {
        emit zoneRemoved(id);
        zones_.erase(it);
        VT_DEBUG("Zone removed from page '{}': id={}", name_.toStdString(), id.value);
    }
}

Zone* Page::zone(ZoneId id) {
    auto it = std::find_if(zones_.begin(), zones_.end(),
        [id](const auto& z) { return z->id() == id; });
    return it != zones_.end() ? it->get() : nullptr;
}

const Zone* Page::zone(ZoneId id) const {
    auto it = std::find_if(zones_.begin(), zones_.end(),
        [id](const auto& z) { return z->id() == id; });
    return it != zones_.end() ? it->get() : nullptr;
}

Zone* Page::zone(const QString& name) {
    auto it = std::find_if(zones_.begin(), zones_.end(),
        [&name](const auto& z) { return z->zoneName() == name; });
    return it != zones_.end() ? it->get() : nullptr;
}

const Zone* Page::zone(const QString& name) const {
    auto it = std::find_if(zones_.begin(), zones_.end(),
        [&name](const auto& z) { return z->zoneName() == name; });
    return it != zones_.end() ? it->get() : nullptr;
}

void Page::clearZones() {
    zones_.clear();
    VT_DEBUG("All zones cleared from page '{}'", name_.toStdString());
}

Zone* Page::zoneAt(const QPoint& pos) {
    // Search in reverse order (top-most first)
    for (auto it = zones_.rbegin(); it != zones_.rend(); ++it) {
        if ((*it)->geometry().contains(pos) && (*it)->isVisible()) {
            return it->get();
        }
    }
    return nullptr;
}

void Page::createButtonGrid(int rows, int cols, 
                            const QStringList& labels,
                            int startX, int startY,
                            int buttonWidth, int buttonHeight,
                            int spacing) {
    int labelIndex = 0;
    
    for (int row = 0; row < rows && labelIndex < labels.size(); ++row) {
        for (int col = 0; col < cols && labelIndex < labels.size(); ++col) {
            auto button = std::make_unique<ButtonZone>();
            button->setText(labels[labelIndex]);
            button->setZoneName(QString("btn_%1_%2").arg(row).arg(col));
            
            int x = startX + col * (buttonWidth + spacing);
            int y = startY + row * (buttonHeight + spacing);
            
            addZone(std::move(button), x, y, buttonWidth, buttonHeight);
            ++labelIndex;
        }
    }
}

QJsonObject Page::toJson() const {
    QJsonObject json;
    
    json["id"] = static_cast<int>(id_.value);
    json["name"] = name_;
    json["type"] = QString::fromStdString(std::string(magic_enum::enum_name(type_)));
    json["bgColor"] = bgColor_.name();
    
    if (!bgImagePath_.isEmpty()) {
        json["bgImage"] = bgImagePath_;
    }
    
    // Serialize zones
    QJsonArray zonesArray;
    for (const auto& zone : zones_) {
        zonesArray.append(zone->toJson());
    }
    json["zones"] = zonesArray;
    
    return json;
}

void Page::fromJson(const QJsonObject& json) {
    if (json.contains("id")) {
        id_ = PageId{static_cast<uint32_t>(json["id"].toInt())};
    }
    
    if (json.contains("name")) {
        setPageName(json["name"].toString());
    }
    
    if (json.contains("type")) {
        auto typeName = json["type"].toString().toStdString();
        if (auto parsed = magic_enum::enum_cast<PageType>(typeName)) {
            type_ = *parsed;
        }
    }
    
    if (json.contains("bgColor")) {
        setBackgroundColor(QColor(json["bgColor"].toString()));
    }
    
    if (json.contains("bgImage")) {
        setBackgroundImage(json["bgImage"].toString());
    }
    
    // Load zones
    if (json.contains("zones")) {
        clearZones();
        auto zonesArray = json["zones"].toArray();
        for (const auto& zoneVal : zonesArray) {
            auto zone = ZoneFactory::instance().createFromJson(zoneVal.toObject());
            addZone(std::move(zone));
        }
    }
}

bool Page::saveToFile(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        VT_ERROR("Failed to open file for writing: {}", path.toStdString());
        return false;
    }
    
    QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    
    VT_INFO("Page saved to: {}", path.toStdString());
    return true;
}

bool Page::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        VT_ERROR("Failed to open file for reading: {}", path.toStdString());
        return false;
    }
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    
    if (error.error != QJsonParseError::NoError) {
        VT_ERROR("JSON parse error: {}", error.errorString().toStdString());
        return false;
    }
    
    fromJson(doc.object());
    
    VT_INFO("Page loaded from: {}", path.toStdString());
    return true;
}

void Page::onEnter() {
    VT_DEBUG("Entering page: {}", name_.toStdString());
    refresh();
}

void Page::onExit() {
    VT_DEBUG("Exiting page: {}", name_.toStdString());
}

void Page::refresh() {
    // Base implementation just updates display
    update();
}

void Page::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    
    // Draw background color
    painter.fillRect(rect(), bgColor_);
    
    // Draw background image if set
    if (!bgImage_.isNull()) {
        painter.drawPixmap(rect(), bgImage_);
    }
}

void Page::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    
    // Could implement auto-layout here
    // For now, zones maintain their absolute positions
}

// ============================================================================
// PageFactory
// ============================================================================

PageFactory& PageFactory::instance() {
    static PageFactory instance;
    return instance;
}

std::unique_ptr<Page> PageFactory::create(PageType type) {
    return std::make_unique<Page>(type);
}

std::unique_ptr<Page> PageFactory::createFromJson(const QJsonObject& json) {
    auto page = std::make_unique<Page>();
    page->fromJson(json);
    return page;
}

std::unique_ptr<Page> PageFactory::loadFromFile(const QString& path) {
    auto page = std::make_unique<Page>();
    if (page->loadFromFile(path)) {
        return page;
    }
    return nullptr;
}

} // namespace vt2
