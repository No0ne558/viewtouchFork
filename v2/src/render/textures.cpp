/*
 * ViewTouch V2 - Textures Implementation
 */

#include "render/textures.hpp"
#include "core/colors.hpp"

#include <QDir>
#include <QPainter>
#include <QRandomGenerator>

namespace vt {

Textures::Textures(QObject* parent)
    : QObject(parent)
{
}

Textures::~Textures() = default;

bool Textures::loadAll() {
    if (basePath_.isEmpty()) {
        generateProceduralTextures();
        return true;
    }
    
    QDir dir(basePath_);
    if (!dir.exists()) {
        generateProceduralTextures();
        return true;
    }
    
    // Try to load texture files
    // Original ViewTouch uses .xpm or .png files
    // File naming convention: texture_XX.png where XX is the ID
    
    bool anyLoaded = false;
    for (int i = 0; i < 64; ++i) {
        QString filename = QString("texture_%1.png").arg(i, 2, 10, QChar('0'));
        QString filepath = dir.filePath(filename);
        
        QPixmap pix(filepath);
        if (!pix.isNull()) {
            textures_[static_cast<uint8_t>(i)] = pix;
            anyLoaded = true;
        }
    }
    
    // Generate procedural textures for any missing ones
    generateProceduralTextures();
    
    return anyLoaded;
}

QPixmap Textures::texture(uint8_t textureId) const {
    auto it = textures_.find(textureId);
    if (it != textures_.end()) {
        return it->second;
    }
    return QPixmap();
}

bool Textures::hasTexture(uint8_t textureId) const {
    return textures_.find(textureId) != textures_.end();
}

void Textures::clear() {
    textures_.clear();
}

void Textures::generateProceduralTextures() {
    // Generate procedural textures for standard ViewTouch textures
    // These are placeholders - real textures should be loaded from files
    
    auto addIfMissing = [this](uint8_t id) {
        if (!hasTexture(id)) {
            textures_[id] = generateTexture(id);
        }
    };
    
    // Generate all standard textures
    for (int i = static_cast<int>(TextureId::Default); 
         i <= static_cast<int>(TextureId::DarkBlueGreen); ++i) {
        addIfMissing(static_cast<uint8_t>(i));
    }
}

QPixmap Textures::generateTexture(uint8_t textureId) const {
    // Get base color from palette
    ColorPalette& palette = ColorPalette::instance();
    auto [r, g, b] = palette.textureRgb(textureId);
    QColor baseColor(r, g, b);
    
    // Create a small tileable texture (32x32)
    int size = 32;
    QPixmap pix(size, size);
    pix.fill(baseColor);
    
    QPainter painter(&pix);
    
    // Add some texture variation based on type
    TextureId texId = static_cast<TextureId>(textureId);
    
    switch (texId) {
        case TextureId::Sand:
        case TextureId::LiteSand:
        case TextureId::DarkSand: {
            // Sandy speckled texture
            QRandomGenerator* rng = QRandomGenerator::global();
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    if (rng->bounded(10) < 3) {
                        int var = rng->bounded(-20, 20);
                        QColor c(
                            qBound(0, baseColor.red() + var, 255),
                            qBound(0, baseColor.green() + var, 255),
                            qBound(0, baseColor.blue() + var, 255)
                        );
                        painter.setPen(c);
                        painter.drawPoint(x, y);
                    }
                }
            }
            break;
        }
        
        case TextureId::Wood:
        case TextureId::LiteWood:
        case TextureId::DarkWood: {
            // Wood grain lines
            painter.setPen(QPen(baseColor.darker(110), 1));
            for (int y = 0; y < size; y += 4) {
                int offset = (y / 4) % 3;
                painter.drawLine(0, y + offset, size, y + offset);
            }
            break;
        }
        
        case TextureId::Parchment:
        case TextureId::LiteParchment:
        case TextureId::DarkParchment: {
            // Subtle parchment texture
            QRandomGenerator* rng = QRandomGenerator::global();
            for (int i = 0; i < 50; ++i) {
                int x = rng->bounded(size);
                int y = rng->bounded(size);
                int var = rng->bounded(-10, 10);
                QColor c(
                    qBound(0, baseColor.red() + var, 255),
                    qBound(0, baseColor.green() + var, 255),
                    qBound(0, baseColor.blue() + var, 255)
                );
                painter.setPen(c);
                painter.drawPoint(x, y);
            }
            break;
        }
        
        case TextureId::Marble:
        case TextureId::LiteMarble:
        case TextureId::DarkMarble: {
            // Marble veins
            painter.setPen(QPen(baseColor.darker(120), 1));
            QRandomGenerator* rng = QRandomGenerator::global();
            int x = 0, y = rng->bounded(size);
            while (x < size) {
                int nx = x + rng->bounded(2, 6);
                int ny = y + rng->bounded(-2, 3);
                ny = qBound(0, ny, size - 1);
                painter.drawLine(x, y, nx, ny);
                x = nx;
                y = ny;
            }
            break;
        }
        
        case TextureId::Leather:
        case TextureId::LiteLeather:
        case TextureId::DarkLeather: {
            // Leather texture (small bumps)
            QRandomGenerator* rng = QRandomGenerator::global();
            for (int y = 0; y < size; y += 4) {
                for (int x = 0; x < size; x += 4) {
                    int ox = rng->bounded(2);
                    int oy = rng->bounded(2);
                    painter.setPen(baseColor.lighter(110));
                    painter.drawPoint(x + ox, y + oy);
                    painter.setPen(baseColor.darker(110));
                    painter.drawPoint(x + ox + 1, y + oy + 1);
                }
            }
            break;
        }
        
        case TextureId::Canvas:
        case TextureId::LiteCanvas:
        case TextureId::DarkCanvas: {
            // Canvas weave pattern
            for (int y = 0; y < size; y += 2) {
                for (int x = 0; x < size; x += 2) {
                    if ((x / 2 + y / 2) % 2 == 0) {
                        painter.setPen(baseColor.lighter(105));
                    } else {
                        painter.setPen(baseColor.darker(105));
                    }
                    painter.drawRect(x, y, 1, 1);
                }
            }
            break;
        }
        
        default:
            // Solid color (already filled)
            break;
    }
    
    painter.end();
    return pix;
}

} // namespace vt
