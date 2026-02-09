/*
 * ViewTouch V2 - Page Class
 * A Page contains a collection of Zones
 */

#pragma once

#include "core/types.hpp"
#include "core/colors.hpp"
#include "core/fonts.hpp"
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
    
    // Default zone appearance (per-state: 0=normal, 1=selected, 2=alternate)
    // These match original ViewTouch Page defaults for zone creation
    ZoneFrame defaultFrame(int state) const { return (state >= 0 && state < 3) ? defaultFrame_[state] : ZoneFrame::Default; }
    void setDefaultFrame(int state, ZoneFrame f) { if (state >= 0 && state < 3) defaultFrame_[state] = f; }
    
    uint8_t defaultTexture(int state) const { return (state >= 0 && state < 3) ? defaultTexture_[state] : TEXTURE_DEFAULT; }
    void setDefaultTexture(int state, uint8_t t) { if (state >= 0 && state < 3) defaultTexture_[state] = t; }
    // Legacy single-value accessors (use state 0)
    uint8_t defaultTexture() const { return defaultTexture_[0]; }
    void setDefaultTexture(uint8_t t) { defaultTexture_[0] = t; }
    
    uint8_t defaultColor(int state) const { return (state >= 0 && state < 3) ? defaultColor_[state] : COLOR_DEFAULT; }
    void setDefaultColor(int state, uint8_t c) { if (state >= 0 && state < 3) defaultColor_[state] = c; }
    // Legacy single-value accessors (use state 0)
    uint8_t defaultColor() const { return defaultColor_[0]; }
    void setDefaultColor(uint8_t c) { defaultColor_[0] = c; }
    
    uint8_t titleColor() const { return titleColor_; }
    void setTitleColor(uint8_t c) { titleColor_ = c; }
    
    FontId defaultFont() const { return defaultFont_; }
    void setDefaultFont(FontId f) { defaultFont_ = f; }
    
    int defaultSpacing() const { return defaultSpacing_; }
    void setDefaultSpacing(int s) { defaultSpacing_ = s; }
    
    int defaultShadow() const { return defaultShadow_; }
    void setDefaultShadow(int s) { defaultShadow_ = s; }
    
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
    
    // Per-state zone defaults (0=normal, 1=selected, 2=alternate)
    // Matches original ViewTouch Page constructor defaults
    ZoneFrame defaultFrame_[3] = { ZoneFrame::Default, ZoneFrame::Default, ZoneFrame::Hidden };
    uint8_t defaultTexture_[3] = { TEXTURE_DEFAULT, TEXTURE_DEFAULT, TEXTURE_DEFAULT };
    uint8_t defaultColor_[3]   = { COLOR_DEFAULT, COLOR_DEFAULT, COLOR_DEFAULT };
    uint8_t titleColor_ = static_cast<uint8_t>(TextColor::Black);
    FontId defaultFont_ = FontId::Default;      // FONT_DEFAULT = 0
    int defaultSpacing_ = 0;
    int defaultShadow_ = 256;                    // SHADOW_DEFAULT = 256
    
    std::vector<std::unique_ptr<Zone>> zones_;
};

} // namespace vt
