/**
 * @file page.hpp
 * @brief Page class - container for zones
 * 
 * A Page represents a full screen of the POS interface, containing
 * multiple zones. Pages can be navigated between and can have
 * parent/child relationships.
 */

#pragma once

#include "core/types.hpp"
#include "ui/zone.hpp"
#include <QWidget>
#include <QJsonObject>
#include <vector>
#include <memory>

namespace vt2 {

/**
 * @brief A page containing multiple zones
 * 
 * Pages are the primary navigation unit in ViewTouch. Each page
 * contains a collection of zones arranged in a layout. Users
 * navigate between pages using navigation zones/buttons.
 */
class Page : public QWidget {
    Q_OBJECT
    
    Q_PROPERTY(QString pageName READ pageName WRITE setPageName NOTIFY pageNameChanged)
    Q_PROPERTY(PageType pageType READ pageType CONSTANT)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)

public:
    explicit Page(QWidget* parent = nullptr);
    explicit Page(PageType type, QWidget* parent = nullptr);
    ~Page() override;
    
    // ========================================================================
    // Identity
    // ========================================================================
    
    PageId id() const { return id_; }
    void setId(PageId id) { id_ = id; }
    
    QString pageName() const { return name_; }
    void setPageName(const QString& name);
    
    PageType pageType() const { return type_; }
    
    // ========================================================================
    // Visual Properties
    // ========================================================================
    
    QColor backgroundColor() const { return bgColor_; }
    void setBackgroundColor(const QColor& color);
    
    /**
     * @brief Set background image
     */
    void setBackgroundImage(const QString& path);
    
    // ========================================================================
    // Zone Management
    // ========================================================================
    
    /**
     * @brief Add a zone to the page
     * @param zone Zone to add (takes ownership)
     */
    void addZone(std::unique_ptr<Zone> zone);
    
    /**
     * @brief Add a zone at specific position
     */
    void addZone(std::unique_ptr<Zone> zone, int x, int y, int width, int height);
    
    /**
     * @brief Remove a zone by ID
     */
    void removeZone(ZoneId id);
    
    /**
     * @brief Get zone by ID
     */
    Zone* zone(ZoneId id);
    const Zone* zone(ZoneId id) const;
    
    /**
     * @brief Get zone by name
     */
    Zone* zone(const QString& name);
    const Zone* zone(const QString& name) const;
    
    /**
     * @brief Get all zones
     */
    const std::vector<std::unique_ptr<Zone>>& zones() const { return zones_; }
    
    /**
     * @brief Get zone count
     */
    size_t zoneCount() const { return zones_.size(); }
    
    /**
     * @brief Clear all zones
     */
    void clearZones();
    
    /**
     * @brief Find zone at position
     */
    Zone* zoneAt(const QPoint& pos);
    
    // ========================================================================
    // Layout Helpers
    // ========================================================================
    
    /**
     * @brief Create a grid of buttons
     * @param rows Number of rows
     * @param cols Number of columns
     * @param labels Button labels
     * @param startX Starting X position
     * @param startY Starting Y position
     * @param buttonWidth Width of each button
     * @param buttonHeight Height of each button
     * @param spacing Spacing between buttons
     */
    void createButtonGrid(int rows, int cols, 
                          const QStringList& labels,
                          int startX, int startY,
                          int buttonWidth, int buttonHeight,
                          int spacing = 5);
    
    // ========================================================================
    // Serialization
    // ========================================================================
    
    /**
     * @brief Serialize page to JSON
     */
    QJsonObject toJson() const;
    
    /**
     * @brief Load page from JSON
     */
    void fromJson(const QJsonObject& json);
    
    /**
     * @brief Save page to file
     */
    bool saveToFile(const QString& path) const;
    
    /**
     * @brief Load page from file
     */
    bool loadFromFile(const QString& path);
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    /**
     * @brief Called when page becomes visible
     */
    virtual void onEnter();
    
    /**
     * @brief Called when page is about to be hidden
     */
    virtual void onExit();
    
    /**
     * @brief Refresh page content
     */
    virtual void refresh();

signals:
    void pageNameChanged(const QString& name);
    void zoneAdded(Zone* zone);
    void zoneRemoved(ZoneId id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    
private:
    PageId id_{0};
    QString name_;
    PageType type_ = PageType::Custom;
    QColor bgColor_ = colors::VTBackground;
    QString bgImagePath_;
    QPixmap bgImage_;
    
    std::vector<std::unique_ptr<Zone>> zones_;
    
    uint32_t nextZoneId_ = 1;
};

/**
 * @brief Factory for creating pages
 */
class PageFactory {
public:
    static PageFactory& instance();
    
    /**
     * @brief Create a page by type
     */
    std::unique_ptr<Page> create(PageType type);
    
    /**
     * @brief Create a page from JSON
     */
    std::unique_ptr<Page> createFromJson(const QJsonObject& json);
    
    /**
     * @brief Load page from file
     */
    std::unique_ptr<Page> loadFromFile(const QString& path);
    
private:
    PageFactory() = default;
};

} // namespace vt2
