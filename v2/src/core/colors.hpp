/*
 * ViewTouch V2 - Color Definitions
 * Faithful reimplementation of ViewTouch colors
 */

#pragma once

#include <QColor>
#include <array>
#include <tuple>
#include <cstdint>

namespace vt {

/*************************************************************
 * Text Colors - matches original ViewTouch
 *************************************************************/
enum class TextColor : uint8_t {
    Black       = 0,
    White       = 1,
    Red         = 2,
    Green       = 3,
    Blue        = 4,
    Yellow      = 5,
    Brown       = 6,
    Orange      = 7,
    Purple      = 8,
    Teal        = 9,
    Gray        = 10,
    Magenta     = 11,
    RedOrange   = 12,
    SeaGreen    = 13,
    LtBlue      = 14,
    DkRed       = 15,
    DkGreen     = 16,
    DkBlue      = 17,
    DkTeal      = 18,
    DkMagenta   = 19,
    DkSeaGreen  = 20,
    Count       = 21
};

// Special color values
constexpr uint8_t COLOR_DEFAULT      = 255;
constexpr uint8_t COLOR_PAGE_DEFAULT = 254;
constexpr uint8_t COLOR_CLEAR        = 253;
constexpr uint8_t COLOR_UNCHANGED    = 252;

/*************************************************************
 * Texture/Image IDs - matches original ViewTouch
 *************************************************************/
enum class TextureId : uint8_t {
    Default        = 0,
    Sand           = 1,
    LiteSand       = 2,
    DarkSand       = 3,
    Wood           = 4,
    LiteWood       = 5,
    DarkWood       = 6,
    Parchment      = 7,
    LiteParchment  = 8,
    DarkParchment  = 9,
    Marble         = 10,
    LiteMarble     = 11,
    DarkMarble     = 12,
    Leather        = 13,
    LiteLeather    = 14,
    DarkLeather    = 15,
    Canvas         = 16,
    LiteCanvas     = 17,
    DarkCanvas     = 18,
    White          = 19,
    Gray           = 20,
    Black          = 21,
    Red            = 22,
    DarkRed        = 23,
    Green          = 24,
    DarkGreen      = 25,
    Blue           = 26,
    DarkBlue       = 27,
    Yellow         = 28,
    Orange         = 29,
    Tan            = 30,
    Teal           = 31,
    DarkTeal       = 32,
    BlueGreen      = 33,
    DarkBlueGreen  = 34,
    Count          = 35
};

// Special texture values
constexpr uint8_t TEXTURE_CLEAR     = 253;
constexpr uint8_t TEXTURE_UNCHANGED = 254;
constexpr uint8_t TEXTURE_DEFAULT   = 255;

/*************************************************************
 * Color Palette - actual RGB values
 *************************************************************/
class ColorPalette {
public:
    static ColorPalette& instance();
    
    QColor textColor(TextColor color) const;
    QColor textColor(uint8_t index) const;
    
    // Get RGB tuple for a text color
    std::tuple<uint8_t, uint8_t, uint8_t> rgb(uint8_t colorId) const;
    
    // Get RGB tuple for a texture base color
    std::tuple<uint8_t, uint8_t, uint8_t> textureRgb(uint8_t textureId) const;
    
    // Get light edge color for frame effects
    std::tuple<uint8_t, uint8_t, uint8_t> lightEdge(uint8_t textureId) const;
    
    // Get dark edge color for frame effects  
    std::tuple<uint8_t, uint8_t, uint8_t> darkEdge(uint8_t textureId) const;
    
    // Frame edge colors
    QColor edgeTop() const { return edgeTop_; }
    QColor edgeBottom() const { return edgeBottom_; }
    QColor edgeLeft() const { return edgeLeft_; }
    QColor edgeRight() const { return edgeRight_; }
    
    QColor litEdgeTop() const { return litEdgeTop_; }
    QColor litEdgeBottom() const { return litEdgeBottom_; }
    QColor litEdgeLeft() const { return litEdgeLeft_; }
    QColor litEdgeRight() const { return litEdgeRight_; }
    
    QColor darkEdgeTop() const { return darkEdgeTop_; }
    QColor darkEdgeBottom() const { return darkEdgeBottom_; }
    QColor darkEdgeLeft() const { return darkEdgeLeft_; }
    QColor darkEdgeRight() const { return darkEdgeRight_; }

private:
    ColorPalette();
    
    std::array<QColor, static_cast<int>(TextColor::Count)> textColors_;
    
    QColor edgeTop_, edgeBottom_, edgeLeft_, edgeRight_;
    QColor litEdgeTop_, litEdgeBottom_, litEdgeLeft_, litEdgeRight_;
    QColor darkEdgeTop_, darkEdgeBottom_, darkEdgeLeft_, darkEdgeRight_;
};

} // namespace vt
