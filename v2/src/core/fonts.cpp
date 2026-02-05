/*
 * ViewTouch V2 - Font Implementation
 */

#include "core/fonts.hpp"

namespace vt {

FontManager::FontManager() {
    // Don't initialize in constructor - wait for explicit initialize()
}

void FontManager::initialize() {
    if (initialized_) return;
    initFonts();
    initialized_ = true;
}

void FontManager::initFonts() {
    // Use Liberation Serif as Times replacement (common on Linux)
    QString fontFamily = "Liberation Serif";
    
    // Default font
    fonts_[0] = QFont(fontFamily, 20);
    
    // Times variants
    fonts_[1] = QFont(fontFamily, 48);           // Times48
    fonts_[2] = QFont(fontFamily, 48, QFont::Bold);  // Times48B
    fonts_[3] = QFont(fontFamily, 20);           // Placeholder
    fonts_[4] = QFont(fontFamily, 20);           // Times20
    fonts_[5] = QFont(fontFamily, 24);           // Times24
    fonts_[6] = QFont(fontFamily, 34);           // Times34
    fonts_[7] = QFont(fontFamily, 20, QFont::Bold);  // Times20B
    fonts_[8] = QFont(fontFamily, 24, QFont::Bold);  // Times24B
    fonts_[9] = QFont(fontFamily, 34, QFont::Bold);  // Times34B
    fonts_[10] = QFont(fontFamily, 14);          // Times14
    fonts_[11] = QFont(fontFamily, 14, QFont::Bold); // Times14B
    fonts_[12] = QFont(fontFamily, 18);          // Times18
    fonts_[13] = QFont(fontFamily, 18, QFont::Bold); // Times18B
}

QFont FontManager::font(FontId id) const {
    return font(static_cast<uint8_t>(id));
}

QFont FontManager::font(uint8_t id) const {
    if (id >= fonts_.size()) {
        return fonts_[0]; // Default
    }
    return fonts_[id];
}

QFont FontManager::getScaledFont(FontId id, double scale) const {
    QFont f = font(id);
    int scaledSize = static_cast<int>(f.pointSize() * scale);
    f.setPointSize(std::max(8, scaledSize));
    return f;
}

} // namespace vt
