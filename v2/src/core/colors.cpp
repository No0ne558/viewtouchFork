/*
 * ViewTouch V2 - Color Implementation
 */

#include "core/colors.hpp"

namespace vt {

ColorPalette& ColorPalette::instance() {
    static ColorPalette palette;
    return palette;
}

ColorPalette::ColorPalette() {
    // Original ViewTouch color values
    textColors_[0]  = QColor(0, 0, 0);           // Black
    textColors_[1]  = QColor(255, 255, 255);     // White
    textColors_[2]  = QColor(255, 0, 0);         // Red
    textColors_[3]  = QColor(0, 200, 0);         // Green
    textColors_[4]  = QColor(0, 0, 255);         // Blue
    textColors_[5]  = QColor(255, 255, 0);       // Yellow
    textColors_[6]  = QColor(139, 90, 43);       // Brown
    textColors_[7]  = QColor(255, 165, 0);       // Orange
    textColors_[8]  = QColor(160, 32, 240);      // Purple
    textColors_[9]  = QColor(0, 128, 128);       // Teal
    textColors_[10] = QColor(128, 128, 128);     // Gray
    textColors_[11] = QColor(255, 0, 255);       // Magenta
    textColors_[12] = QColor(255, 69, 0);        // RedOrange
    textColors_[13] = QColor(32, 178, 170);      // SeaGreen
    textColors_[14] = QColor(135, 206, 235);     // LtBlue
    textColors_[15] = QColor(139, 0, 0);         // DkRed
    textColors_[16] = QColor(0, 100, 0);         // DkGreen
    textColors_[17] = QColor(0, 0, 139);         // DkBlue
    textColors_[18] = QColor(0, 80, 80);         // DkTeal
    textColors_[19] = QColor(139, 0, 139);       // DkMagenta
    textColors_[20] = QColor(20, 120, 120);      // DkSeaGreen
    
    // Frame edge colors - matches original ViewTouch
    edgeTop_    = QColor(200, 200, 200);
    edgeBottom_ = QColor(80, 80, 80);
    edgeLeft_   = QColor(180, 180, 180);
    edgeRight_  = QColor(100, 100, 100);
    
    litEdgeTop_    = QColor(240, 240, 240);
    litEdgeBottom_ = QColor(60, 60, 60);
    litEdgeLeft_   = QColor(220, 220, 220);
    litEdgeRight_  = QColor(80, 80, 80);
    
    darkEdgeTop_    = QColor(140, 140, 140);
    darkEdgeBottom_ = QColor(40, 40, 40);
    darkEdgeLeft_   = QColor(120, 120, 120);
    darkEdgeRight_  = QColor(60, 60, 60);
}

QColor ColorPalette::textColor(TextColor color) const {
    return textColor(static_cast<uint8_t>(color));
}

QColor ColorPalette::textColor(uint8_t index) const {
    if (index >= textColors_.size()) {
        return textColors_[0]; // Default to black
    }
    return textColors_[index];
}

std::tuple<uint8_t, uint8_t, uint8_t> ColorPalette::rgb(uint8_t colorId) const {
    QColor c = textColor(colorId);
    return {static_cast<uint8_t>(c.red()), 
            static_cast<uint8_t>(c.green()), 
            static_cast<uint8_t>(c.blue())};
}

std::tuple<uint8_t, uint8_t, uint8_t> ColorPalette::textureRgb(uint8_t textureId) const {
    // Base colors for textures
    struct TextureColor { uint8_t r, g, b; };
    static constexpr TextureColor colors[] = {
        {192, 180, 164},  // 0 Default - sand-like
        {192, 180, 164},  // 1 Sand
        {220, 210, 195},  // 2 LiteSand
        {160, 150, 140},  // 3 DarkSand
        {160, 120, 80},   // 4 Wood
        {190, 150, 110},  // 5 LiteWood
        {130, 90, 60},    // 6 DarkWood
        {230, 220, 200},  // 7 Parchment
        {245, 240, 230},  // 8 LiteParchment
        {200, 190, 170},  // 9 DarkParchment
        {180, 180, 190},  // 10 Marble
        {210, 210, 220},  // 11 LiteMarble
        {140, 140, 150},  // 12 DarkMarble
        {130, 90, 70},    // 13 Leather
        {160, 120, 100},  // 14 LiteLeather
        {100, 70, 50},    // 15 DarkLeather
        {200, 190, 180},  // 16 Canvas
        {230, 220, 210},  // 17 LiteCanvas
        {170, 160, 150},  // 18 DarkCanvas
        {250, 250, 250},  // 19 White
        {160, 160, 160},  // 20 Gray
        {40, 40, 40},     // 21 Black
        {200, 80, 80},    // 22 Red
        {160, 50, 50},    // 23 DarkRed
        {80, 180, 80},    // 24 Green
        {50, 130, 50},    // 25 DarkGreen
        {100, 140, 200},  // 26 Blue
        {60, 100, 160},   // 27 DarkBlue
        {230, 220, 100},  // 28 Yellow
        {230, 160, 80},   // 29 Orange
        {210, 190, 150},  // 30 Tan
        {80, 160, 160},   // 31 Teal
        {50, 120, 120},   // 32 DarkTeal
        {80, 140, 130},   // 33 BlueGreen
        {50, 110, 100},   // 34 DarkBlueGreen
    };
    
    if (textureId < sizeof(colors)/sizeof(colors[0])) {
        return {colors[textureId].r, colors[textureId].g, colors[textureId].b};
    }
    return {180, 180, 180}; // Default gray
}

std::tuple<uint8_t, uint8_t, uint8_t> ColorPalette::lightEdge(uint8_t textureId) const {
    auto [r, g, b] = textureRgb(textureId);
    // Lighten by 40
    return {static_cast<uint8_t>(std::min(255, r + 40)),
            static_cast<uint8_t>(std::min(255, g + 40)),
            static_cast<uint8_t>(std::min(255, b + 40))};
}

std::tuple<uint8_t, uint8_t, uint8_t> ColorPalette::darkEdge(uint8_t textureId) const {
    auto [r, g, b] = textureRgb(textureId);
    // Darken by 60
    return {static_cast<uint8_t>(std::max(0, r - 60)),
            static_cast<uint8_t>(std::max(0, g - 60)),
            static_cast<uint8_t>(std::max(0, b - 60))};
}

} // namespace vt
