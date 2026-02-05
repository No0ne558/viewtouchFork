/*
 * ViewTouch V2 - Page Implementation
 */

#include "zone/page.hpp"
#include "render/renderer.hpp"

namespace vt {

Page::Page() = default;
Page::~Page() = default;

void Page::addZone(std::unique_ptr<Zone> zone) {
    if (zone) {
        zones_.push_back(std::move(zone));
    }
}

void Page::removeZone(Zone* zone) {
    auto it = std::find_if(zones_.begin(), zones_.end(),
        [zone](const auto& z) { return z.get() == zone; });
    if (it != zones_.end()) {
        zones_.erase(it);
    }
}

void Page::clearZones() {
    zones_.clear();
}

Zone* Page::zone(size_t index) {
    if (index < zones_.size()) {
        return zones_[index].get();
    }
    return nullptr;
}

const Zone* Page::zone(size_t index) const {
    if (index < zones_.size()) {
        return zones_[index].get();
    }
    return nullptr;
}

std::vector<Zone*> Page::zones() const {
    std::vector<Zone*> result;
    result.reserve(zones_.size());
    for (const auto& z : zones_) {
        result.push_back(z.get());
    }
    return result;
}

Zone* Page::findZone(int x, int y) {
    // Search in reverse order (top-most zones first)
    for (auto it = zones_.rbegin(); it != zones_.rend(); ++it) {
        Zone* z = it->get();
        if (z->contains(x, y)) {
            return z;
        }
    }
    return nullptr;
}

Zone* Page::findZoneByName(const QString& name) {
    for (auto& z : zones_) {
        if (z->name() == name) {
            return z.get();
        }
    }
    return nullptr;
}

std::vector<Zone*> Page::findZonesByGroup(int groupId) {
    std::vector<Zone*> result;
    for (auto& z : zones_) {
        if (z->groupId() == groupId) {
            result.push_back(z.get());
        }
    }
    return result;
}

void Page::render(Renderer& renderer, Terminal* term) {
    // Render all zones in order (back to front)
    for (auto& z : zones_) {
        z->render(renderer, term);
    }
}

void Page::update(Terminal* term, UpdateFlag flags, const QString& value) {
    for (auto& z : zones_) {
        z->update(term, flags, value);
    }
}

} // namespace vt
