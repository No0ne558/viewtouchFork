/*
 * ViewTouch V2 - Textures Implementation
 * Loads XPM texture files matching original ViewTouch
 */

#include "render/textures.hpp"
#include "core/colors.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QDebug>

namespace vt {

// Texture filename mapping - matches original ViewTouch exactly
static const char* TextureFiles[] = {
    "sand-8.xpm",                    // 0 - Sand
    "litsand-6.xpm",                 // 1 - LitSand
    "darksand-6.xpm",                // 2 - DarkSand
    "litewood-8.xpm",                // 3 - LiteWood
    "wood-10.xpm",                   // 4 - Wood
    "darkwood-10.xpm",               // 5 - DarkWood
    "grayparchment-8.xpm",           // 6 - GrayParchment
    "graymarble-12.xpm",             // 7 - GrayMarble
    "greenmarble-12.xpm",            // 8 - GreenMarble
    "parchment-6.xpm",               // 9 - Parchment
    "pearl-8.xpm",                   // 10 - Pearl
    "canvas-8.xpm",                  // 11 - Canvas
    "tanparchment-8.xpm",            // 12 - TanParchment
    "smoke-4.xpm",                   // 13 - Smoke
    "leather-8.xpm",                 // 14 - Leather
    "blueparchment.xpm",             // 15 - BlueParchment
    "gradient-8.xpm",                // 16 - Gradient
    "gradient-brown.xpm",            // 17 - GradientBrown
    "black.xpm",                     // 18 - Black
    "greySand.xpm",                  // 19 - GreySand
    "whiteMesh.xpm",                 // 20 - WhiteMesh
    "carbonfiber-128-6.xpm",         // 21 - CarbonFiber
    "whitetexture-128-32.xpm",       // 22 - WhiteTexture
    "darkorangetexture-128-32.xpm",  // 23 - DarkOrangeTexture
    "yellowtexture-128-32.xpm",      // 24 - YellowTexture
    "greentexture-128-32.xpm",       // 25 - GreenTexture
    "orangetexture-128-32.xpm",      // 26 - OrangeTexture
    "bluetexture-128-32.xpm",        // 27 - BlueTexture
    "pooltable-256.xpm",             // 28 - PoolTable
    "test-256.xpm",                  // 29 - Test
    "diamondleather-256.xpm",        // 30 - DiamondLeather
    "bread-256.xpm",                 // 31 - Bread
    "lava-256.xpm",                  // 32 - Lava
    "darkmarble-256.xpm",            // 33 - DarkMarble
};

constexpr int TextureFileCount = sizeof(TextureFiles) / sizeof(TextureFiles[0]);

Textures::Textures(QObject* parent)
    : QObject(parent)
{
}

Textures::~Textures() = default;

bool Textures::loadAll() {
    bool anyLoaded = false;
    
    // Try common locations first, then fall back to basePath_
    QStringList searchPaths = {
        QStringLiteral("assets/images/xpm"),
        QStringLiteral("../assets/images/xpm"),
        QStringLiteral("../../assets/images/xpm"),
        QStringLiteral("/home/ariel/Documents/viewtouchFork/v2/assets/images/xpm"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../assets/images/xpm"),
    };
    
    // Add basePath_ if set
    if (!basePath_.isEmpty()) {
        searchPaths.append(basePath_);
    }
    
    QString path;
    for (const QString& sp : searchPaths) {
        QDir dir(sp);
        if (dir.exists() && dir.exists(QStringLiteral("sand-8.xpm"))) {
            path = dir.absolutePath();
            break;
        }
    }
    
    if (path.isEmpty()) {
        qWarning() << "Textures: Could not find texture directory, using procedural";
        generateProceduralTextures();
        return false;
    }
    
    fprintf(stderr, "Textures: Loading from %s\n", path.toUtf8().constData());
    
    QDir dir(path);
    
    // Load each texture file
    for (int i = 0; i < TextureFileCount; ++i) {
        QString filepath = dir.filePath(QString::fromLatin1(TextureFiles[i]));
        QPixmap pix(filepath);
        
        if (!pix.isNull()) {
            textures_[static_cast<uint8_t>(i)] = pix;
            anyLoaded = true;
        } else {
            fprintf(stderr, "Textures: Failed to load %s\n", filepath.toUtf8().constData());
        }
    }
    
    fprintf(stderr, "Textures: Loaded %zu textures\n", textures_.size());
    
    // Generate procedural fallbacks for any missing
    generateProceduralTextures();
    
    return anyLoaded;
}

QPixmap Textures::texture(uint8_t textureId) const {
    // Handle special values
    if (textureId == TEXTURE_CLEAR || textureId == TEXTURE_DEFAULT || 
        textureId == TEXTURE_UNCHANGED) {
        // Return sand as default
        auto it = textures_.find(static_cast<uint8_t>(TextureId::Sand));
        if (it != textures_.end()) {
            return it->second;
        }
    }
    
    auto it = textures_.find(textureId);
    if (it != textures_.end()) {
        return it->second;
    }
    
    // Fallback to sand
    it = textures_.find(static_cast<uint8_t>(TextureId::Sand));
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
    // Generate procedural textures only for missing ones
    for (int i = 0; i < static_cast<int>(TextureId::Count); ++i) {
        uint8_t id = static_cast<uint8_t>(i);
        if (!hasTexture(id)) {
            textures_[id] = generateTexture(id);
        }
    }
}

QPixmap Textures::generateTexture(uint8_t textureId) const {
    // Create a simple colored texture as fallback
    int size = 32;
    QPixmap pix(size, size);
    
    // Base colors for each texture type
    QColor baseColor;
    
    TextureId texId = static_cast<TextureId>(textureId);
    switch (texId) {
        case TextureId::Sand:
        case TextureId::LitSand:
        case TextureId::DarkSand:
        case TextureId::GreySand:
            baseColor = QColor(194, 178, 128); // Sandy tan
            if (texId == TextureId::LitSand) baseColor = baseColor.lighter(120);
            if (texId == TextureId::DarkSand) baseColor = baseColor.darker(120);
            if (texId == TextureId::GreySand) baseColor = QColor(160, 160, 140);
            break;
            
        case TextureId::Wood:
        case TextureId::LiteWood:
        case TextureId::DarkWood:
            baseColor = QColor(139, 90, 43); // Wood brown
            if (texId == TextureId::LiteWood) baseColor = baseColor.lighter(130);
            if (texId == TextureId::DarkWood) baseColor = baseColor.darker(130);
            break;
            
        case TextureId::Parchment:
        case TextureId::GrayParchment:
        case TextureId::TanParchment:
        case TextureId::BlueParchment:
            baseColor = QColor(245, 235, 200); // Parchment
            if (texId == TextureId::GrayParchment) baseColor = QColor(200, 200, 195);
            if (texId == TextureId::TanParchment) baseColor = QColor(210, 180, 140);
            if (texId == TextureId::BlueParchment) baseColor = QColor(180, 200, 220);
            break;
            
        case TextureId::GrayMarble:
        case TextureId::GreenMarble:
        case TextureId::DarkMarble:
            baseColor = QColor(180, 180, 180); // Gray marble
            if (texId == TextureId::GreenMarble) baseColor = QColor(100, 140, 100);
            if (texId == TextureId::DarkMarble) baseColor = QColor(60, 60, 60);
            break;
            
        case TextureId::Pearl:
            baseColor = QColor(240, 235, 225);
            break;
            
        case TextureId::Canvas:
            baseColor = QColor(200, 195, 180);
            break;
            
        case TextureId::Smoke:
            baseColor = QColor(100, 100, 100);
            break;
            
        case TextureId::Leather:
        case TextureId::DiamondLeather:
            baseColor = QColor(80, 50, 30);
            break;
            
        case TextureId::Gradient:
        case TextureId::GradientBrown:
            baseColor = QColor(100, 100, 150);
            if (texId == TextureId::GradientBrown) baseColor = QColor(120, 80, 50);
            break;
            
        case TextureId::Black:
            baseColor = QColor(20, 20, 20);
            break;
            
        case TextureId::WhiteMesh:
        case TextureId::WhiteTexture:
            baseColor = QColor(240, 240, 240);
            break;
            
        case TextureId::CarbonFiber:
            baseColor = QColor(40, 40, 45);
            break;
            
        case TextureId::DarkOrangeTexture:
        case TextureId::OrangeTexture:
            baseColor = QColor(255, 140, 0);
            if (texId == TextureId::DarkOrangeTexture) baseColor = baseColor.darker(140);
            break;
            
        case TextureId::YellowTexture:
            baseColor = QColor(255, 220, 50);
            break;
            
        case TextureId::GreenTexture:
            baseColor = QColor(50, 150, 50);
            break;
            
        case TextureId::BlueTexture:
            baseColor = QColor(50, 100, 180);
            break;
            
        case TextureId::PoolTable:
            baseColor = QColor(0, 100, 60);
            break;
            
        case TextureId::Test:
            baseColor = QColor(255, 0, 255); // Magenta for visibility
            break;
            
        case TextureId::Bread:
            baseColor = QColor(200, 150, 80);
            break;
            
        case TextureId::Lava:
            baseColor = QColor(200, 50, 0);
            break;
            
        default:
            baseColor = QColor(128, 128, 128);
            break;
    }
    
    pix.fill(baseColor);
    
    // Add some simple texture pattern
    QPainter painter(&pix);
    painter.setPen(QPen(baseColor.darker(110), 1));
    
    for (int y = 0; y < size; y += 4) {
        for (int x = 0; x < size; x += 4) {
            if ((x + y) % 8 == 0) {
                painter.drawPoint(x + 1, y + 1);
            }
        }
    }
    
    painter.end();
    return pix;
}

} // namespace vt
