/*
 * ViewTouch V2 - Button Properties Dialog
 * Edit all properties of a button (zone)
 * 
 * This matches the original ViewTouch "Button Properties Dialog"
 * which dynamically shows/hides fields based on button type.
 */

#pragma once

#include "core/types.hpp"
#include "core/colors.hpp"
#include "core/fonts.hpp"

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QFormLayout>

namespace vt {

class Zone;
class ColorComboBox;
class TextureComboBox;
class FrameComboBox;
class FontComboBox;
class BehaviorComboBox;
class ShapeComboBox;
class ZoneTypeComboBox;
class JumpTypeComboBox;
class TenderTypeComboBox;
class ReportTypeComboBox;
class SwitchTypeComboBox;
class QualifierComboBox;
class CustomerTypeComboBox;
class ShadowComboBox;
class ItemTypeComboBox;
class ItemFamilyComboBox;
class SalesTypeComboBox;
class PrinterComboBox;
class CallOrderComboBox;
class Page;

/*************************************************************
 * ZonePropertiesDialog - Edit button/zone properties
 * (Named to match original ViewTouch terminology)
 *************************************************************/
class ZonePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ZonePropertiesDialog(Zone* zone, Page* page = nullptr, QWidget* parent = nullptr);
    ~ZonePropertiesDialog();

    void applyChanges();
    
    // Returns true if zone was replaced (type changed)
    bool wasZoneReplaced() const { return zoneReplaced_; }
    
    // Get the new zone if it was replaced
    Zone* replacementZone() const { return replacementZone_; }

private slots:
    void onApply();
    void onOk();
    void onStateTabChanged(int index);
    void onZoneTypeChanged(int index);
    void onJumpTypeChanged(int index);
    void onItemTypeChanged(int index);
    void onReportTypeChanged(int index);
    void updatePreview();
    void updateFieldVisibility();

private:
    void setupUi();
    void setupGeneralTab();
    void setupAppearanceTab();
    void setupBehaviorTab();
    void setupActionsTab();
    void setupItemTab();
    void loadFromZone();
    void saveToZone();
    void replaceZoneIfTypeChanged();
    void applyZoneTypeDefaults(ZoneType type);

    Zone* zone_;
    Page* page_ = nullptr;
    ZoneType originalType_ = ZoneType::Undefined;
    bool zoneReplaced_ = false;
    Zone* replacementZone_ = nullptr;
    
    // Tab widget
    QTabWidget* mainTabWidget_;
    
    // General tab
    QLineEdit* nameEdit_;
    QSpinBox* xSpinBox_;
    QSpinBox* ySpinBox_;
    QSpinBox* widthSpinBox_;
    QSpinBox* heightSpinBox_;
    QSpinBox* groupSpinBox_;
    ZoneTypeComboBox* zoneTypeCombo_;
    QLineEdit* pageEdit_;
    
    // Appearance tab - for each state
    QTabWidget* stateTabWidget_;
    
    struct StateWidgets {
        FrameComboBox* frameCombo;
        TextureComboBox* textureCombo;
        ColorComboBox* colorCombo;
    };
    StateWidgets stateWidgets_[3];
    
    // Font and shape
    FontComboBox* fontCombo_;
    ShapeComboBox* shapeCombo_;
    ShadowComboBox* shadowCombo_;
    
    // Behavior tab
    BehaviorComboBox* behaviorCombo_;
    QCheckBox* activeCheck_;
    QCheckBox* editCheck_;
    QCheckBox* stayLitCheck_;
    QSpinBox* keySpinBox_;
    
    // Actions tab (for buttons that do something)
    QWidget* actionsTab_;
    QFormLayout* actionsLayout_;
    
    // Jump fields
    QLabel* jumpTypeLabel_;
    JumpTypeComboBox* jumpTypeCombo_;
    QLabel* jumpIdLabel_;
    QSpinBox* jumpIdSpinBox_;
    
    // Message/Expression
    QLabel* messageLabel_;
    QLineEdit* messageEdit_;
    QLabel* expressionLabel_;
    QLineEdit* expressionEdit_;
    
    // Confirmation
    QLabel* confirmLabel_;
    QCheckBox* confirmCheck_;
    QLabel* confirmMsgLabel_;
    QLineEdit* confirmMsgEdit_;
    
    // Tender fields
    QLabel* tenderTypeLabel_;
    TenderTypeComboBox* tenderTypeCombo_;
    QLabel* tenderAmountLabel_;
    QDoubleSpinBox* tenderAmountSpinBox_;
    
    // Report fields
    QLabel* reportTypeLabel_;
    ReportTypeComboBox* reportTypeCombo_;
    QLabel* reportPrintLabel_;
    QComboBox* reportPrintCombo_;
    QLabel* checkDispLabel_;
    QSpinBox* checkDispSpinBox_;
    QLabel* videoTargetLabel_;
    PrinterComboBox* videoTargetCombo_;
    
    // Switch fields
    QLabel* switchTypeLabel_;
    SwitchTypeComboBox* switchTypeCombo_;
    
    // Qualifier fields
    QLabel* qualifierLabel_;
    QualifierComboBox* qualifierCombo_;
    
    // Customer type
    QLabel* customerTypeLabel_;
    CustomerTypeComboBox* customerTypeCombo_;
    
    // Drawer zone type
    QLabel* drawerZoneTypeLabel_;
    QComboBox* drawerZoneTypeCombo_;
    
    // Spacing/Amount
    QLabel* spacingLabel_;
    QSpinBox* spacingSpinBox_;
    QLabel* amountLabel_;
    QSpinBox* amountSpinBox_;
    
    // Filename
    QLabel* filenameLabel_;
    QLineEdit* filenameEdit_;
    QLabel* imageFilenameLabel_;
    QComboBox* imageFilenameCombo_;
    
    // Item tab (for menu items)
    QWidget* itemTab_;
    QFormLayout* itemLayout_;
    
    // Item fields
    QLabel* itemTypeLabel_;
    ItemTypeComboBox* itemTypeCombo_;
    QLabel* itemNameLabel_;
    QLineEdit* itemNameEdit_;
    QLabel* itemZoneNameLabel_;
    QLineEdit* itemZoneNameEdit_;
    QLabel* itemPrintNameLabel_;
    QLineEdit* itemPrintNameEdit_;
    QLabel* itemPriceLabel_;
    QDoubleSpinBox* itemPriceSpinBox_;
    QLabel* itemSubpriceLabel_;
    QDoubleSpinBox* itemSubpriceSpinBox_;
    QLabel* itemEmployeePriceLabel_;
    QDoubleSpinBox* itemEmployeePriceSpinBox_;
    QLabel* itemFamilyLabel_;
    ItemFamilyComboBox* itemFamilyCombo_;
    QLabel* itemSalesLabel_;
    SalesTypeComboBox* itemSalesCombo_;
    QLabel* itemPrinterLabel_;
    PrinterComboBox* itemPrinterCombo_;
    QLabel* itemCallOrderLabel_;
    CallOrderComboBox* itemCallOrderCombo_;
    QLabel* pageListLabel_;
    QLineEdit* pageListEdit_;
    
    // Admission fields
    QLabel* itemLocationLabel_;
    QLineEdit* itemLocationEdit_;
    QLabel* itemEventTimeLabel_;
    QLineEdit* itemEventTimeEdit_;
    QLabel* itemTotalTicketsLabel_;
    QSpinBox* itemTotalTicketsSpinBox_;
    QLabel* itemPriceLabelLabel_;
    QLineEdit* itemPriceLabelEdit_;
    
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

/*************************************************************
 * JumpTypeComboBox - Combo box for selecting jump types
 *************************************************************/
class JumpTypeComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit JumpTypeComboBox(QWidget* parent = nullptr);
    
    void setCurrentJumpType(JumpType type);
    JumpType currentJumpType() const;
};

/*************************************************************
 * TenderTypeComboBox - Combo box for selecting tender types
 *************************************************************/
class TenderTypeComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit TenderTypeComboBox(QWidget* parent = nullptr);
    
    void setCurrentTenderType(int type);
    int currentTenderType() const;
};

/*************************************************************
 * ReportTypeComboBox - Combo box for selecting report types
 *************************************************************/
class ReportTypeComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit ReportTypeComboBox(QWidget* parent = nullptr);
    
    void setCurrentReportType(int type);
    int currentReportType() const;
};

/*************************************************************
 * SwitchTypeComboBox - Combo box for selecting switch types
 *************************************************************/
class SwitchTypeComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit SwitchTypeComboBox(QWidget* parent = nullptr);
    
    void setCurrentSwitchType(int type);
    int currentSwitchType() const;
};

/*************************************************************
 * QualifierComboBox - Combo box for selecting qualifiers
 *************************************************************/
class QualifierComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit QualifierComboBox(QWidget* parent = nullptr);
    
    void setCurrentQualifier(int type);
    int currentQualifier() const;
};

/*************************************************************
 * CustomerTypeComboBox - Combo box for selecting customer types
 *************************************************************/
class CustomerTypeComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit CustomerTypeComboBox(QWidget* parent = nullptr);
    
    void setCurrentCustomerType(int type);
    int currentCustomerType() const;
};

/*************************************************************
 * ItemTypeComboBox - Combo box for selecting item types
 *************************************************************/
class ItemTypeComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit ItemTypeComboBox(QWidget* parent = nullptr);
    
    void setCurrentItemType(int type);
    int currentItemType() const;
};

/*************************************************************
 * ItemFamilyComboBox - Combo box for selecting item families
 *************************************************************/
class ItemFamilyComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit ItemFamilyComboBox(QWidget* parent = nullptr);
    
    void setCurrentFamily(int family);
    int currentFamily() const;
};

/*************************************************************
 * SalesTypeComboBox - Combo box for selecting sales types
 *************************************************************/
class SalesTypeComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit SalesTypeComboBox(QWidget* parent = nullptr);
    
    void setCurrentSalesType(int type);
    int currentSalesType() const;
};

/*************************************************************
 * PrinterComboBox - Combo box for selecting printers
 *************************************************************/
class PrinterComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit PrinterComboBox(QWidget* parent = nullptr);
    
    void setCurrentPrinter(int id);
    int currentPrinter() const;
};

/*************************************************************
 * CallOrderComboBox - Combo box for selecting call order
 *************************************************************/
class CallOrderComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit CallOrderComboBox(QWidget* parent = nullptr);
    
    void setCurrentCallOrder(int order);
    int currentCallOrder() const;
};

} // namespace vt
