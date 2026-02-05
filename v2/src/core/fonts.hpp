/*
 * ViewTouch V2 - Font Definitions
 */

#pragma once

#include <QFont>
#include <array>
#include <cstdint>

namespace vt {

/*************************************************************
 * Font IDs - matches original ViewTouch
 *************************************************************/
enum class FontId : uint8_t {
    Default    = 0,
    Times48    = 1,
    Times48B   = 2,
    Times_18   = 3,
    Times20    = 4,
    Times_20   = 4,  // Alias
    Times24    = 5,
    Times34    = 6,
    Times20B   = 7,
    Times24B   = 8,
    Times34B   = 9,
    Times14    = 10,
    Times14B   = 11,
    Times18    = 12,
    Times18B   = 13,
    Count      = 14
};

constexpr uint8_t FONT_DEFAULT = 255;

/*************************************************************
 * Font Manager
 *************************************************************/
class FontManager {
public:
    FontManager();
    
    void initialize();
    
    QFont font(FontId id) const;
    QFont font(uint8_t id) const;
    
    QFont getFont(FontId id) const { return font(id); }
    QFont getFont(uint8_t id) const { return font(id); }
    
    // Get scaled font for current resolution
    QFont getScaledFont(FontId id, double scale) const;

private:
    void initFonts();
    
    std::array<QFont, static_cast<int>(FontId::Count)> fonts_;
    bool initialized_ = false;
};

} // namespace vt
