/*
 * ViewTouch V2 - Edit Toolbar
 * Toolbar for zone/page editing tools
 */

#pragma once

#include "editor/edit_mode.hpp"

#include <QWidget>
#include <QToolBar>
#include <QAction>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>

namespace vt {

class EditMode;
class Page;
class Zone;

/*************************************************************
 * EditToolbar - Main editing toolbar
 *************************************************************/
class EditToolbar : public QToolBar {
    Q_OBJECT

public:
    explicit EditToolbar(EditMode* editMode, QWidget* parent = nullptr);
    ~EditToolbar();

    void setCurrentPage(Page* page) { currentPage_ = page; }

signals:
    void newZoneRequested();
    void newPageRequested();
    void zonePropertiesRequested(Zone* zone);
    void pagePropertiesRequested(Page* page);
    void propertiesRequested();  // Generic - opens properties for current selection
    void saveRequested();
    void loadRequested();

private slots:
    void onEditModeChanged(bool active);
    void onToolChanged(EditTool tool);
    void onSelectionChanged();
    
    void onSelectTool();
    void onCreateTool();
    void onDeleteTool();
    void onPropertiesTool();
    void onCopyAction();
    void onPasteAction();
    void onGridSnapChanged(bool checked);
    void onGridSizeChanged(int size);
    
    void onNewZone();
    void onNewPage();
    void onSave();
    void onLoad();

private:
    void setupActions();
    void updateToolButtons();

    EditMode* editMode_;
    Page* currentPage_ = nullptr;

    // Tool actions
    QAction* selectAction_;
    QAction* createAction_;
    QAction* deleteAction_;
    QAction* propertiesAction_;
    QAction* copyAction_;
    QAction* pasteAction_;
    
    // Grid actions
    QAction* gridSnapAction_;
    QSpinBox* gridSizeSpinBox_;
    
    // File actions
    QAction* newZoneAction_;
    QAction* newPageAction_;
    QAction* saveAction_;
    QAction* loadAction_;
    
    // Zone type selector for creation
    QComboBox* zoneTypeCombo_;
};

/*************************************************************
 * ZoneTypeSelector - Select zone type for creation
 *************************************************************/
class ZoneTypeSelector : public QComboBox {
    Q_OBJECT
    
public:
    enum ZoneType {
        Button,
        Label,
        List,
        Table,
        Input,
        Image,
    };
    
    explicit ZoneTypeSelector(QWidget* parent = nullptr);
    
    ZoneType selectedType() const;
};

} // namespace vt
