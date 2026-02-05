/**
 * @file button_zone.hpp
 * @brief Button zone - the most common zone type
 */

#pragma once

#include "ui/zone.hpp"
#include <QFont>

namespace vt2 {

/**
 * @brief A clickable button zone
 * 
 * This is the workhorse zone type in ViewTouch. Buttons can display
 * text and/or icons, respond to touch, and trigger actions.
 */
class ButtonZone : public Zone {
    Q_OBJECT
    
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QString iconPath READ iconPath WRITE setIconPath NOTIFY iconPathChanged)
    Q_PROPERTY(FontSize fontSize READ fontSize WRITE setFontSize)

public:
    explicit ButtonZone(QWidget* parent = nullptr);
    ~ButtonZone() override;
    
    // ========================================================================
    // Text
    // ========================================================================
    
    QString text() const { return text_; }
    void setText(const QString& text);
    
    // ========================================================================
    // Icon
    // ========================================================================
    
    QString iconPath() const { return iconPath_; }
    void setIconPath(const QString& path);
    
    QPixmap icon() const { return icon_; }
    void setIcon(const QPixmap& icon);
    
    // ========================================================================
    // Font
    // ========================================================================
    
    FontSize fontSize() const { return fontSize_; }
    void setFontSize(FontSize size);
    
    FontWeight fontWeight() const { return fontWeight_; }
    void setFontWeight(FontWeight weight);
    
    QFont font() const { return font_; }
    void setFont(const QFont& font);
    
    // ========================================================================
    // Alignment
    // ========================================================================
    
    HAlign horizontalAlignment() const { return hAlign_; }
    VAlign verticalAlignment() const { return vAlign_; }
    void setAlignment(HAlign h, VAlign v);
    
    // ========================================================================
    // Navigation
    // ========================================================================
    
    /**
     * @brief Set page to jump to when clicked
     */
    void setJumpPage(PageId pageId) { jumpPageId_ = pageId; }
    std::optional<PageId> jumpPage() const { return jumpPageId_; }
    
    // ========================================================================
    // Serialization
    // ========================================================================
    
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& json) override;

signals:
    void textChanged(const QString& text);
    void iconPathChanged(const QString& path);

protected:
    void drawContent(QPainter& painter) override;
    void initProperties() override;

private:
    void updateFont();
    
    QString text_;
    QString iconPath_;
    QPixmap icon_;
    QPixmap scaledIcon_;
    
    FontSize fontSize_ = FontSize::Normal;
    FontWeight fontWeight_ = FontWeight::Normal;
    QFont font_;
    
    HAlign hAlign_ = HAlign::Center;
    VAlign vAlign_ = VAlign::Center;
    
    std::optional<PageId> jumpPageId_;
    
    int padding_ = 8;
};

} // namespace vt2
