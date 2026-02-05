/*
 * ViewTouch V2 - Zone Implementation
 */

#include "zone/zone.hpp"
#include "render/renderer.hpp"

#include <QRect>

namespace vt {

Zone::Zone() {
    // Initialize states with defaults
    states_[0].frame = ZoneFrame::Default;
    states_[0].texture = TEXTURE_DEFAULT;
    states_[0].color = COLOR_DEFAULT;
    
    states_[1].frame = ZoneFrame::Default;
    states_[1].texture = TEXTURE_DEFAULT;
    states_[1].color = COLOR_DEFAULT;
    
    states_[2].frame = ZoneFrame::Hidden;
    states_[2].texture = static_cast<uint8_t>(TextureId::Sand);
    states_[2].color = COLOR_DEFAULT;
    
    // Default size
    region_.w = 140;
    region_.h = 100;
}

Zone::~Zone() = default;

void Zone::setRegion(int x, int y, int w, int h) {
    region_.x = x;
    region_.y = y;
    region_.w = w;
    region_.h = h;
}

void Zone::setRegion(const Region& r) {
    region_ = r;
}

ZoneState& Zone::state(int index) {
    if (index < 0 || index > 2) index = 0;
    return states_[index];
}

const ZoneState& Zone::state(int index) const {
    if (index < 0 || index > 2) index = 0;
    return states_[index];
}

void Zone::setState(int index, const ZoneState& st) {
    if (index >= 0 && index <= 2) {
        states_[index] = st;
    }
}

bool Zone::contains(int px, int py) const {
    if (!active_) return false;
    
    // For now, just use rectangle bounds
    // TODO: Add shape-based hit testing for non-rectangle shapes
    return region_.contains(px, py);
}

void Zone::render(Renderer& renderer, Terminal* term) {
    if (!active_) return;
    
    const ZoneState& st = states_[currentState_];
    if (st.frame == ZoneFrame::Hidden) return;
    
    renderTexture(renderer, term);
    renderFrame(renderer, term);
    renderContent(renderer, term);
}

void Zone::renderFrame(Renderer& renderer, Terminal* term) {
    ZoneFrame frame = effectiveFrame();
    if (frame == ZoneFrame::Hidden || frame == ZoneFrame::None) return;
    
    QRect r(region_.x, region_.y, region_.w, region_.h);
    uint8_t tex = effectiveTexture();
    renderer.drawFrame(r, frame, tex);
}

void Zone::renderTexture(Renderer& renderer, Terminal* term) {
    uint8_t tex = effectiveTexture();
    if (tex == TEXTURE_CLEAR) return;
    
    QRect r(region_.x, region_.y, region_.w, region_.h);
    renderer.fillRect(r, tex);
}

void Zone::renderContent(Renderer& renderer, Terminal* term) {
    // Draw zone name as text
    if (!name_.isEmpty()) {
        uint8_t col = effectiveColor();
        QRect r(region_.x, region_.y, region_.w, region_.h);
        renderer.drawText(name_, r, static_cast<uint8_t>(font_), col, TextAlign::Center);
    }
}

int Zone::touch(Terminal* term, int tx, int ty) {
    if (!active_) return 0;
    
    switch (behavior_) {
    case ZoneBehavior::None:
        break;
        
    case ZoneBehavior::Toggle:
        currentState_ = (currentState_ == 0) ? 1 : 0;
        emit stateChanged(this, currentState_);
        break;
        
    case ZoneBehavior::Blink:
        currentState_ = 1;
        emit stateChanged(this, currentState_);
        break;
        
    case ZoneBehavior::Select:
        currentState_ = 1;
        emit stateChanged(this, currentState_);
        break;
        
    case ZoneBehavior::Double:
        // TODO: Implement double-tap detection
        break;
        
    case ZoneBehavior::Miss:
        return 0;  // Let touch pass through
    }
    
    emit touched(this);
    return 1;  // Touch handled
}

int Zone::touchRelease(Terminal* term, int tx, int ty) {
    if (behavior_ == ZoneBehavior::Blink && !stayLit_) {
        currentState_ = 0;
        emit stateChanged(this, currentState_);
    }
    return 1;
}

int Zone::touchDrag(Terminal* term, int tx, int ty) {
    return 0;
}

int Zone::keyPress(Terminal* term, int key, int state) {
    if (key_ != 0 && key == key_) {
        return touch(term, region_.x, region_.y);
    }
    return 0;
}

int Zone::update(Terminal* term, UpdateFlag flags, const QString& value) {
    return 0;
}

ZoneFrame Zone::effectiveFrame() const {
    ZoneFrame f = states_[currentState_].frame;
    if (f == ZoneFrame::Default || f == ZoneFrame::Unchanged) {
        f = ZoneFrame::Raised;  // Default frame style
    }
    return f;
}

uint8_t Zone::effectiveTexture() const {
    uint8_t t = states_[currentState_].texture;
    if (t == TEXTURE_DEFAULT || t == TEXTURE_UNCHANGED) {
        t = static_cast<uint8_t>(TextureId::Sand);  // Default texture
    }
    return t;
}

uint8_t Zone::effectiveColor() const {
    uint8_t c = states_[currentState_].color;
    if (c == COLOR_DEFAULT || c == COLOR_UNCHANGED || c == COLOR_PAGE_DEFAULT) {
        c = static_cast<uint8_t>(TextColor::Black);  // Default to black
    }
    return c;
}

} // namespace vt
