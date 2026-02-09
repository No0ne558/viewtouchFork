/*
 * ViewTouch V2 - Page Properties Dialog
 * Edit page properties — matches original ViewTouch EditPage/ReadPage flow
 */

#pragma once

#include "core/types.hpp"
#include "core/fonts.hpp"

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QTabWidget>
#include <memory>

namespace vt {

class Page;
class TextureComboBox;
class ColorComboBox;
class FrameComboBox;
class FontComboBox;

/*************************************************************
 * SizePresetComboBox - Page resolution presets from v1
 *************************************************************/
class SizePresetComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit SizePresetComboBox(QWidget* parent = nullptr);
    void setFromSize(int w, int h);
    int selectedWidth() const;
    int selectedHeight() const;
};

/*************************************************************
 * IndexComboBox - Meal period / page index from v1
 *************************************************************/
class IndexComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit IndexComboBox(QWidget* parent = nullptr);
    void setCurrentIndex2(int index);
    int currentIndex2() const;
};

/*************************************************************
 * ShadowComboBox - Named shadow presets from v1
 *************************************************************/
class ShadowComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit ShadowComboBox(bool includeDefault = true, QWidget* parent = nullptr);
    void setCurrentShadow(int value);
    int currentShadow() const;
};

/*************************************************************
 * PagePropertiesDialog - Edit page properties
 * 
 * Used for both editing existing pages and creating new pages.
 * Matches original ViewTouch EditPage/ReadPage flow.
 *************************************************************/
class PagePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    // Edit an existing page
    explicit PagePropertiesDialog(Page* page, QWidget* parent = nullptr);
    
    // Create a new page (page is nullptr, defaults provided)
    PagePropertiesDialog(int suggestedId, PageType defaultType, QWidget* parent = nullptr);
    
    ~PagePropertiesDialog();

    void applyChanges();
    
    // For new page creation: returns the configured page (caller takes ownership)
    std::unique_ptr<Page> takeNewPage();
    
    // Whether this dialog is creating a new page vs editing existing
    bool isNewPage() const { return isNewPage_; }
    
    // Whether the user requested page deletion
    bool deleteRequested() const { return deleteRequested_; }

private slots:
    void onApply();
    void onOk();
    void onDelete();
    void onPageTypeChanged(int index);
    void onSizePresetChanged(int index);

private:
    void setupUi();
    void loadFromPage();
    void saveToPage();
    void initDefaults(int suggestedId, PageType defaultType);
    void autoAssignParent(PageType type);

    Page* page_ = nullptr;                  // Existing page being edited (not owned)
    std::unique_ptr<Page> newPage_;         // New page being created (owned)
    bool isNewPage_ = false;
    bool deleteRequested_ = false;
    
    // --- General ---
    QSpinBox* idSpinBox_;
    QLineEdit* nameEdit_;
    QComboBox* typeCombo_;
    SizePresetComboBox* sizePresetCombo_;
    QSpinBox* widthSpinBox_;
    QSpinBox* heightSpinBox_;
    QSpinBox* parentIdSpinBox_;
    IndexComboBox* indexCombo_;
    
    // --- Appearance ---
    TextureComboBox* bgTextureCombo_;       // Page background texture (image)
    ColorComboBox* titleColorCombo_;
    
    // --- Zone Defaults (per-state: Normal/Selected/Alternate) ---
    struct StateDefaults {
        FrameComboBox* frameCombo = nullptr;
        TextureComboBox* textureCombo = nullptr;
        ColorComboBox* colorCombo = nullptr;
    };
    StateDefaults stateDefaults_[3];        // 0=Normal, 1=Selected, 2=Alternate
    
    FontComboBox* defaultFontCombo_;
    QSpinBox* defaultSpacingSpinBox_;
    ShadowComboBox* defaultShadowCombo_;
    
    QTabWidget* mainTabWidget_;
};

/*************************************************************
 * PageTypeComboBox - Combo box for selecting page types
 *************************************************************/
class PageTypeComboBox : public QComboBox {
    Q_OBJECT
    
public:
    explicit PageTypeComboBox(QWidget* parent = nullptr);
    
    void setCurrentPageType(PageType type);
    PageType currentPageType() const;
};

} // namespace vt
