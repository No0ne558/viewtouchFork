/*
 * ViewTouch V2 - Page Properties Dialog
 * Edit page properties
 */

#pragma once

#include "core/types.hpp"

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>

namespace vt {

class Page;

/*************************************************************
 * PagePropertiesDialog - Edit page properties
 *************************************************************/
class PagePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PagePropertiesDialog(Page* page, QWidget* parent = nullptr);
    ~PagePropertiesDialog();

    void applyChanges();

private slots:
    void onApply();
    void onOk();

private:
    void setupUi();
    void loadFromPage();
    void saveToPage();

    Page* page_;
    
    QSpinBox* idSpinBox_;
    QLineEdit* nameEdit_;
    QComboBox* typeCombo_;
    QSpinBox* widthSpinBox_;
    QSpinBox* heightSpinBox_;
    QSpinBox* parentIdSpinBox_;
    QSpinBox* indexSpinBox_;
    QComboBox* defaultTextureCombo_;
    QComboBox* defaultColorCombo_;
    QComboBox* titleColorCombo_;
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
