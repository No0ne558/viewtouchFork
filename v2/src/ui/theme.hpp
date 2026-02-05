/**
 * @file theme.hpp
 * @brief UI theming system
 */

#pragma once

#include "core/types.hpp"
#include <QString>
#include <QColor>
#include <QFont>
#include <map>

namespace vt2 {

/**
 * @brief Theme definition for consistent UI styling
 */
struct Theme {
    QString name;
    
    // Colors
    QColor background;
    QColor foreground;
    QColor primary;
    QColor secondary;
    QColor accent;
    QColor success;
    QColor warning;
    QColor error;
    
    // Button colors
    QColor buttonBackground;
    QColor buttonForeground;
    QColor buttonBorder;
    QColor buttonHover;
    QColor buttonPressed;
    
    // Fonts
    QString fontFamily;
    int baseFontSize;
};

/**
 * @brief Theme manager
 */
class ThemeManager {
public:
    static ThemeManager& instance();
    
    void loadTheme(const QString& name);
    const Theme& currentTheme() const { return currentTheme_; }
    
    void registerTheme(const Theme& theme);
    QStringList availableThemes() const;
    
private:
    ThemeManager();
    Theme currentTheme_;
    std::map<QString, Theme> themes_;
};

} // namespace vt2
