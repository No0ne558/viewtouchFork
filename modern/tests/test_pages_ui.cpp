#include <catch2/catch_test_macros.hpp>

#include "layoutcontroller.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace vt;

// Every shipped page must render without a single QML warning, running and
// in the editor. Catches broken bindings in widgets nobody tapped yet.

namespace {

QStringList g_warnings;

void collect(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (type != QtDebugMsg && type != QtInfoMsg)
        g_warnings << msg;
}

} // namespace

TEST_CASE("Every seed page renders cleanly, running and editing", "[ui][pages]")
{
    auto layout = layout::Layout::loadDirectory(QStringLiteral(VTM_SEED_DIR));
    REQUIRE(layout);
    QStringList pageIds;
    for (const auto &p : layout->pages)
        pageIds << p.id;

    test::RecordingSink sink;
    app::PosService pos(test::seedPosData(), &sink);
    LayoutController c(std::move(*layout));
    c.setPos(&pos);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.addItem(u"classic-burger"_s);   // give order widgets something to show

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{u"controller"_s, QVariant::fromValue(&c)},
                                 {u"width"_s, 1600}, {u"height"_s, 900}});
    g_warnings.clear();
    const QtMessageHandler previous = qInstallMessageHandler(collect);
    engine.loadFromModule("ViewTouch", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    REQUIRE(window);
    REQUIRE(QTest::qWaitForWindowExposed(window));

    for (const QString &id : pageIds) {
        INFO(id.toStdString());
        CHECK(c.showPage(id));
        QTest::qWait(20);
    }
    c.enterEditMode();
    for (const QString &id : pageIds) {
        INFO("edit " << id.toStdString());
        CHECK(c.showPage(id));
        QTest::qWait(20);
    }
    c.leaveEditMode(false);
    QTest::qWait(20);
    qInstallMessageHandler(previous);
    CHECK(g_warnings == QStringList{});
}

namespace {

int countVisible(QQuickItem *root, const char *classPrefix)
{
    int n = 0;
    for (QQuickItem *item : root->childItems()) {
        if (!item->isVisible())
            continue;
        if (QByteArray(item->metaObject()->className()).startsWith(classPrefix))
            ++n;
        n += countVisible(item, classPrefix);
    }
    return n;
}

} // namespace

TEST_CASE("Admin screen: touching a record opens its form", "[ui][pages]")
{
    auto layout = layout::Layout::loadDirectory(QStringLiteral(VTM_SEED_DIR));
    REQUIRE(layout);
    test::RecordingSink sink;
    app::PosService pos(test::seedPosData(), &sink);
    LayoutController c(std::move(*layout));
    c.setPos(&pos);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(c.showPage(u"admin-menu"_s));

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{u"controller"_s, QVariant::fromValue(&c)},
                                 {u"width"_s, 1600}, {u"height"_s, 900}});
    engine.loadFromModule("ViewTouch", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    REQUIRE(window);
    REQUIRE(QTest::qWaitForWindowExposed(window));
    QTest::qWait(50);
    CHECK(countVisible(window->contentItem(), "FieldEditor") == 0);

    // First row of the list: widget at (16,112), drawn at 1.6x inside.
    auto *surface = window->findChild<QQuickItem *>(u"pageSurface"_s);
    REQUIRE(surface);
    QTest::mouseClick(window, Qt::LeftButton, {}, surface->mapToScene(QPointF(16 + 60 * 1.6, 112 + 30 * 1.6)).toPoint());
    QTest::qWait(50);
    CHECK(countVisible(window->contentItem(), "FieldEditor") == 8);   // menu item fields

    if (const QByteArray dir = qgetenv("VTM_SHOTS"); !dir.isEmpty())
        window->grabWindow().save(QString::fromLocal8Bit(dir) + u"/5-admin-menu.png"_s);
}
