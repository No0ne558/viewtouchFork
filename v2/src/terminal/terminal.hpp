/*
 * ViewTouch V2 - Terminal Class
 * Represents a POS terminal station
 */

#pragma once

#include "core/types.hpp"

#include <QString>
#include <QObject>
#include <array>
#include <memory>

namespace vt {

class Page;
class Zone;
class ZoneDB;
class Control;
class Renderer;

/*************************************************************
 * Terminal - POS Terminal Station
 * 
 * Each terminal has:
 * - Its own page stack (navigation history)
 * - Current user (employee)
 * - Current check/order being worked on
 * - Connection state
 *************************************************************/
class Terminal : public QObject {
    Q_OBJECT
    
public:
    explicit Terminal(QObject* parent = nullptr);
    ~Terminal();
    
    // Identity
    int id() const { return id_; }
    void setId(int id) { id_ = id; }
    
    QString name() const { return name_; }
    void setName(const QString& name) { name_ = name; }
    
    TerminalType type() const { return type_; }
    void setType(TerminalType t) { type_ = t; }
    
    // Display size
    int width() const { return width_; }
    int height() const { return height_; }
    void setSize(int w, int h) { width_ = w; height_ = h; }
    
    // Control reference
    Control* control() const { return control_; }
    void setControl(Control* ctrl) { control_ = ctrl; }
    
    // ZoneDB reference
    ZoneDB* zoneDb() const { return zoneDb_; }
    void setZoneDb(ZoneDB* db) { zoneDb_ = db; }
    
    // Current page
    Page* currentPage() const { return currentPage_; }
    
    // Page navigation
    bool jumpToPage(int pageId, JumpType jt = JumpType::Normal);
    bool jumpToPage(Page* page, JumpType jt = JumpType::Normal);
    bool jumpBack();
    bool jumpHome();
    void clearPageStack();
    
    // Page stack info
    int pageStackDepth() const { return stackIndex_; }
    int previousPageId() const;
    
    // User management
    int userId() const { return userId_; }
    void setUserId(int id) { userId_ = id; }
    
    QString userName() const { return userName_; }
    void setUserName(const QString& name) { userName_ = name; }
    
    // Check/order management
    int checkId() const { return checkId_; }
    void setCheckId(int id) { checkId_ = id; }
    
    // Touch handling
    void touch(int x, int y);
    void release(int x, int y);
    
    // Signal/message handling
    int signal(const QString& message, int groupId = 0);
    
    // Selected zone
    Zone* selectedZone() const { return selectedZone_; }
    void setSelectedZone(Zone* zone);
    void clearSelection();
    
    // Rendering
    void draw(Renderer& renderer);
    void update(UpdateFlag flags, const QString& value = QString());
    
    // Request redraw
    void requestRedraw();
    bool needsRedraw() const { return needsRedraw_; }
    void clearRedrawFlag() { needsRedraw_ = false; }
    
signals:
    void pageChanged(Page* newPage, Page* oldPage);
    void userChanged(int userId);
    void checkChanged(int checkId);
    void selectionChanged(Zone* zone);
    void redrawRequested();
    
private:
    // Identity
    int id_ = 0;
    QString name_;
    TerminalType type_ = TerminalType::OrderOnly;
    
    // Display
    int width_ = 1024;
    int height_ = 768;
    
    // References
    Control* control_ = nullptr;
    ZoneDB* zoneDb_ = nullptr;
    
    // Page navigation stack
    std::array<Page*, PAGE_STACK_SIZE> pageStack_{};
    int stackIndex_ = 0;
    Page* currentPage_ = nullptr;
    int homePageId_ = 0;
    
    // User state
    int userId_ = 0;
    QString userName_;
    
    // Check state
    int checkId_ = 0;
    
    // Selection
    Zone* selectedZone_ = nullptr;
    
    // Redraw flag
    bool needsRedraw_ = true;
};

} // namespace vt
