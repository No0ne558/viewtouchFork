#pragma once

#include "app/navigator.hh"
#include "layout/layout.hh"
#include "zonemodel.hh"

#include <QObject>
#include <QSize>
#include <QTime>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Runtime (non-edit) driver for the page UI: owns the layout and navigator,
// publishes the current page to QML, and runs zone actions when touched.
class LayoutController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(QString pageId READ pageId NOTIFY pageChanged)
    Q_PROPERTY(QString pageName READ pageName NOTIFY pageChanged)
    Q_PROPERTY(QString pageKind READ pageKind NOTIFY pageChanged)
    Q_PROPERTY(QSize canvasSize READ canvasSize NOTIFY pageChanged)
    Q_PROPERTY(QVariantMap background READ background NOTIFY pageChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY pageChanged)
    Q_PROPERTY(ZoneModel *zones READ zones CONSTANT)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)

public:
    explicit LayoutController(vt::layout::Layout layout, QObject *parent = nullptr);

    QString pageId() const { return nav_.current(); }
    QString pageName() const;
    QString pageKind() const;
    QSize canvasSize() const;
    QVariantMap background() const;
    bool canGoBack() const { return nav_.canGoBack(); }
    ZoneModel *zones() { return &zones_; }
    QString statusText() const { return status_; }

    const vt::layout::Layout &layout() const { return layout_; }

    // Meal period for index jumps. Defaults from the clock; store hours
    // become a setting in M4.
    void setMealPeriod(const QString &period) { nav_.setMealPeriod(period); }
    static QString mealPeriodAt(QTime time);

    Q_INVOKABLE void activate(const QString &zoneId);
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goHome();
    Q_INVOKABLE bool jumpTo(const QString &pageId);
    Q_INVOKABLE bool triggerHotkey(const QString &key);

signals:
    void pageChanged();
    void statusChanged();
    void itemAdded(const QString &item);
    void commandRequested(const QString &name, const QVariantMap &args);

private:
    bool runAction(const vt::layout::Action &action);
    void navigate(vt::app::Navigator::Mode mode, const QString &target = {});
    void refresh();
    void setStatus(const QString &text);
    const vt::layout::Page *currentPage() const { return layout_.page(nav_.current()); }

    vt::layout::Layout layout_;
    vt::app::Navigator nav_;
    ZoneModel zones_;
    QString status_;
};
