/*
 * ViewTouch V2 - Page Class
 * A Page contains a collection of Zones
 */

#pragma once

#include "core/types.hpp"
#include "zone/zone.hpp"

#include <QString>
#include <vector>
#include <memory>

namespace vt {

class Terminal;
class Renderer;

/*************************************************************
 * Page - Container for Zones
 *************************************************************/
class Page {
public:
    Page();
    ~Page();
    
    // Disable copy
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
    
    // Properties
    int id() const { return id_; }
    void setId(int id) { id_ = id; }
    
    QString name() const { return name_; }
    void setName(const QString& name) { name_ = name; }
    
    PageType type() const { return type_; }
    void setType(PageType t) { type_ = t; }
    
    int parentId() const { return parentId_; }
    void setParentId(int id) { parentId_ = id; }
    
    int index() const { return index_; }
    void setIndex(int i) { index_ = i; }
    
    // Size (design resolution)
    int width() const { return width_; }
    int height() const { return height_; }
    void setSize(int w, int h) { width_ = w; height_ = h; }
    
    // Default colors/textures
    uint8_t defaultTexture() const { return defaultTexture_; }
    void setDefaultTexture(uint8_t t) { defaultTexture_ = t; }
    
    uint8_t defaultColor() const { return defaultColor_; }
    void setDefaultColor(uint8_t c) { defaultColor_ = c; }
    
    uint8_t titleColor() const { return titleColor_; }
    void setTitleColor(uint8_t c) { titleColor_ = c; }
    
    // Zone management
    void addZone(std::unique_ptr<Zone> zone);
    void removeZone(Zone* zone);
    void clearZones();
    
    size_t zoneCount() const { return zones_.size(); }
    Zone* zone(size_t index);
    const Zone* zone(size_t index) const;
    
    // Get all zones as raw pointers (for iteration)
    std::vector<Zone*> zones() const;
    
    // Zone iteration (over unique_ptrs)
    auto begin() { return zones_.begin(); }
    auto end() { return zones_.end(); }
    auto begin() const { return zones_.begin(); }
    auto end() const { return zones_.end(); }
    
    // Find zone at position
    Zone* findZone(int x, int y);
    
    // Find zone by name or group
    Zone* findZoneByName(const QString& name);
    std::vector<Zone*> findZonesByGroup(int groupId);
    
    // Rendering
    void render(Renderer& renderer, Terminal* term);
    
    // Update all zones
    void update(Terminal* term, UpdateFlag flags, const QString& value);

private:
    int id_ = 0;
    QString name_;
    PageType type_ = PageType::Index;
    int parentId_ = 0;
    int index_ = 0;
    
    int width_ = 1024;
    int height_ = 768;
    
    uint8_t defaultTexture_ = TEXTURE_DEFAULT;
    uint8_t defaultColor_ = COLOR_DEFAULT;
    uint8_t titleColor_ = static_cast<uint8_t>(TextColor::Black);
    
    std::vector<std::unique_ptr<Zone>> zones_;
};

} // namespace vt
