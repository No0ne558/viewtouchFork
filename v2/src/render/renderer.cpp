/*
 * ViewTouch V2 - Renderer Implementation
 */

#include "render/renderer.hpp"
#include "core/colors.hpp"
#include "core/fonts.hpp"
#include "render/textures.hpp"

#include <QPen>
#include <QBrush>

namespace vt {

Renderer::Renderer(QObject* parent)
    : QObject(parent)
{
}

Renderer::~Renderer() {
    if (painter_.isActive()) {
        painter_.end();
    }
}

void Renderer::begin(QPaintDevice* device) {
    if (painter_.isActive()) {
        painter_.end();
    }
    painter_.begin(device);
    painter_.setRenderHint(QPainter::Antialiasing, true);
    painter_.setRenderHint(QPainter::TextAntialiasing, true);
}

void Renderer::end() {
    if (painter_.isActive()) {
        painter_.end();
    }
}

void Renderer::setTargetSize(int width, int height) {
    targetWidth_ = width;
    targetHeight_ = height;
}

void Renderer::setDesignSize(int width, int height) {
    designWidth_ = width;
    designHeight_ = height;
}

int Renderer::scaleX(int x) const {
    return (x * targetWidth_) / designWidth_;
}

int Renderer::scaleY(int y) const {
    return (y * targetHeight_) / designHeight_;
}

QRect Renderer::scaleRect(const QRect& r) const {
    return QRect(
        scaleX(r.x()),
        scaleY(r.y()),
        scaleX(r.width()),
        scaleY(r.height())
    );
}

void Renderer::clear(const QColor& color) {
    if (!painter_.isActive()) return;
    painter_.fillRect(0, 0, targetWidth_, targetHeight_, color);
}

void Renderer::getFrameColors(uint8_t textureId, QColor& light, QColor& dark, QColor& face) {
    if (palette_) {
        auto [lr, lg, lb] = palette_->lightEdge(textureId);
        auto [dr, dg, db] = palette_->darkEdge(textureId);
        auto [fr, fg, fb] = palette_->textureRgb(textureId);
        light = QColor(lr, lg, lb);
        dark = QColor(dr, dg, db);
        face = QColor(fr, fg, fb);
    } else {
        // Default colors if no palette
        light = QColor(240, 240, 240);
        dark = QColor(100, 100, 100);
        face = QColor(200, 200, 200);
    }
}

void Renderer::drawFrame(const QRect& rect, ZoneFrame frame, uint8_t textureId) {
    switch (frame) {
        case ZoneFrame::Raised:
        case ZoneFrame::Raised1:
        case ZoneFrame::Raised2:
        case ZoneFrame::Raised3:
            drawRaisedFrame(rect, textureId);
            break;
            
        case ZoneFrame::Inset:
        case ZoneFrame::Inset1:
        case ZoneFrame::Inset2:
        case ZoneFrame::Inset3:
            drawInsetFrame(rect, textureId);
            break;
            
        case ZoneFrame::Double:
        case ZoneFrame::Double1:
        case ZoneFrame::Double2:
        case ZoneFrame::Double3:
            drawDoubleFrame(rect, textureId);
            break;
            
        case ZoneFrame::Border:
        case ZoneFrame::ClearBorder:
        case ZoneFrame::SandBorder:
        case ZoneFrame::LitSandBorder:
        case ZoneFrame::InsetBorder:
        case ZoneFrame::ParchmentBorder:
        case ZoneFrame::DoubleBorder:
        case ZoneFrame::LitDoubleBorder:
            drawBorderFrame(rect, textureId);
            break;
            
        default:
            fillRect(rect, textureId);
            break;
    }
}

void Renderer::drawRaisedFrame(const QRect& rect, uint8_t textureId) {
    if (!painter_.isActive()) return;
    
    QColor light, dark, face;
    getFrameColors(textureId, light, dark, face);
    
    QRect r = scaleRect(rect);
    
    // Draw raised edges (light on top/left, dark on bottom/right)
    // Texture is drawn separately, we just draw the 3D frame edges
    painter_.setPen(QPen(light, 2));
    painter_.drawLine(r.left(), r.top(), r.right() - 1, r.top());     // top
    painter_.drawLine(r.left(), r.top(), r.left(), r.bottom() - 1);   // left
    
    painter_.setPen(QPen(dark, 2));
    painter_.drawLine(r.right(), r.top(), r.right(), r.bottom());     // right
    painter_.drawLine(r.left(), r.bottom(), r.right(), r.bottom());   // bottom
}

void Renderer::drawInsetFrame(const QRect& rect, uint8_t textureId) {
    if (!painter_.isActive()) return;
    
    QColor light, dark, face;
    getFrameColors(textureId, light, dark, face);
    
    QRect r = scaleRect(rect);
    
    // Draw inset edges (dark on top/left, light on bottom/right)
    // Texture is drawn separately, we just draw the 3D frame edges
    painter_.setPen(QPen(dark, 2));
    painter_.drawLine(r.left(), r.top(), r.right() - 1, r.top());     // top
    painter_.drawLine(r.left(), r.top(), r.left(), r.bottom() - 1);   // left
    
    painter_.setPen(QPen(light, 2));
    painter_.drawLine(r.right(), r.top(), r.right(), r.bottom());     // right
    painter_.drawLine(r.left(), r.bottom(), r.right(), r.bottom());   // bottom
}

void Renderer::drawDoubleFrame(const QRect& rect, uint8_t textureId) {
    if (!painter_.isActive()) return;
    
    QColor light, dark, face;
    getFrameColors(textureId, light, dark, face);
    
    QRect r = scaleRect(rect);
    
    // Texture is drawn separately, we just draw the 3D frame edges
    // Outer frame (raised)
    painter_.setPen(QPen(light, 2));
    painter_.drawLine(r.left(), r.top(), r.right() - 1, r.top());
    painter_.drawLine(r.left(), r.top(), r.left(), r.bottom() - 1);
    painter_.setPen(QPen(dark, 2));
    painter_.drawLine(r.right(), r.top(), r.right(), r.bottom());
    painter_.drawLine(r.left(), r.bottom(), r.right(), r.bottom());
    
    // Inner frame (inset)
    int inset = 4;
    QRect inner = r.adjusted(inset, inset, -inset, -inset);
    painter_.setPen(QPen(dark, 1));
    painter_.drawLine(inner.left(), inner.top(), inner.right(), inner.top());
    painter_.drawLine(inner.left(), inner.top(), inner.left(), inner.bottom());
    painter_.setPen(QPen(light, 1));
    painter_.drawLine(inner.right(), inner.top(), inner.right(), inner.bottom());
    painter_.drawLine(inner.left(), inner.bottom(), inner.right(), inner.bottom());
}

void Renderer::drawBorderFrame(const QRect& rect, uint8_t textureId) {
    if (!painter_.isActive()) return;
    
    QColor light, dark, face;
    getFrameColors(textureId, light, dark, face);
    
    QRect r = scaleRect(rect);
    
    // Texture is drawn separately, we just draw the border
    painter_.setPen(QPen(dark, 2));
    painter_.setBrush(Qt::NoBrush);
    painter_.drawRect(r);
}

void Renderer::drawSandFrame(const QRect& rect, uint8_t textureId) {
    // Sand frame is similar to raised but with texture effects
    drawRaisedFrame(rect, textureId);
}

void Renderer::fillRect(const QRect& rect, uint8_t textureId) {
    if (!painter_.isActive()) return;
    
    QRect r = scaleRect(rect);
    
    if (textures_) {
        QPixmap tex = textures_->texture(textureId);
        if (!tex.isNull()) {
            painter_.drawTiledPixmap(r, tex);
            return;
        }
    }
    
    // Fall back to solid color
    if (palette_) {
        auto [cr, cg, cb] = palette_->textureRgb(textureId);
        painter_.fillRect(r, QColor(cr, cg, cb));
    } else {
        painter_.fillRect(r, QColor(200, 200, 200));
    }
}

void Renderer::fillRect(const QRect& rect, const QColor& c) {
    if (!painter_.isActive()) return;
    painter_.fillRect(scaleRect(rect), c);
}

void Renderer::drawText(const QString& text, const QRect& rect,
                        uint8_t fontId, uint8_t colorId, TextAlign align) {
    if (!painter_.isActive() || text.isEmpty()) return;
    
    QRect r = scaleRect(rect);
    
    // Set font
    QFont f = font(fontId);
    painter_.setFont(f);
    
    // Set color
    painter_.setPen(color(colorId));
    
    // Map alignment
    Qt::Alignment qtAlign = Qt::AlignCenter;
    switch (align) {
        case TextAlign::Left:
            qtAlign = Qt::AlignLeft | Qt::AlignVCenter;
            break;
        case TextAlign::Right:
            qtAlign = Qt::AlignRight | Qt::AlignVCenter;
            break;
        case TextAlign::Center:
        default:
            qtAlign = Qt::AlignCenter;
            break;
    }
    
    painter_.drawText(r, qtAlign, text);
}

void Renderer::drawText(const QString& text, int x, int y,
                        uint8_t fontId, uint8_t colorId) {
    if (!painter_.isActive() || text.isEmpty()) return;
    
    painter_.setFont(font(fontId));
    painter_.setPen(color(colorId));
    painter_.drawText(scaleX(x), scaleY(y), text);
}

QSize Renderer::textSize(const QString& text, uint8_t fontId) const {
    QFontMetrics fm(font(fontId));
    return QSize(fm.horizontalAdvance(text), fm.height());
}

int Renderer::textWidth(const QString& text, uint8_t fontId) const {
    QFontMetrics fm(font(fontId));
    return fm.horizontalAdvance(text);
}

int Renderer::textHeight(uint8_t fontId) const {
    QFontMetrics fm(font(fontId));
    return fm.height();
}

void Renderer::drawImage(const QPixmap& image, const QRect& rect) {
    if (!painter_.isActive() || image.isNull()) return;
    painter_.drawPixmap(scaleRect(rect), image);
}

void Renderer::drawImage(const QPixmap& image, int x, int y) {
    if (!painter_.isActive() || image.isNull()) return;
    painter_.drawPixmap(scaleX(x), scaleY(y), image);
}

void Renderer::drawLine(int x1, int y1, int x2, int y2, const QColor& c, int width) {
    if (!painter_.isActive()) return;
    painter_.setPen(QPen(c, width));
    painter_.drawLine(scaleX(x1), scaleY(y1), scaleX(x2), scaleY(y2));
}

void Renderer::drawLine(int x1, int y1, int x2, int y2, uint8_t colorId, int width) {
    drawLine(x1, y1, x2, y2, color(colorId), width);
}

void Renderer::drawRect(const QRect& rect, const QColor& c, int width) {
    if (!painter_.isActive()) return;
    painter_.setPen(QPen(c, width));
    painter_.setBrush(Qt::NoBrush);
    painter_.drawRect(scaleRect(rect));
}

void Renderer::drawRect(const QRect& rect, uint8_t colorId, int width) {
    drawRect(rect, color(colorId), width);
}

QColor Renderer::color(uint8_t colorId) const {
    if (palette_) {
        auto [r, g, b] = palette_->rgb(colorId);
        return QColor(r, g, b);
    }
    // Default to black
    return QColor(0, 0, 0);
}

QFont Renderer::font(uint8_t fontId) const {
    if (fontManager_) {
        return fontManager_->font(static_cast<FontId>(fontId));
    }
    return QFont();
}

} // namespace vt
