/**
 * @file property_editor.hpp
 * @brief Property editor widget for zones
 */

#pragma once

#include "ui/zone.hpp"
#include <QWidget>
#include <QFormLayout>

namespace vt2 {

/**
 * @brief Widget for editing zone properties
 * 
 * Used in design mode to configure zones visually.
 */
class PropertyEditor : public QWidget {
    Q_OBJECT

public:
    explicit PropertyEditor(QWidget* parent = nullptr);
    ~PropertyEditor() override;
    
    void setZone(Zone* zone);
    Zone* zone() const { return zone_; }
    
signals:
    void propertyChanged(const QString& name, const QVariant& value);
    
private:
    void buildUI();
    void clear();
    
    Zone* zone_ = nullptr;
    QFormLayout* layout_ = nullptr;
};

} // namespace vt2
