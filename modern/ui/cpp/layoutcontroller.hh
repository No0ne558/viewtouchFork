#pragma once

#include "app/navigator.hh"
#include "editorcontroller.hh"
#include "layout/layout.hh"
#include "zonemodel.hh"

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTime>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace vt::storage { class LayoutStore; }

// Drives the page UI: owns the saved layout and the navigator, publishes the
// current page to QML, and runs zone actions when touched. In edit mode it
// shows the editor's draft instead and ignores touches (the editor overlay
// handles them); the saved layout only changes on saveEdits().
class LayoutController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(QString pageId READ pageId NOTIFY pageChanged)
    Q_PROPERTY(QString pageName READ pageName NOTIFY pageChanged)
    Q_PROPERTY(QString pageKind READ pageKind NOTIFY pageChanged)
    Q_PROPERTY(QSize canvasSize READ canvasSize NOTIFY pageChanged)
    Q_PROPERTY(int pageGrid READ pageGrid NOTIFY pageChanged)
    Q_PROPERTY(QVariantMap background READ background NOTIFY pageChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY pageChanged)
    Q_PROPERTY(ZoneModel *zones READ zones CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(EditorController *editor READ editor NOTIFY editingChanged)

public:
    explicit LayoutController(vt::layout::Layout layout, QObject *parent = nullptr);
    ~LayoutController() override;

    // Where saveEdits() persists. Not owned; may be null (tests, --layout).
    void setStore(vt::storage::LayoutStore *store) { store_ = store; }

    QString pageId() const { return nav_.current(); }
    QString pageName() const;
    QString pageKind() const;
    QSize canvasSize() const;
    int pageGrid() const;
    QVariantMap background() const;
    bool canGoBack() const { return nav_.canGoBack(); }
    ZoneModel *zones() { return &zones_; }
    QString statusText() const { return status_; }
    bool editing() const { return editor_ != nullptr; }
    EditorController *editor() const { return editor_; }

    // The saved layout (what runs), and what is on screen (draft while editing).
    const vt::layout::Layout &layout() const { return layout_; }
    const vt::layout::Layout &activeLayout() const;

    // Meal period for index jumps. Defaults from the clock; store hours
    // become a setting in M4.
    void setMealPeriod(const QString &period) { nav_.setMealPeriod(period); }
    static QString mealPeriodAt(QTime time);

    Q_INVOKABLE void activate(const QString &zoneId);
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goHome();
    Q_INVOKABLE bool jumpTo(const QString &pageId);
    // Switch pages without remembering the current one (editor page list).
    Q_INVOKABLE bool showPage(const QString &pageId);
    Q_INVOKABLE bool triggerHotkey(const QString &key);

    Q_INVOKABLE void enterEditMode();
    // Persist the draft and make it the running layout; stays in edit mode.
    Q_INVOKABLE bool saveEdits();
    // Leave edit mode, saving first when `save` (and staying if saving fails).
    Q_INVOKABLE bool leaveEditMode(bool save);

signals:
    void pageChanged();
    void statusChanged();
    void editingChanged();
    void itemAdded(const QString &item);
    void commandRequested(const QString &name, const QVariantMap &args);

private:
    bool runAction(const vt::layout::Action &action);
    void navigate(vt::app::Navigator::Mode mode, const QString &target = {});
    void onDraftChanged();
    void ensureCurrentPageExists();
    void refresh();
    void setStatus(const QString &text);
    const vt::layout::Page *currentPage() const { return activeLayout().page(nav_.current()); }

    vt::layout::Layout layout_;
    vt::app::Navigator nav_;
    ZoneModel zones_;
    QString status_;
    QPointer<EditorController> editor_;
    vt::storage::LayoutStore *store_ = nullptr;
};
