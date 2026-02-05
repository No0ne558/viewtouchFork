/*
 * ViewTouch V2 - Textures Class
 * Manages texture images for zone backgrounds
 */

#pragma once

#include "core/types.hpp"

#include <QObject>
#include <QPixmap>
#include <QString>
#include <map>

namespace vt {

/*************************************************************
 * Textures - Texture image management
 *************************************************************/
class Textures : public QObject {
    Q_OBJECT
    
public:
    explicit Textures(QObject* parent = nullptr);
    ~Textures();
    
    // Set base path for texture images
    void setBasePath(const QString& path) { basePath_ = path; }
    QString basePath() const { return basePath_; }
    
    // Load all textures
    bool loadAll();
    
    // Get a texture by ID
    QPixmap texture(uint8_t textureId) const;
    
    // Check if texture exists
    bool hasTexture(uint8_t textureId) const;
    
    // Clear all loaded textures
    void clear();
    
    // Generate procedural textures (fallback)
    void generateProceduralTextures();
    
private:
    // Generate a single procedural texture
    QPixmap generateTexture(uint8_t textureId) const;
    
    QString basePath_;
    std::map<uint8_t, QPixmap> textures_;
};

} // namespace vt
