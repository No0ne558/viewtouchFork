/*
 * ViewTouch V2 - Edit Mode
 * Manages the edit state for UI customization
 */

#pragma once

#include "core/types.hpp"
#include "zone/zone.hpp"

#include <QObject>
#include <QPoint>
#include <vector>

namespace vt {

class Page;
class Terminal;

/*************************************************************
 * EditTool - Available editing tools
 *************************************************************/
enum class EditTool {
    Select,      // Select/move zones
    Resize,      // Resize zones
    Create,      // Create new zones
    Delete,      // Delete zones
    Copy,        // Copy zone
    Paste,       // Paste copied zone
    Properties,  // Edit zone properties
};

/*************************************************************
 * ResizeHandle - Which part of zone is being resized
 *************************************************************/
enum class ResizeHandle {
    None,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left,
};

/*************************************************************
 * EditMode - Edit mode state manager
 *************************************************************/
class EditMode : public QObject {
    Q_OBJECT

public:
    explicit EditMode(QObject* parent = nullptr);
    ~EditMode();

    // Edit mode state
    bool isActive() const { return active_; }
    void setActive(bool active);
    void toggle() { setActive(!active_); }

    // Current tool
    EditTool currentTool() const { return currentTool_; }
    void setCurrentTool(EditTool tool);

    // Grid snapping
    bool gridSnap() const { return gridSnap_; }
    void setGridSnap(bool snap) { gridSnap_ = snap; }
    int gridSize() const { return gridSize_; }
    void setGridSize(int size) { gridSize_ = size; }

    // Selection
    Zone* selectedZone() const { return selectedZone_; }
    void selectZone(Zone* zone);
    void clearSelection();
    
    // Multi-selection
    const std::vector<Zone*>& selectedZones() const { return selectedZones_; }
    void addToSelection(Zone* zone);
    void removeFromSelection(Zone* zone);
    void deselectZone(Zone* zone) { removeFromSelection(zone); }
    void selectAll(Page* page);

    // Clipboard
    void copyZone();
    void pasteZone(Page* page, int x, int y);
    void cutZone();
    bool hasClipboard() const { return clipboardZone_ != nullptr; }

    // Zone manipulation
    void deleteSelectedZones(Page* page);
    void deleteZone(Zone* zone, Page* page);
    void moveSelectedZones(int dx, int dy);
    void resizeSelectedZone(int dw, int dh, ResizeHandle handle);

    // Snap to grid
    int snapToGrid(int value) const;
    QPoint snapToGrid(const QPoint& pt) const;
    
    // Grid enabled
    bool isGridSnapEnabled() const { return gridSnap_; }

    // Dragging state
    bool isDragging() const { return dragging_; }
    void startDrag(const QPoint& pos, ResizeHandle handle = ResizeHandle::None);
    void startDrag(Zone* zone, int x, int y, ResizeHandle handle = ResizeHandle::None);
    void updateDrag(const QPoint& pos);
    void updateDrag(int x, int y) { updateDrag(QPoint(x, y)); }
    void endDrag();
    
    // Hit test for resize handles
    ResizeHandle hitTestResizeHandle(Zone* zone, int x, int y) const;

    // Undo/Redo (placeholder for future)
    void undo();
    void redo();
    bool canUndo() const { return false; } // TODO
    bool canRedo() const { return false; } // TODO

signals:
    void editModeChanged(bool active);
    void toolChanged(EditTool tool);
    void selectionChanged();
    void zoneModified(Zone* zone);
    void pageModified(Page* page);
    void requestRedraw();

private:
    bool active_ = false;
    EditTool currentTool_ = EditTool::Select;

    // Grid
    bool gridSnap_ = true;
    int gridSize_ = 10;

    // Selection
    Zone* selectedZone_ = nullptr;
    std::vector<Zone*> selectedZones_;

    // Clipboard
    std::unique_ptr<ZoneState> clipboardState_;
    Zone* clipboardZone_ = nullptr;
    Region clipboardRegion_;

    // Dragging
    bool dragging_ = false;
    QPoint dragStart_;
    QPoint dragCurrent_;
    ResizeHandle resizeHandle_ = ResizeHandle::None;
    Region originalRegion_;
};

} // namespace vt
