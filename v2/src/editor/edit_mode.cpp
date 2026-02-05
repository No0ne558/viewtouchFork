/*
 * ViewTouch V2 - Edit Mode Implementation
 */

#include "editor/edit_mode.hpp"
#include "zone/page.hpp"

namespace vt {

EditMode::EditMode(QObject* parent)
    : QObject(parent)
{
}

EditMode::~EditMode() = default;

void EditMode::setActive(bool active) {
    if (active_ != active) {
        active_ = active;
        if (!active_) {
            clearSelection();
        }
        emit editModeChanged(active_);
        emit requestRedraw();
    }
}

void EditMode::setCurrentTool(EditTool tool) {
    if (currentTool_ != tool) {
        currentTool_ = tool;
        emit toolChanged(tool);
    }
}

void EditMode::selectZone(Zone* zone) {
    selectedZones_.clear();
    selectedZone_ = zone;
    if (zone) {
        selectedZones_.push_back(zone);
    }
    emit selectionChanged();
    emit requestRedraw();
}

void EditMode::clearSelection() {
    selectedZone_ = nullptr;
    selectedZones_.clear();
    emit selectionChanged();
    emit requestRedraw();
}

void EditMode::addToSelection(Zone* zone) {
    if (!zone) return;
    
    // Check if already selected
    for (auto* z : selectedZones_) {
        if (z == zone) return;
    }
    
    selectedZones_.push_back(zone);
    if (!selectedZone_) {
        selectedZone_ = zone;
    }
    emit selectionChanged();
    emit requestRedraw();
}

void EditMode::removeFromSelection(Zone* zone) {
    auto it = std::find(selectedZones_.begin(), selectedZones_.end(), zone);
    if (it != selectedZones_.end()) {
        selectedZones_.erase(it);
        if (selectedZone_ == zone) {
            selectedZone_ = selectedZones_.empty() ? nullptr : selectedZones_[0];
        }
        emit selectionChanged();
        emit requestRedraw();
    }
}

void EditMode::selectAll(Page* page) {
    if (!page) return;
    
    selectedZones_.clear();
    for (auto& z : *page) {
        selectedZones_.push_back(z.get());
    }
    selectedZone_ = selectedZones_.empty() ? nullptr : selectedZones_[0];
    emit selectionChanged();
    emit requestRedraw();
}

void EditMode::copyZone() {
    if (!selectedZone_) return;
    
    clipboardZone_ = selectedZone_;
    clipboardRegion_ = selectedZone_->region();
    clipboardState_ = std::make_unique<ZoneState>(selectedZone_->state(0));
}

void EditMode::pasteZone(Page* page, int x, int y) {
    if (!page || !clipboardZone_) return;
    
    // Create a new zone based on the clipboard
    // For now, this is a placeholder - actual implementation would
    // create the appropriate Zone subclass
    // TODO: Implement zone factory for proper cloning
}

void EditMode::cutZone() {
    if (!selectedZone_) return;
    copyZone();
    // Mark for deletion on paste
}

void EditMode::deleteSelectedZones(Page* page) {
    if (!page || selectedZones_.empty()) return;
    
    for (Zone* z : selectedZones_) {
        page->removeZone(z);
    }
    
    clearSelection();
    emit pageModified(page);
    emit requestRedraw();
}

void EditMode::moveSelectedZones(int dx, int dy) {
    if (selectedZones_.empty()) return;
    
    if (gridSnap_) {
        dx = snapToGrid(dx);
        dy = snapToGrid(dy);
    }
    
    for (Zone* z : selectedZones_) {
        Region r = z->region();
        r.x += dx;
        r.y += dy;
        z->setRegion(r);
        emit zoneModified(z);
    }
    emit requestRedraw();
}

void EditMode::resizeSelectedZone(int dw, int dh, ResizeHandle handle) {
    if (!selectedZone_) return;
    
    Region r = selectedZone_->region();
    
    if (gridSnap_) {
        dw = snapToGrid(dw);
        dh = snapToGrid(dh);
    }
    
    switch (handle) {
        case ResizeHandle::TopLeft:
            r.x += dw;
            r.y += dh;
            r.w -= dw;
            r.h -= dh;
            break;
        case ResizeHandle::Top:
            r.y += dh;
            r.h -= dh;
            break;
        case ResizeHandle::TopRight:
            r.y += dh;
            r.w += dw;
            r.h -= dh;
            break;
        case ResizeHandle::Right:
            r.w += dw;
            break;
        case ResizeHandle::BottomRight:
            r.w += dw;
            r.h += dh;
            break;
        case ResizeHandle::Bottom:
            r.h += dh;
            break;
        case ResizeHandle::BottomLeft:
            r.x += dw;
            r.w -= dw;
            r.h += dh;
            break;
        case ResizeHandle::Left:
            r.x += dw;
            r.w -= dw;
            break;
        default:
            break;
    }
    
    // Enforce minimum size
    if (r.w < 20) r.w = 20;
    if (r.h < 20) r.h = 20;
    
    selectedZone_->setRegion(r);
    emit zoneModified(selectedZone_);
    emit requestRedraw();
}

int EditMode::snapToGrid(int value) const {
    if (!gridSnap_ || gridSize_ <= 1) return value;
    return ((value + gridSize_ / 2) / gridSize_) * gridSize_;
}

QPoint EditMode::snapToGrid(const QPoint& pt) const {
    return QPoint(snapToGrid(pt.x()), snapToGrid(pt.y()));
}

void EditMode::startDrag(const QPoint& pos, ResizeHandle handle) {
    dragging_ = true;
    dragStart_ = pos;
    dragCurrent_ = pos;
    resizeHandle_ = handle;
    
    if (selectedZone_) {
        originalRegion_ = selectedZone_->region();
    }
}

void EditMode::updateDrag(const QPoint& pos) {
    if (!dragging_) return;
    
    dragCurrent_ = pos;
    int dx = pos.x() - dragStart_.x();
    int dy = pos.y() - dragStart_.y();
    
    if (resizeHandle_ == ResizeHandle::None) {
        // Moving
        if (selectedZone_) {
            Region r = originalRegion_;
            r.x += dx;
            r.y += dy;
            if (gridSnap_) {
                r.x = snapToGrid(r.x);
                r.y = snapToGrid(r.y);
            }
            selectedZone_->setRegion(r);
        }
    } else {
        // Resizing
        if (selectedZone_) {
            Region r = originalRegion_;
            
            switch (resizeHandle_) {
                case ResizeHandle::TopLeft:
                    r.x = originalRegion_.x + dx;
                    r.y = originalRegion_.y + dy;
                    r.w = originalRegion_.w - dx;
                    r.h = originalRegion_.h - dy;
                    break;
                case ResizeHandle::Top:
                    r.y = originalRegion_.y + dy;
                    r.h = originalRegion_.h - dy;
                    break;
                case ResizeHandle::TopRight:
                    r.y = originalRegion_.y + dy;
                    r.w = originalRegion_.w + dx;
                    r.h = originalRegion_.h - dy;
                    break;
                case ResizeHandle::Right:
                    r.w = originalRegion_.w + dx;
                    break;
                case ResizeHandle::BottomRight:
                    r.w = originalRegion_.w + dx;
                    r.h = originalRegion_.h + dy;
                    break;
                case ResizeHandle::Bottom:
                    r.h = originalRegion_.h + dy;
                    break;
                case ResizeHandle::BottomLeft:
                    r.x = originalRegion_.x + dx;
                    r.w = originalRegion_.w - dx;
                    r.h = originalRegion_.h + dy;
                    break;
                case ResizeHandle::Left:
                    r.x = originalRegion_.x + dx;
                    r.w = originalRegion_.w - dx;
                    break;
                default:
                    break;
            }
            
            if (gridSnap_) {
                r.x = snapToGrid(r.x);
                r.y = snapToGrid(r.y);
                r.w = snapToGrid(r.w);
                r.h = snapToGrid(r.h);
            }
            
            // Minimum size
            if (r.w < 20) r.w = 20;
            if (r.h < 20) r.h = 20;
            
            selectedZone_->setRegion(r);
        }
    }
    
    emit requestRedraw();
}

void EditMode::endDrag() {
    if (dragging_ && selectedZone_) {
        emit zoneModified(selectedZone_);
    }
    dragging_ = false;
    resizeHandle_ = ResizeHandle::None;
}

void EditMode::startDrag(Zone* zone, int x, int y, ResizeHandle handle) {
    if (zone) {
        selectZone(zone);
    }
    startDrag(QPoint(x, y), handle);
}

void EditMode::deleteZone(Zone* zone, Page* page) {
    if (!zone || !page) return;
    
    // Remove from selection if selected
    removeFromSelection(zone);
    
    // Remove from page
    page->removeZone(zone);
    
    emit pageModified(page);
    emit requestRedraw();
}

ResizeHandle EditMode::hitTestResizeHandle(Zone* zone, int x, int y) const {
    if (!zone) return ResizeHandle::None;
    
    const int handleSize = 8;
    Region r = zone->region();
    
    // Check corners first (they take priority)
    if (x >= r.x - handleSize && x <= r.x + handleSize &&
        y >= r.y - handleSize && y <= r.y + handleSize) {
        return ResizeHandle::TopLeft;
    }
    if (x >= r.x + r.w - handleSize && x <= r.x + r.w + handleSize &&
        y >= r.y - handleSize && y <= r.y + handleSize) {
        return ResizeHandle::TopRight;
    }
    if (x >= r.x + r.w - handleSize && x <= r.x + r.w + handleSize &&
        y >= r.y + r.h - handleSize && y <= r.y + r.h + handleSize) {
        return ResizeHandle::BottomRight;
    }
    if (x >= r.x - handleSize && x <= r.x + handleSize &&
        y >= r.y + r.h - handleSize && y <= r.y + r.h + handleSize) {
        return ResizeHandle::BottomLeft;
    }
    
    // Check edges
    if (y >= r.y - handleSize && y <= r.y + handleSize &&
        x > r.x + handleSize && x < r.x + r.w - handleSize) {
        return ResizeHandle::Top;
    }
    if (y >= r.y + r.h - handleSize && y <= r.y + r.h + handleSize &&
        x > r.x + handleSize && x < r.x + r.w - handleSize) {
        return ResizeHandle::Bottom;
    }
    if (x >= r.x - handleSize && x <= r.x + handleSize &&
        y > r.y + handleSize && y < r.y + r.h - handleSize) {
        return ResizeHandle::Left;
    }
    if (x >= r.x + r.w - handleSize && x <= r.x + r.w + handleSize &&
        y > r.y + handleSize && y < r.y + r.h - handleSize) {
        return ResizeHandle::Right;
    }
    
    return ResizeHandle::None;
}

void EditMode::undo() {
    // TODO: Implement undo stack
}

void EditMode::redo() {
    // TODO: Implement redo stack
}

} // namespace vt
