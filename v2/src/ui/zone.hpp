/**
 * @file zone.hpp
 * @brief Base Zone class - the fundamental UI building block
 * 
 * Zones are the core UI concept in ViewTouch. Every interactive element
 * on screen is a Zone. This is equivalent to the original zone.hh but
 * reimplemented using Qt widgets.
 */

#pragma once

#include "core/types.hpp"
#include <QWidget>
#include <QPropertyAnimation>
#include <QJsonObject>
#include <memory>
#include <functional>

namespace vt2 {

// Forward declarations
class Page;
class Zone;

/**
 * @brief Zone property definition
 * 
 * Allows zones to have dynamic, editable properties (like the original
 * ViewTouch property system).
 */
struct ZoneProperty {
    QString name;
    QString displayName;
    QVariant value;
    QVariant defaultValue;
    QString type;  // "string", "int", "color", "font", "bool", etc.
    QString description;
    bool editable = true;
    QVariantList options;  // For enum/choice properties
};

/**
 * @brief Base class for all zones (UI elements)
 * 
 * A Zone represents a rectangular area on a Page that can:
 * - Display content (text, images, data)
 * - Respond to touch/click input
 * - Have configurable properties
 * - Be styled with colors, fonts, borders
 * 
 * This maps to the original ViewTouch Zone class but uses Qt's
 * widget system under the hood.
 */
class Zone : public QWidget {
    Q_OBJECT
    
    // Qt properties for QML/designer integration
    Q_PROPERTY(QString zoneName READ zoneName WRITE setZoneName NOTIFY zoneNameChanged)
    Q_PROPERTY(ZoneType zoneType READ zoneType CONSTANT)
    Q_PROPERTY(bool selected READ isSelected WRITE setSelected NOTIFY selectedChanged)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(QColor foregroundColor READ foregroundColor WRITE setForegroundColor NOTIFY foregroundColorChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)
    Q_PROPERTY(int borderWidth READ borderWidth WRITE setBorderWidth NOTIFY borderWidthChanged)

public:
    explicit Zone(QWidget* parent = nullptr);
    explicit Zone(ZoneType type, QWidget* parent = nullptr);
    ~Zone() override;
    
    // ========================================================================
    // Identity
    // ========================================================================
    
    /**
     * @brief Get zone ID
     */
    ZoneId id() const { return id_; }
    
    /**
     * @brief Set zone ID
     */
    void setId(ZoneId id) { id_ = id; }
    
    /**
     * @brief Get zone name
     */
    QString zoneName() const { return name_; }
    
    /**
     * @brief Set zone name
     */
    void setZoneName(const QString& name);
    
    /**
     * @brief Get zone type
     */
    ZoneType zoneType() const { return type_; }
    
    // ========================================================================
    // Visual Properties
    // ========================================================================
    
    QColor backgroundColor() const { return bgColor_; }
    void setBackgroundColor(const QColor& color);
    
    QColor foregroundColor() const { return fgColor_; }
    void setForegroundColor(const QColor& color);
    
    QColor borderColor() const { return borderColor_; }
    void setBorderColor(const QColor& color);
    
    int borderWidth() const { return borderWidth_; }
    void setBorderWidth(int width);
    
    int borderRadius() const { return borderRadius_; }
    void setBorderRadius(int radius);
    
    // ========================================================================
    // State
    // ========================================================================
    
    bool isSelected() const { return selected_; }
    void setSelected(bool selected);
    
    bool isPressed() const { return pressed_; }
    
    // ========================================================================
    // Behavior
    // ========================================================================
    
    ZoneBehavior behavior() const { return behavior_; }
    void setBehavior(ZoneBehavior behavior) { behavior_ = behavior; }
    
    // ========================================================================
    // Properties System
    // ========================================================================
    
    /**
     * @brief Get all editable properties
     */
    const std::vector<ZoneProperty>& properties() const { return properties_; }
    
    /**
     * @brief Get a property value by name
     */
    QVariant property(const QString& name) const;
    
    /**
     * @brief Set a property value
     */
    void setProperty(const QString& name, const QVariant& value);
    
    /**
     * @brief Add a custom property
     */
    void addProperty(const ZoneProperty& prop);
    
    // ========================================================================
    // Serialization
    // ========================================================================
    
    /**
     * @brief Serialize zone to JSON
     */
    virtual QJsonObject toJson() const;
    
    /**
     * @brief Load zone from JSON
     */
    virtual void fromJson(const QJsonObject& json);
    
    // ========================================================================
    // Page Integration
    // ========================================================================
    
    /**
     * @brief Get parent page
     */
    Page* page() const { return page_; }
    
    /**
     * @brief Set parent page
     */
    void setPage(Page* page) { page_ = page; }
    
    // ========================================================================
    // Actions
    // ========================================================================
    
    /**
     * @brief Set the action to perform on touch/click
     */
    void setAction(std::function<void()> action) { action_ = std::move(action); }
    
    /**
     * @brief Execute the zone's action
     */
    virtual void executeAction();

signals:
    void zoneNameChanged(const QString& name);
    void selectedChanged(bool selected);
    void enabledChanged(bool enabled);
    void backgroundColorChanged(const QColor& color);
    void foregroundColorChanged(const QColor& color);
    void borderColorChanged(const QColor& color);
    void borderWidthChanged(int width);
    
    /**
     * @brief Emitted when zone is touched/clicked
     */
    void touched();
    
    /**
     * @brief Emitted when zone is pressed
     */
    void pressed();
    
    /**
     * @brief Emitted when zone is released
     */
    void released();
    
    /**
     * @brief Emitted when a property changes
     */
    void propertyChanged(const QString& name, const QVariant& value);

protected:
    // Qt event handlers
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    
    /**
     * @brief Draw zone content (override in subclasses)
     */
    virtual void drawContent(QPainter& painter);
    
    /**
     * @brief Initialize default properties
     */
    virtual void initProperties();
    
    /**
     * @brief Update stylesheet from properties
     */
    void updateStyleSheet();

    ZoneId id_{0};
    QString name_;
    ZoneType type_ = ZoneType::Button;
    ZoneBehavior behavior_ = ZoneBehavior::Standard;
    
    QColor bgColor_ = colors::VTBlue;
    QColor fgColor_ = colors::White;
    QColor borderColor_ = colors::DarkGray;
    int borderWidth_ = 1;
    int borderRadius_ = 4;
    
    bool selected_ = false;
    bool pressed_ = false;
    bool hovered_ = false;
    
    Page* page_ = nullptr;
    std::vector<ZoneProperty> properties_;
    std::function<void()> action_;
};

/**
 * @brief Factory for creating zones from JSON or type
 */
class ZoneFactory {
public:
    static ZoneFactory& instance();
    
    /**
     * @brief Register a zone type creator
     */
    template<typename T>
    void registerType(ZoneType type) {
        creators_[type] = []() { return std::make_unique<T>(); };
    }
    
    /**
     * @brief Create a zone by type
     */
    std::unique_ptr<Zone> create(ZoneType type);
    
    /**
     * @brief Create a zone from JSON
     */
    std::unique_ptr<Zone> createFromJson(const QJsonObject& json);
    
private:
    ZoneFactory();
    std::map<ZoneType, std::function<std::unique_ptr<Zone>()>> creators_;
};

} // namespace vt2
