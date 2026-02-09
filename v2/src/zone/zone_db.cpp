/*
 * ViewTouch V2 - Zone Database Implementation
 */

#include "zone/zone_db.hpp"

#include <QFile>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

namespace vt {

ZoneDB::ZoneDB() = default;
ZoneDB::~ZoneDB() = default;

void ZoneDB::addPage(std::unique_ptr<Page> page) {
    if (page) {
        int id = page->id();
        if (id == 0) {
            // Auto-assign next user page ID
            id = nextUserPageId_++;
            page->setId(id);
        } else if (id > 0 && id >= nextUserPageId_) {
            // Update next ID if we're adding a page with a higher ID
            nextUserPageId_ = id + 1;
        }
        // System pages (negative IDs) are added as-is
        pages_[id] = std::move(page);
    }
}

void ZoneDB::removePage(int id) {
    pages_.erase(id);
}

void ZoneDB::clear() {
    pages_.clear();
    nextUserPageId_ = 1;
}

Page* ZoneDB::createPage(const QString& name, PageType type) {
    auto page = std::make_unique<Page>();
    page->setId(nextUserPageId_++);
    page->setName(name);
    page->setType(type);
    
    Page* ptr = page.get();
    pages_[ptr->id()] = std::move(page);
    return ptr;
}

Page* ZoneDB::createSystemPage(int id, const QString& name, PageType type) {
    if (id >= 0) {
        return nullptr;  // System pages must have negative IDs
    }
    
    // Check if already exists
    if (pages_.find(id) != pages_.end()) {
        return pages_[id].get();
    }
    
    auto page = std::make_unique<Page>();
    page->setId(id);
    page->setName(name);
    page->setType(type);
    
    Page* ptr = page.get();
    pages_[id] = std::move(page);
    return ptr;
}

void ZoneDB::initSystemPages() {
    // Start with 0 pages — system pages will be created as needed
}

Page* ZoneDB::page(int id) {
    auto it = pages_.find(id);
    if (it != pages_.end()) {
        return it->second.get();
    }
    return nullptr;
}

const Page* ZoneDB::page(int id) const {
    auto it = pages_.find(id);
    if (it != pages_.end()) {
        return it->second.get();
    }
    return nullptr;
}

Page* ZoneDB::pageByName(const QString& name) {
    for (auto& [id, p] : pages_) {
        if (p->name() == name) {
            return p.get();
        }
    }
    return nullptr;
}

std::vector<Page*> ZoneDB::pagesByType(PageType type) {
    std::vector<Page*> result;
    for (auto& [id, p] : pages_) {
        if (p->type() == type) {
            result.push_back(p.get());
        }
    }
    return result;
}

std::vector<Page*> ZoneDB::systemPages() {
    std::vector<Page*> result;
    for (auto& [id, p] : pages_) {
        if (isSystemPage(id)) {
            result.push_back(p.get());
        }
    }
    return result;
}

std::vector<Page*> ZoneDB::userPages() {
    std::vector<Page*> result;
    for (auto& [id, p] : pages_) {
        if (!isSystemPage(id)) {
            result.push_back(p.get());
        }
    }
    return result;
}

size_t ZoneDB::systemPageCount() const {
    size_t count = 0;
    for (const auto& [id, p] : pages_) {
        if (isSystemPage(id)) {
            ++count;
        }
    }
    return count;
}

size_t ZoneDB::userPageCount() const {
    size_t count = 0;
    for (const auto& [id, p] : pages_) {
        if (!isSystemPage(id)) {
            ++count;
        }
    }
    return count;
}

std::vector<int> ZoneDB::pageIds() const {
    std::vector<int> ids;
    ids.reserve(pages_.size());
    for (const auto& [id, p] : pages_) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<int> ZoneDB::systemPageIds() const {
    std::vector<int> ids;
    for (const auto& [id, p] : pages_) {
        if (isSystemPage(id)) {
            ids.push_back(id);
        }
    }
    return ids;
}

std::vector<int> ZoneDB::userPageIds() const {
    std::vector<int> ids;
    for (const auto& [id, p] : pages_) {
        if (!isSystemPage(id)) {
            ids.push_back(id);
        }
    }
    return ids;
}

bool ZoneDB::loadUi(const QString& filename) {
    QString filepath = filename;
    if (!dataDir_.isEmpty() && !QDir::isAbsolutePath(filename)) {
        filepath = dataDir_ + "/" + filename;
    }
    
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }
    
    QJsonObject root = doc.object();
    
    // Read metadata
    int version = root["version"].toInt(1);
    nextUserPageId_ = root["nextUserPageId"].toInt(1);
    
    // Read pages
    QJsonArray pagesArray = root["pages"].toArray();
    for (const QJsonValue& pageVal : pagesArray) {
        QJsonObject pageObj = pageVal.toObject();
        
        auto page = std::make_unique<Page>();
        page->setId(pageObj["id"].toInt());
        page->setName(pageObj["name"].toString());
        page->setType(static_cast<PageType>(pageObj["type"].toInt()));
        page->setParentId(pageObj["parentId"].toInt());
        page->setIndex(pageObj["index"].toInt());
        page->setSize(pageObj["width"].toInt(1024), pageObj["height"].toInt(768));
        page->setDefaultTexture(static_cast<uint8_t>(pageObj["defaultTexture"].toInt()));
        page->setDefaultColor(static_cast<uint8_t>(pageObj["defaultColor"].toInt()));
        page->setTitleColor(static_cast<uint8_t>(pageObj["titleColor"].toInt()));
        
        // Read zones
        QJsonArray zonesArray = pageObj["zones"].toArray();
        for (const QJsonValue& zoneVal : zonesArray) {
            QJsonObject zoneObj = zoneVal.toObject();
            
            auto zone = std::make_unique<Zone>();
            zone->setName(zoneObj["name"].toString());
            zone->setRegion(zoneObj["x"].toInt(), zoneObj["y"].toInt(),
                          zoneObj["w"].toInt(), zoneObj["h"].toInt());
            zone->setGroupId(zoneObj["groupId"].toInt());
            zone->setZoneType(static_cast<ZoneType>(zoneObj["zoneType"].toInt()));
            zone->setBehavior(static_cast<ZoneBehavior>(zoneObj["behavior"].toInt()));
            zone->setFont(static_cast<FontId>(zoneObj["font"].toInt()));
            zone->setShape(static_cast<ZoneShape>(zoneObj["shape"].toInt()));
            zone->setShadow(zoneObj["shadow"].toInt());
            zone->setKey(zoneObj["key"].toInt());
            zone->setActive(zoneObj["active"].toBool(true));
            zone->setEdit(zoneObj["edit"].toBool(false));
            zone->setStayLit(zoneObj["stayLit"].toBool(false));
            
            // Read states
            QJsonArray statesArray = zoneObj["states"].toArray();
            for (int i = 0; i < 3 && i < statesArray.size(); ++i) {
                QJsonObject stateObj = statesArray[i].toObject();
                ZoneState st;
                st.frame = static_cast<ZoneFrame>(stateObj["frame"].toInt());
                st.texture = static_cast<uint8_t>(stateObj["texture"].toInt());
                st.color = static_cast<uint8_t>(stateObj["color"].toInt());
                st.image = static_cast<uint8_t>(stateObj["image"].toInt());
                zone->setState(i, st);
            }
            
            page->addZone(std::move(zone));
        }
        
        pages_[page->id()] = std::move(page);
    }
    
    return true;
}

bool ZoneDB::saveUi(const QString& filename) const {
    QString filepath = filename;
    if (!dataDir_.isEmpty() && !QDir::isAbsolutePath(filename)) {
        // Ensure data directory exists
        QDir().mkpath(dataDir_);
        filepath = dataDir_ + "/" + filename;
    }
    
    QJsonObject root;
    root["version"] = 1;
    root["nextUserPageId"] = nextUserPageId_;
    
    QJsonArray pagesArray;
    for (const auto& [id, page] : pages_) {
        QJsonObject pageObj;
        pageObj["id"] = page->id();
        pageObj["name"] = page->name();
        pageObj["type"] = static_cast<int>(page->type());
        pageObj["parentId"] = page->parentId();
        pageObj["index"] = page->index();
        pageObj["width"] = page->width();
        pageObj["height"] = page->height();
        pageObj["defaultTexture"] = page->defaultTexture();
        pageObj["defaultColor"] = page->defaultColor();
        pageObj["titleColor"] = page->titleColor();
        
        // Write zones
        QJsonArray zonesArray;
        for (size_t i = 0; i < page->zoneCount(); ++i) {
            const Zone* zone = page->zone(i);
            QJsonObject zoneObj;
            zoneObj["name"] = zone->name();
            zoneObj["x"] = zone->x();
            zoneObj["y"] = zone->y();
            zoneObj["w"] = zone->w();
            zoneObj["h"] = zone->h();
            zoneObj["groupId"] = zone->groupId();
            zoneObj["zoneType"] = static_cast<int>(zone->zoneType());
            zoneObj["behavior"] = static_cast<int>(zone->behavior());
            zoneObj["font"] = static_cast<int>(zone->font());
            zoneObj["shape"] = static_cast<int>(zone->shape());
            zoneObj["shadow"] = zone->shadow();
            zoneObj["key"] = zone->key();
            zoneObj["active"] = zone->isActive();
            zoneObj["edit"] = zone->isEdit();
            zoneObj["stayLit"] = zone->stayLit();
            
            // Write states
            QJsonArray statesArray;
            for (int s = 0; s < 3; ++s) {
                const ZoneState& st = zone->state(s);
                QJsonObject stateObj;
                stateObj["frame"] = static_cast<int>(st.frame);
                stateObj["texture"] = st.texture;
                stateObj["color"] = st.color;
                stateObj["image"] = st.image;
                statesArray.append(stateObj);
            }
            zoneObj["states"] = statesArray;
            
            zonesArray.append(zoneObj);
        }
        pageObj["zones"] = zonesArray;
        
        pagesArray.append(pageObj);
    }
    root["pages"] = pagesArray;
    
    QJsonDocument doc(root);
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

} // namespace vt
