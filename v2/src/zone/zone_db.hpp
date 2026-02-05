/*
 * ViewTouch V2 - Zone Database
 * Manages all Pages for the system
 */

#pragma once

#include "zone/page.hpp"

#include <QString>
#include <map>
#include <vector>
#include <memory>

namespace vt {

/*************************************************************
 * ZoneDB - Database of all Pages
 *************************************************************/
class ZoneDB {
public:
    ZoneDB();
    ~ZoneDB();
    
    // Disable copy
    ZoneDB(const ZoneDB&) = delete;
    ZoneDB& operator=(const ZoneDB&) = delete;
    
    // Page management
    void addPage(std::unique_ptr<Page> page);
    void removePage(int id);
    void clear();
    
    // Page access
    Page* page(int id);
    const Page* page(int id) const;
    Page* pageByName(const QString& name);
    
    // Find pages by type
    std::vector<Page*> pagesByType(PageType type);
    
    // Page count
    size_t pageCount() const { return pages_.size(); }
    
    // Page iteration
    auto begin() { return pages_.begin(); }
    auto end() { return pages_.end(); }
    auto begin() const { return pages_.begin(); }
    auto end() const { return pages_.end(); }
    
    // Get all page IDs
    std::vector<int> pageIds() const;
    
    // Load/save pages
    bool loadFromFile(const QString& filename);
    bool saveToFile(const QString& filename) const;

private:
    std::map<int, std::unique_ptr<Page>> pages_;
    int nextPageId_ = 1;
};

} // namespace vt
