/**
 * @file theme.cpp
 */

#include "ui/theme.hpp"

namespace vt2 {

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager() {
    // Register default themes
    Theme dark;
    dark.name = "modern-dark";
    dark.background = QColor(45, 45, 45);
    dark.foreground = QColor(255, 255, 255);
    dark.primary = QColor(51, 102, 153);
    dark.secondary = QColor(76, 153, 76);
    dark.accent = QColor(253, 126, 20);
    dark.success = QColor(40, 167, 69);
    dark.warning = QColor(255, 193, 7);
    dark.error = QColor(220, 53, 69);
    dark.buttonBackground = QColor(51, 102, 153);
    dark.buttonForeground = QColor(255, 255, 255);
    dark.buttonBorder = QColor(52, 58, 64);
    dark.buttonHover = QColor(66, 133, 199);
    dark.buttonPressed = QColor(41, 82, 122);
    dark.fontFamily = "Liberation Sans";
    dark.baseFontSize = 14;
    themes_["modern-dark"] = dark;
    
    Theme light;
    light.name = "modern-light";
    light.background = QColor(245, 245, 245);
    light.foreground = QColor(33, 33, 33);
    light.primary = QColor(51, 102, 153);
    light.secondary = QColor(76, 153, 76);
    light.accent = QColor(253, 126, 20);
    light.success = QColor(40, 167, 69);
    light.warning = QColor(255, 193, 7);
    light.error = QColor(220, 53, 69);
    light.buttonBackground = QColor(51, 102, 153);
    light.buttonForeground = QColor(255, 255, 255);
    light.buttonBorder = QColor(200, 200, 200);
    light.buttonHover = QColor(66, 133, 199);
    light.buttonPressed = QColor(41, 82, 122);
    light.fontFamily = "Liberation Sans";
    light.baseFontSize = 14;
    themes_["modern-light"] = light;
    
    currentTheme_ = dark;
}

void ThemeManager::loadTheme(const QString& name) {
    if (auto it = themes_.find(name); it != themes_.end()) {
        currentTheme_ = it->second;
    }
}

void ThemeManager::registerTheme(const Theme& theme) {
    themes_[theme.name] = theme;
}

QStringList ThemeManager::availableThemes() const {
    QStringList names;
    for (const auto& [name, _] : themes_) {
        names.append(name);
    }
    return names;
}

} // namespace vt2
