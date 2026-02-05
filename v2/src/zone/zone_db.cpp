/*
 * ViewTouch V2 - Zone Database Implementation
 */

#include "zone/zone_db.hpp"

#include <QFile>
#include <QDataStream>

namespace vt {

ZoneDB::ZoneDB() = default;
ZoneDB::~ZoneDB() = default;

void ZoneDB::addPage(std::unique_ptr<Page> page) {
    if (page) {
        int id = page->id();
        if (id == 0) {
            id = nextPageId_++;
            page->setId(id);
        } else if (id >= nextPageId_) {
            nextPageId_ = id + 1;
        }
        pages_[id] = std::move(page);
    }
}

void ZoneDB::removePage(int id) {
    pages_.erase(id);
}

void ZoneDB::clear() {
    pages_.clear();
    nextPageId_ = 1;
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

std::vector<int> ZoneDB::pageIds() const {
    std::vector<int> ids;
    ids.reserve(pages_.size());
    for (const auto& [id, p] : pages_) {
        ids.push_back(id);
    }
    return ids;
}

bool ZoneDB::loadFromFile(const QString& filename) {
    // TODO: Implement file loading
    // Original ViewTouch uses binary format
    return false;
}

bool ZoneDB::saveToFile(const QString& filename) const {
    // TODO: Implement file saving
    return false;
}

} // namespace vt
