/*
 * ViewTouch V2 - Edit Toolbar Implementation
 */

#include "editor/edit_toolbar.hpp"
#include "zone/page.hpp"
#include "zone/zone.hpp"

#include <QActionGroup>
#include <QHBoxLayout>

namespace vt {

EditToolbar::EditToolbar(EditMode* editMode, QWidget* parent)
    : QToolBar(tr("Edit Tools"), parent)
    , editMode_(editMode)
{
    setObjectName("EditToolbar");
    setMovable(false);
    
    setupActions();
    
    // Connect to edit mode signals
    connect(editMode_, &EditMode::editModeChanged, this, &EditToolbar::onEditModeChanged);
    connect(editMode_, &EditMode::toolChanged, this, &EditToolbar::onToolChanged);
    connect(editMode_, &EditMode::selectionChanged, this, &EditToolbar::onSelectionChanged);
    
    // Initial state
    onEditModeChanged(editMode_->isActive());
}

EditToolbar::~EditToolbar() = default;

void EditToolbar::setupActions() {
    // Tool selection group (mutually exclusive)
    auto* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);
    
    // Select tool
    selectAction_ = new QAction(tr("Select"), this);
    selectAction_->setCheckable(true);
    selectAction_->setChecked(true);
    selectAction_->setToolTip(tr("Select and move zones (S)"));
    selectAction_->setShortcut(QKeySequence("S"));
    toolGroup->addAction(selectAction_);
    connect(selectAction_, &QAction::triggered, this, &EditToolbar::onSelectTool);
    addAction(selectAction_);
    
    // Create tool
    createAction_ = new QAction(tr("Create"), this);
    createAction_->setCheckable(true);
    createAction_->setToolTip(tr("Create new zones (C)"));
    createAction_->setShortcut(QKeySequence("C"));
    toolGroup->addAction(createAction_);
    connect(createAction_, &QAction::triggered, this, &EditToolbar::onCreateTool);
    addAction(createAction_);
    
    // Zone type selector
    zoneTypeCombo_ = new ZoneTypeSelector(this);
    addWidget(zoneTypeCombo_);
    
    addSeparator();
    
    // Delete tool
    deleteAction_ = new QAction(tr("Delete"), this);
    deleteAction_->setToolTip(tr("Delete selected zones (Delete)"));
    deleteAction_->setShortcut(QKeySequence::Delete);
    connect(deleteAction_, &QAction::triggered, this, &EditToolbar::onDeleteTool);
    addAction(deleteAction_);
    
    // Properties
    propertiesAction_ = new QAction(tr("Properties"), this);
    propertiesAction_->setToolTip(tr("Edit zone properties (P)"));
    propertiesAction_->setShortcut(QKeySequence("P"));
    connect(propertiesAction_, &QAction::triggered, this, &EditToolbar::onPropertiesTool);
    addAction(propertiesAction_);
    
    addSeparator();
    
    // Copy/Paste
    copyAction_ = new QAction(tr("Copy"), this);
    copyAction_->setToolTip(tr("Copy zone (Ctrl+C)"));
    copyAction_->setShortcut(QKeySequence::Copy);
    connect(copyAction_, &QAction::triggered, this, &EditToolbar::onCopyAction);
    addAction(copyAction_);
    
    pasteAction_ = new QAction(tr("Paste"), this);
    pasteAction_->setToolTip(tr("Paste zone (Ctrl+V)"));
    pasteAction_->setShortcut(QKeySequence::Paste);
    connect(pasteAction_, &QAction::triggered, this, &EditToolbar::onPasteAction);
    addAction(pasteAction_);
    
    addSeparator();
    
    // Grid snap
    gridSnapAction_ = new QAction(tr("Snap to Grid"), this);
    gridSnapAction_->setCheckable(true);
    gridSnapAction_->setChecked(editMode_->gridSnap());
    gridSnapAction_->setToolTip(tr("Snap to grid (G)"));
    gridSnapAction_->setShortcut(QKeySequence("G"));
    connect(gridSnapAction_, &QAction::toggled, this, &EditToolbar::onGridSnapChanged);
    addAction(gridSnapAction_);
    
    // Grid size
    auto* gridLabel = new QLabel(tr("Grid:"), this);
    addWidget(gridLabel);
    
    gridSizeSpinBox_ = new QSpinBox(this);
    gridSizeSpinBox_->setRange(5, 50);
    gridSizeSpinBox_->setValue(editMode_->gridSize());
    gridSizeSpinBox_->setSuffix(tr("px"));
    gridSizeSpinBox_->setToolTip(tr("Grid size"));
    connect(gridSizeSpinBox_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &EditToolbar::onGridSizeChanged);
    addWidget(gridSizeSpinBox_);
    
    addSeparator();
    
    // New zone/page
    newZoneAction_ = new QAction(tr("New Zone"), this);
    newZoneAction_->setToolTip(tr("Create a new zone"));
    connect(newZoneAction_, &QAction::triggered, this, &EditToolbar::onNewZone);
    addAction(newZoneAction_);
    
    newPageAction_ = new QAction(tr("New Page"), this);
    newPageAction_->setToolTip(tr("Create a new page"));
    connect(newPageAction_, &QAction::triggered, this, &EditToolbar::onNewPage);
    addAction(newPageAction_);
    
    addSeparator();
    
    // Save/Load
    saveAction_ = new QAction(tr("Save"), this);
    saveAction_->setToolTip(tr("Save pages (Ctrl+S)"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &EditToolbar::onSave);
    addAction(saveAction_);
    
    loadAction_ = new QAction(tr("Load"), this);
    loadAction_->setToolTip(tr("Load pages (Ctrl+O)"));
    loadAction_->setShortcut(QKeySequence::Open);
    connect(loadAction_, &QAction::triggered, this, &EditToolbar::onLoad);
    addAction(loadAction_);
}

void EditToolbar::onEditModeChanged(bool active) {
    setEnabled(active);
    updateToolButtons();
}

void EditToolbar::onToolChanged(EditTool tool) {
    selectAction_->setChecked(tool == EditTool::Select);
    createAction_->setChecked(tool == EditTool::Create);
}

void EditToolbar::onSelectionChanged() {
    updateToolButtons();
}

void EditToolbar::updateToolButtons() {
    bool hasSelection = editMode_->selectedZone() != nullptr;
    deleteAction_->setEnabled(hasSelection);
    propertiesAction_->setEnabled(hasSelection);
    copyAction_->setEnabled(hasSelection);
    pasteAction_->setEnabled(editMode_->hasClipboard());
}

void EditToolbar::onSelectTool() {
    editMode_->setCurrentTool(EditTool::Select);
}

void EditToolbar::onCreateTool() {
    editMode_->setCurrentTool(EditTool::Create);
}

void EditToolbar::onDeleteTool() {
    if (currentPage_) {
        editMode_->deleteSelectedZones(currentPage_);
    }
}

void EditToolbar::onPropertiesTool() {
    if (editMode_->selectedZone()) {
        emit zonePropertiesRequested(editMode_->selectedZone());
    }
    emit propertiesRequested();
}

void EditToolbar::onCopyAction() {
    editMode_->copyZone();
    updateToolButtons();
}

void EditToolbar::onPasteAction() {
    if (currentPage_) {
        // Paste at center of view (TODO: get actual center)
        editMode_->pasteZone(currentPage_, 100, 100);
    }
}

void EditToolbar::onGridSnapChanged(bool checked) {
    editMode_->setGridSnap(checked);
}

void EditToolbar::onGridSizeChanged(int size) {
    editMode_->setGridSize(size);
}

void EditToolbar::onNewZone() {
    emit newZoneRequested();
}

void EditToolbar::onNewPage() {
    emit newPageRequested();
}

void EditToolbar::onSave() {
    emit saveRequested();
}

void EditToolbar::onLoad() {
    emit loadRequested();
}

/*************************************************************
 * ZoneTypeSelector Implementation
 *************************************************************/
ZoneTypeSelector::ZoneTypeSelector(QWidget* parent)
    : QComboBox(parent)
{
    addItem(tr("Button"), Button);
    addItem(tr("Label"), Label);
    addItem(tr("List"), List);
    addItem(tr("Table"), Table);
    addItem(tr("Input"), Input);
    addItem(tr("Image"), Image);
    
    setCurrentIndex(0);
    setToolTip(tr("Zone type to create"));
}

ZoneTypeSelector::ZoneType ZoneTypeSelector::selectedType() const {
    return static_cast<ZoneType>(currentData().toInt());
}

} // namespace vt
