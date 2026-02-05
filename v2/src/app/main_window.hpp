/*
 * ViewTouch V2 - Main Window
 * The main display window for a terminal
 */

#pragma once

#include <QMainWindow>
#include <QWidget>
#include <memory>

namespace vt {

class Control;
class Terminal;
class Renderer;
class TerminalWidget;
class EditMode;
class EditToolbar;
class Zone;
class Page;

/*************************************************************
 * MainWindow - Main application window
 *************************************************************/
class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    explicit MainWindow(Control* control, QWidget* parent = nullptr);
    ~MainWindow();
    
    // Get the terminal
    Terminal* terminal() const { return terminal_; }
    
    // Edit mode
    EditMode* editMode() const { return editMode_.get(); }
    bool isEditMode() const;
    
    // Toggle fullscreen
    void toggleFullscreen();
    bool isFullscreen() const { return isFullScreen(); }
    
public slots:
    void onZonePropertiesRequested(Zone* zone);
    void onPagePropertiesRequested(Page* page);
    void onNewZoneRequested();
    void onNewPageRequested();
    void onSaveRequested();
    void onLoadRequested();
    
protected:
    void keyPressEvent(QKeyEvent* event) override;
    
private slots:
    void onRedrawRequested();
    void onEditModeChanged(bool active);
    
private:
    void setupUi();
    void setupMenuBar();
    void createDemoPages();
    
    Control* control_;
    Terminal* terminal_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<EditMode> editMode_;
    TerminalWidget* terminalWidget_ = nullptr;
    EditToolbar* editToolbar_ = nullptr;
};

/*************************************************************
 * TerminalWidget - Touch/render surface
 *************************************************************/
class TerminalWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit TerminalWidget(Terminal* term, Renderer* renderer, 
                           QWidget* parent = nullptr);
    ~TerminalWidget();
    
    // Force redraw
    void requestRedraw();
    
    void setEditMode(EditMode* em) { editMode_ = em; }
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    
private:
    void drawEditOverlay(QPainter& painter);
    void drawSelectionHandles(QPainter& painter, Zone* zone);
    void drawGrid(QPainter& painter);
    
    QPoint screenToDesign(const QPoint& pt) const;
    QPoint designToScreen(const QPoint& pt) const;
    
    Terminal* terminal_;
    Renderer* renderer_;
    EditMode* editMode_ = nullptr;
};

} // namespace vt
