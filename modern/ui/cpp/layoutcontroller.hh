#pragma once

#include "app/navigator.hh"
#include "app/pos_session.hh"
#include "editorcontroller.hh"
#include "layout/layout.hh"
#include "zonemodel.hh"

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTime>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace vt::storage { class LayoutStore; }

// Drives the page UI: owns the running layout and the navigator, publishes
// the current page to QML, and runs zone actions when touched. In edit mode
// it shows the editor's draft instead and ignores touches (the editor overlay
// handles them); the running layout only changes on saveEdits().
//
// Zone actions run in order through the POS session. A remote session
// answers later, so each action continues in its reply; a failed step, a
// page change, or entering edit mode ends the chain.
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
    Q_PROPERTY(EditorController *editor READ editor NOTIFY editorChanged)
    Q_PROPERTY(vt::app::PosSession *pos READ pos NOTIFY posChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // "phone": phone versions of pages (and phone-sized layouts) are shown;
    // "standard": pages as designed.
    Q_PROPERTY(QString formFactor READ formFactor NOTIFY formFactorChanged)

public:
    // Persists a layout; false (with a message) when it could not.
    using Saver = std::function<bool(const vt::layout::Layout &, QString *error)>;

    explicit LayoutController(vt::layout::Layout layout, QObject *parent = nullptr);
    ~LayoutController() override;

    // Where saveEdits() goes. setStore() saves to a local database; a remote
    // terminal sends the layout to its server instead. Neither: edits apply
    // in memory only (tests, --layout).
    void setStore(vt::storage::LayoutStore *store);
    void setSaver(Saver saver) { saver_ = std::move(saver); }

    // Pages were changed elsewhere (another terminal saved): run them now.
    // While editing, the draft is kept; the new pages show when leaving
    // without saving.
    void replaceLayout(vt::layout::Layout layout);

    // The POS session that zone actions and widgets operate on. Not owned.
    // Without one, POS actions only show a status message (layout-only use).
    void setPos(vt::app::PosSession *pos);
    vt::app::PosSession *pos() const { return pos_; }

    QString pageId() const { return nav_.current(); }
    QString pageName() const;
    QString pageKind() const;
    QSize canvasSize() const;
    int pageGrid() const;
    QVariantMap background() const;
    bool canGoBack() const { return nav_.canGoBack(); }
    ZoneModel *zones() { return &zones_; }
    QString statusText() const { return status_; }
    bool editing() const { return editing_; }
    EditorController *editor() const { return editor_; }
    bool busy() const { return pending_ > 0; }

    // The running layout, and what is on screen (draft while editing).
    const vt::layout::Layout &layout() const { return layout_; }
    const vt::layout::Layout &activeLayout() const;

    // Meal period for index jumps. Follows the clock and the store's meal
    // periods (checked every minute) unless set here.
    void setMealPeriod(const QString &period);
    // With the built-in periods, or with a session's mealPeriods list.
    static QString mealPeriodAt(QTime time);
    static QString mealPeriodAt(const QVariantList &periods, QTime time);

    QString formFactor() const { return formFactor_; }
    // Force "phone" or "standard" (command line); empty: the terminal's
    // setting, else automatic.
    void setFormFactorOverride(const QString &formFactor);
    // Automatic: a window whose shorter side is under 600 (logical pixels,
    // Android's phone/tablet line) is a phone. On for Android.
    void setAutoFormFactor(bool on);
    Q_INVOKABLE void windowResized(qreal width, qreal height);
    // Every table in the pages, for the phone table list: [{name, seats}].
    Q_INVOKABLE QVariantList tables() const;

    // Save the report on screen as "csv" or "pdf" in the export folder.
    Q_INVOKABLE void exportReport(const QVariantMap &report, const QString &format);
    void setExportDirectory(const QString &dir) { exportDir_ = dir; }

    // Change the choices of an unsent item: opens the Choose page for it.
    Q_INVOKABLE void chooseLine(qint64 lineId);
    // Modifier choices done: back to the menu once the required ones are made.
    Q_INVOKABLE void finishChoosing();

    Q_INVOKABLE void activate(const QString &zoneId);
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goHome();
    Q_INVOKABLE bool jumpTo(const QString &pageId);
    // Switch pages without remembering the current one (editor page list).
    Q_INVOKABLE bool showPage(const QString &pageId);
    Q_INVOKABLE bool triggerHotkey(const QString &key);

    // Widget entry points.
    Q_INVOKABLE void selectTable(const QString &label);
    Q_INVOKABLE void openCheck(qint64 checkId);
    Q_INVOKABLE void login();

    // F1 / "Edit Pages" / --edit: the only way in from the UI. Needs the
    // layout.edit permission when a POS session is attached.
    Q_INVOKABLE bool requestEditMode();
    // Unchecked, for C++ only (tests). Deliberately not Q_INVOKABLE, so no
    // page or widget can skip the permission check.
    void enterEditMode();
    // Persist the draft and make it the running layout; stays in edit mode.
    Q_INVOKABLE bool saveEdits();
    // Leave edit mode, saving first when `save` (and staying if saving fails).
    Q_INVOKABLE bool leaveEditMode(bool save);

signals:
    void pageChanged();
    void formFactorChanged();
    // A manager asked to close ViewTouch (the closeApp command).
    void closeRequested();
    void statusChanged();
    void editingChanged();
    void editorChanged();
    void posChanged();
    void busyChanged();
    void itemAdded(const QString &item);
    void commandRequested(const QString &name, const QVariantMap &args);

private:
    using Done = std::function<void(bool ok)>;

    void runChain(QList<vt::layout::Action> actions, int index, QString startPage);
    void runAction(const vt::layout::Action &action, Done done);
    void runCommand(const QString &name, const QVariantMap &args, Done done);
    // invoke() on the session, counted as pending until answered.
    void call(const QString &method, const QVariantList &args, std::function<void(const QVariant &)> then);
    bool navigate(vt::app::Navigator::Mode mode, const QString &target = {});
    bool mayOpen(const QString &pageId);
    QString rolePage(const QString &role) const;
    void onLoggedInChanged(bool loggedIn);
    void onDraftChanged();
    void ensureCurrentPageExists();
    void refresh();
    void setStatus(const QString &text);
    const vt::layout::Page *currentPage() const { return activeLayout().page(nav_.current()); }
    // What this screen shows for the current page: its phone version, or
    // the phone template with the page's own zones laid out in its content
    // area (moved), or the page itself.
    struct Shown {
        QString pageId;
        QSize canvas;
        QList<vt::layout::Layout::PlacedZone> zones;
        QHash<const vt::layout::Zone *, QRect> moved;   // reflowed: rect on this screen
    };
    Shown shown() const;
    void updateFormFactor();
    QString formFactor_ = QStringLiteral("standard");
    QString formFactorOverride_;
    bool autoFormFactor_ = false;
    bool phoneSizedWindow_ = false;
    QString exportDir_;

    vt::layout::Layout layout_;
    vt::app::Navigator nav_;
    ZoneModel zones_;
    QString status_;
    // While leaving edit mode the editor object outlives `editing_` briefly,
    // so QML panels unload before the editor they bind to goes away.
    QPointer<EditorController> editor_;
    bool editing_ = false;
    QTimer mealTimer_;
    bool mealPeriodFixed_ = false;
    void updateMealPeriod();
    Saver saver_;
    vt::app::PosSession *pos_ = nullptr;
    int pending_ = 0;
};
