/*
 * ViewTouch V2 - Zone Properties Dialog
 * Edit all properties of a zone
 */

#pragma once

#include "core/types.hpp"
#include "core/colors.hpp"
#include "core/fonts.hpp"

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>

namespace vt {

class Zone;
class ColorComboBox;
class TextureComboBox;
class FrameComboBox;
class FontComboBox;
class BehaviorComboBox;
class ShapeComboBox;
class ZoneTypeComboBox;

/*************************************************************
 * ZonePropertiesDialog - Edit zone properties
 *************************************************************/
class ZonePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ZonePropertiesDialog(Zone* zone, QWidget* parent = nullptr);
    ~ZonePropertiesDialog();

    void applyChanges();

private slots:
    void onApply();
    void onOk();
    void onStateTabChanged(int index);
    void updatePreview();

private:
    void setupUi();
    void setupGeneralTab();
    void setupAppearanceTab();
    void setupBehaviorTab();
    void loadFromZone();
    void saveToZone();

    Zone* zone_;
    
    // General tab
    QLineEdit* nameEdit_;
    QSpinBox* xSpinBox_;
    QSpinBox* ySpinBox_;
    QSpinBox* widthSpinBox_;
    QSpinBox* heightSpinBox_;
    QSpinBox* groupSpinBox_;
    ZoneTypeComboBox* zoneTypeCombo_;
    
    // Appearance tab - for each state
    QTabWidget* stateTabWidget_;
    
    struct StateWidgets {
        FrameComboBox* frameCombo;
        TextureComboBox* textureCombo;
        ColorComboBox* colorCombo;
    };
    StateWidgets stateWidgets_[3];
    
    // Font
    FontComboBox* fontCombo_;
    ShapeComboBox* shapeCombo_;
    QSpinBox* shadowSpinBox_;
    
    // Behavior tab
    BehaviorComboBox* behaviorCombo_;
    QCheckBox* activeCheck_;
    QCheckBox* editCheck_;
    QCheckBox* stayLitCheck_;
    QSpinBox* keySpinBox_;
    
    // Preview widget
    QWidget* previewWidget_;
};

/*************************************************************
 * ColorComboBox - Combo box for selecting colors
 *************************************************************/
class ColorComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit ColorComboBox(QWidget* parent = nullptr);
    
    void setCurrentColorId(uint8_t id);
    uint8_t currentColorId() const;
};

/*************************************************************
 * TextureComboBox - Combo box for selecting textures
 *************************************************************/
class TextureComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit TextureComboBox(QWidget* parent = nullptr);
    
    void setCurrentTextureId(uint8_t id);
    uint8_t currentTextureId() const;
};

/*************************************************************
 * FrameComboBox - Combo box for selecting frame types
 *************************************************************/
class FrameComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit FrameComboBox(QWidget* parent = nullptr);
    
    void setCurrentFrame(ZoneFrame frame);
    ZoneFrame currentFrame() const;
};

/*************************************************************
 * FontComboBox - Combo box for selecting fonts
 *************************************************************/
class FontComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit FontComboBox(QWidget* parent = nullptr);
    
    void setCurrentFontId(FontId id);
    FontId currentFontId() const;
};

/*************************************************************
 * BehaviorComboBox - Combo box for selecting behaviors
 *************************************************************/
class BehaviorComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit BehaviorComboBox(QWidget* parent = nullptr);
    
    void setCurrentBehavior(ZoneBehavior behavior);
    ZoneBehavior currentBehavior() const;
};

/*************************************************************
 * ShapeComboBox - Combo box for selecting shapes
 *************************************************************/
class ShapeComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit ShapeComboBox(QWidget* parent = nullptr);
    
    void setCurrentShape(ZoneShape shape);
    ZoneShape currentShape() const;
};

/*************************************************************
 * ZoneTypeComboBox - Combo box for selecting zone types
 *************************************************************/
class ZoneTypeComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit ZoneTypeComboBox(QWidget* parent = nullptr);
    
    void setCurrentZoneType(ZoneType type);
    ZoneType currentZoneType() const;
};

} // namespace vt
