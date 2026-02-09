/*
 * ViewTouch V2 - Zone Database
 * Manages all Pages for the system
 * 
 * Page ID Convention (matching original ViewTouch):
 * - System pages have negative IDs (e.g., -1 = Login, -3 = Table, etc.)
 * - User pages have positive IDs starting from 1
 * - ID 0 is reserved/unused
 */

#pragma once

#include "zone/page.hpp"
#include "core/fonts.hpp"

#include <QString>
#include <map>
#include <vector>
#include <memory>

namespace vt {

/*************************************************************
 * System Page IDs - matches original ViewTouch
 *************************************************************/
constexpr int PAGEID_LOGIN       = -1;   // Login page
constexpr int PAGEID_LOGIN2      = -2;   // Alternate login page
constexpr int PAGEID_TABLE       = -3;   // Table layout page
constexpr int PAGEID_TABLE2      = -4;   // Table page with check detail
constexpr int PAGEID_GUESTCOUNT  = -5;   // Guest count entry
constexpr int PAGEID_GUESTCOUNT2 = -6;   // Alternate guest count
constexpr int PAGEID_LOGOUT      = -7;   // Logout page
constexpr int PAGEID_BAR_SETTLE  = -8;   // Bar settlement page
constexpr int PAGEID_ITEM_TARGET = -9;   // Item target page
constexpr int PAGEID_MANAGER     = -10;  // Manager page
constexpr int PAGEID_SETTLEMENT  = -20;  // Settlement page
constexpr int PAGEID_TABSETTLE   = -85;  // Tab settlement

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
    
    // Create a new user page with auto-assigned ID
    Page* createPage(const QString& name = QString(), PageType type = PageType::Index);
    
    // Create a system page with specific ID
    Page* createSystemPage(int id, const QString& name, PageType type);
    
    // Initialize default system pages
    void initSystemPages();
    
    // Page access
    Page* page(int id);
    const Page* page(int id) const;
    Page* pageByName(const QString& name);
    
    // Check if a page is a system page (negative ID)
    static bool isSystemPage(int id) { return id < 0; }
    
    // Find pages by type
    std::vector<Page*> pagesByType(PageType type);
    
    // Get all system pages
    std::vector<Page*> systemPages();
    
    // Get all user pages
    std::vector<Page*> userPages();
    
    // Page count
    size_t pageCount() const { return pages_.size(); }
    size_t systemPageCount() const;
    size_t userPageCount() const;
    
    // Page iteration
    auto begin() { return pages_.begin(); }
    auto end() { return pages_.end(); }
    auto begin() const { return pages_.begin(); }
    auto end() const { return pages_.end(); }
    
    // Get all page IDs
    std::vector<int> pageIds() const;
    std::vector<int> systemPageIds() const;
    std::vector<int> userPageIds() const;
    
    // Next available user page ID
    int nextUserPageId() const { return nextUserPageId_; }
    
    // Load/save UI data
    bool loadUi(const QString& filename);
    bool saveUi(const QString& filename) const;
    
    // Data directory
    void setDataDir(const QString& dir) { dataDir_ = dir; }
    QString dataDir() const { return dataDir_; }
    
    // ===== Global zone defaults (matching v1 ZoneDB constructor) =====
    // These are the ultimate fallback when Page defaults are also DEFAULT
    ZoneFrame defaultFrame(int state) const { return (state >= 0 && state < 3) ? defaultFrame_[state] : ZoneFrame::Raised; }
    uint8_t   defaultTexture(int state) const { return (state >= 0 && state < 3) ? defaultTexture_[state] : 0; }
    uint8_t   defaultColor(int state) const { return (state >= 0 && state < 3) ? defaultColor_[state] : 0; }
    FontId    defaultFont() const { return defaultFont_; }
    int       defaultSpacing() const { return defaultSpacing_; }
    int       defaultShadow() const { return defaultShadow_; }
    uint8_t   defaultImage() const { return defaultImage_; }
    uint8_t   defaultTitleColor() const { return defaultTitleColor_; }

private:
    std::map<int, std::unique_ptr<Page>> pages_;
    int nextUserPageId_ = 1;  // User pages start at 1
    QString dataDir_;
    
    // Global zone defaults (v1 ZoneDB constructor values)
    ZoneFrame defaultFrame_[3]   = { ZoneFrame::Raised, ZoneFrame::Raised, ZoneFrame::Hidden };
    uint8_t   defaultTexture_[3] = { 0, 1, 0 };  // Sand, LitSand, Sand
    uint8_t   defaultColor_[3]   = { 0, 0, 0 };  // Black, Black, Black
    FontId    defaultFont_        = FontId::Times24;
    int       defaultSpacing_     = 2;
    int       defaultShadow_      = 0;
    uint8_t   defaultImage_       = 7;   // GrayMarble
    uint8_t   defaultTitleColor_  = 4;   // Blue
};

} // namespace vt
