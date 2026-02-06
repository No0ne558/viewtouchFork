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
 * Texture/Image IDs - matches original ViewTouch exactly
 *************************************************************/
enum class TextureId : uint8_t {
    Sand              = 0,   // sand-8.xpm
    LitSand           = 1,   // litsand-6.xpm
    DarkSand          = 2,   // darksand-6.xpm
    LiteWood          = 3,   // litewood-8.xpm
    Wood              = 4,   // wood-10.xpm
    DarkWood          = 5,   // darkwood-10.xpm
    GrayParchment     = 6,   // grayparchment-8.xpm
    GrayMarble        = 7,   // graymarble-12.xpm
    GreenMarble       = 8,   // greenmarble-12.xpm
    Parchment         = 9,   // parchment-6.xpm
    Pearl             = 10,  // pearl-8.xpm
    Canvas            = 11,  // canvas-8.xpm
    TanParchment      = 12,  // tanparchment-8.xpm
    Smoke             = 13,  // smoke-4.xpm
    Leather           = 14,  // leather-8.xpm
    BlueParchment     = 15,  // blueparchment.xpm
    Gradient          = 16,  // gradient-8.xpm
    GradientBrown     = 17,  // gradient-brown.xpm
    Black             = 18,  // black.xpm
    GreySand          = 19,  // greySand.xpm
    WhiteMesh         = 20,  // whiteMesh.xpm
    CarbonFiber       = 21,  // carbonfiber-128-6.xpm
    WhiteTexture      = 22,  // whitetexture-128-32.xpm
    DarkOrangeTexture = 23,  // darkorangetexture-128-32.xpm
    YellowTexture     = 24,  // yellowtexture-128-32.xpm
    GreenTexture      = 25,  // greentexture-128-32.xpm
    OrangeTexture     = 26,  // orangetexture-128-32.xpm
    BlueTexture       = 27,  // bluetexture-128-32.xpm
    PoolTable         = 28,  // pooltable-256.xpm
    Test              = 29,  // test-256.xpm
    DiamondLeather    = 30,  // diamondleather-256.xpm
    Bread             = 31,  // bread-256.xpm
    Lava              = 32,  // lava-256.xpm
    DarkMarble        = 33,  // darkmarble-256.xpm
    Count             = 34
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
