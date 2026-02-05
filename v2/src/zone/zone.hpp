/*
 * ViewTouch V2 - Zone Base Class
 * A Zone is a touch-sensitive area on a Page
 */

#pragma once

#include "core/types.hpp"
#include "core/colors.hpp"
#include "core/fonts.hpp"

#include <QObject>
#include <QString>
#include <memory>
#include <array>

namespace vt {

// Forward declarations
class Page;
class Terminal;
class Renderer;

/*************************************************************
 * ZoneState - visual state for normal/selected/alternate
 *************************************************************/
struct ZoneState {
    ZoneFrame frame   = ZoneFrame::Default;
    uint8_t   texture = TEXTURE_DEFAULT;
    uint8_t   color   = COLOR_DEFAULT;
    uint8_t   image   = 0;
};

/*************************************************************
 * Zone - Base class for all touch zones
 *************************************************************/
class Zone : public QObject {
    Q_OBJECT

public:
    Zone();
    virtual ~Zone();
    
    // Disable copy, allow move
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;
    Zone(Zone&&) noexcept = default;
    Zone& operator=(Zone&&) noexcept = default;
    
    // Region
    Region region() const { return region_; }
    void setRegion(int x, int y, int w, int h);
    void setRegion(const Region& r);
    
    int x() const { return region_.x; }
    int y() const { return region_.y; }
    int w() const { return region_.w; }
    int h() const { return region_.h; }
    
    // Properties
    QString name() const { return name_; }
    void setName(const QString& name) { name_ = name; }
    
    int groupId() const { return groupId_; }
    void setGroupId(int id) { groupId_ = id; }
    int group() const { return groupId_; }  // Alias for convenience
    
    ZoneBehavior behavior() const { return behavior_; }
    void setBehavior(ZoneBehavior b) { behavior_ = b; }
    
    FontId font() const { return font_; }
    void setFont(FontId f) { font_ = f; }
    
    ZoneShape shape() const { return shape_; }
    void setShape(ZoneShape s) { shape_ = s; }
    
    int shadow() const { return shadow_; }
    void setShadow(int s) { shadow_ = s; }
    
    int key() const { return key_; }
    void setKey(int k) { key_ = k; }
    
    // State access (0=normal, 1=selected, 2=alternate)
    ZoneState& state(int index);
    const ZoneState& state(int index) const;
    void setState(int index, const ZoneState& st);
    
    // Current state
    int currentState() const { return currentState_; }
    void setCurrentState(int s) { currentState_ = s; }
    
    // Active/edit flags
    bool isActive() const { return active_; }
    void setActive(bool a) { active_ = a; }
    
    bool isEdit() const { return edit_; }
    void setEdit(bool e) { edit_ = e; }
    
    bool needsUpdate() const { return needsUpdate_; }
    void setNeedsUpdate(bool u) { needsUpdate_ = u; }
    
    bool stayLit() const { return stayLit_; }
    void setStayLit(bool s) { stayLit_ = s; }
    
    // Parent page
    Page* page() const { return page_; }
    void setPage(Page* p) { page_ = p; }
    
    // Hit testing
    virtual bool contains(int px, int py) const;
    bool containsPoint(int px, int py) const { return contains(px, py); }  // Alias
    
    // Selection state
    bool isSelected() const { return currentState_ == 1; }
    void setSelected(bool sel) { currentState_ = sel ? 1 : 0; }
    
    // Rendering
    virtual void render(Renderer& renderer, Terminal* term);
    virtual void renderFrame(Renderer& renderer, Terminal* term);
    virtual void renderTexture(Renderer& renderer, Terminal* term);
    virtual void renderContent(Renderer& renderer, Terminal* term);
    
    // Touch handling
    virtual int touch(Terminal* term, int tx, int ty);
    virtual int touchRelease(Terminal* term, int tx, int ty);
    virtual int touchDrag(Terminal* term, int tx, int ty);
    
    // Convenience alias for release
    void release(int x, int y, Terminal* term) { touchRelease(term, x, y); }
    
    // Keyboard handling
    virtual int keyPress(Terminal* term, int key, int state);
    
    // Update handling
    virtual int update(Terminal* term, UpdateFlag flags, const QString& value);
    
    // Zone type identification (for subclass behavior)
    virtual const char* typeName() const { return "Zone"; }

signals:
    void touched(Zone* zone);
    void stateChanged(Zone* zone, int newState);

protected:
    // Helper to get effective color/texture/frame based on state and defaults
    ZoneFrame effectiveFrame() const;
    uint8_t effectiveTexture() const;
    uint8_t effectiveColor() const;

private:
    Region region_;
    QString name_;
    int groupId_ = 0;
    
    ZoneBehavior behavior_ = ZoneBehavior::Blink;
    FontId font_ = FontId::Default;
    ZoneShape shape_ = ZoneShape::Rectangle;
    int shadow_ = 256;  // SHADOW_DEFAULT
    int key_ = 0;
    
    std::array<ZoneState, 3> states_;  // normal, selected, alternate
    int currentState_ = 0;
    
    bool active_ = true;
    bool edit_ = false;
    bool needsUpdate_ = false;
    bool stayLit_ = false;
    
    Page* page_ = nullptr;
};

} // namespace vt
