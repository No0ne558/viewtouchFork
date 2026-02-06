/*
 * ViewTouch V2 - Renderer Class
 * Handles Qt-based rendering (replaces X11)
 */

#pragma once

#include "core/types.hpp"

#include <QObject>
#include <QPainter>
#include <QFont>
#include <QColor>
#include <QRect>
#include <QString>
#include <QPixmap>
#include <map>

namespace vt {

class ColorPalette;
class FontManager;
class Textures;

/*************************************************************
 * Renderer - Qt-based rendering
 * 
 * Provides drawing operations similar to original ViewTouch:
 * - Frame drawing (raised, inset, double, etc.)
 * - Texture filling
 * - Text rendering
 * - Image drawing
 *************************************************************/
class Renderer : public QObject {
    Q_OBJECT
    
public:
    explicit Renderer(QObject* parent = nullptr);
    ~Renderer();
    
    // Begin/end rendering to a paint device
    void begin(QPaintDevice* device);
    void end();
    
    // Is rendering active?
    bool isActive() const { return painter_.isActive(); }
    
    // Set the target area (for scaling)
    void setTargetSize(int width, int height);
    int targetWidth() const { return targetWidth_; }
    int targetHeight() const { return targetHeight_; }
    
    // Set the design (logical) size
    void setDesignSize(int width, int height);
    int designWidth() const { return designWidth_; }
    int designHeight() const { return designHeight_; }
    
    // Coordinate scaling
    int scaleX(int x) const;
    int scaleY(int y) const;
    QRect scaleRect(const QRect& r) const;
    
    // Background clearing
    void clear(const QColor& color = Qt::white);
    
    // Frame drawing
    void drawFrame(const QRect& rect, ZoneFrame frame, uint8_t textureId);
    void drawRaisedFrame(const QRect& rect, uint8_t textureId);
    void drawInsetFrame(const QRect& rect, uint8_t textureId);
    void drawDoubleFrame(const QRect& rect, uint8_t textureId);
    void drawBorderFrame(const QRect& rect, uint8_t textureId);
    void drawSandFrame(const QRect& rect, uint8_t textureId);
    
    // Fill with texture or color
    void fillRect(const QRect& rect, uint8_t textureId);
    void fillRect(const QRect& rect, const QColor& color);
    
    // Text drawing
    void drawText(const QString& text, const QRect& rect, 
                  uint8_t fontId, uint8_t colorId,
                  TextAlign align = TextAlign::Center);
    
    void drawText(const QString& text, int x, int y,
                  uint8_t fontId, uint8_t colorId);
    
    // Draw text centered at x,y position
    void drawTextCentered(const QString& text, int cx, int cy,
                          uint8_t fontId, uint8_t colorId);
    
    // Get text metrics
    QSize textSize(const QString& text, uint8_t fontId) const;
    int textWidth(const QString& text, uint8_t fontId) const;
    int textHeight(uint8_t fontId) const;
    
    // Image drawing
    void drawImage(const QPixmap& image, const QRect& rect);
    void drawImage(const QPixmap& image, int x, int y);
    
    // Line drawing
    void drawLine(int x1, int y1, int x2, int y2, const QColor& color, int width = 1);
    void drawLine(int x1, int y1, int x2, int y2, uint8_t colorId, int width = 1);
    
    // Rectangle drawing (outline only)
    void drawRect(const QRect& rect, const QColor& color, int width = 1);
    void drawRect(const QRect& rect, uint8_t colorId, int width = 1);
    
    // Get color from palette
    QColor color(uint8_t colorId) const;
    
    // Get font
    QFont font(uint8_t fontId) const;
    
    // Texture management
    Textures* textures() const { return textures_; }
    void setTextures(Textures* tex) { textures_ = tex; }
    
    // Color palette
    ColorPalette* palette() const { return palette_; }
    void setPalette(ColorPalette* pal) { palette_ = pal; }
    
    // Font manager
    FontManager* fontManager() const { return fontManager_; }
    void setFontManager(FontManager* fm) { fontManager_ = fm; }
    
private:
    // Get frame colors for a texture
    void getFrameColors(uint8_t textureId, QColor& light, QColor& dark, QColor& face);
    
    QPainter painter_;
    Textures* textures_ = nullptr;
    ColorPalette* palette_ = nullptr;
    FontManager* fontManager_ = nullptr;
    
    int targetWidth_ = 1024;
    int targetHeight_ = 768;
    int designWidth_ = 1024;
    int designHeight_ = 768;
};

} // namespace vt
