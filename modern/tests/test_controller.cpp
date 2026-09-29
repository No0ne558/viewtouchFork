#include <catch2/catch_test_macros.hpp>

#include "layoutcontroller.hh"
#include "qt_catch.hh"

#include <QSignalSpy>

using namespace Qt::StringLiterals;

namespace {

LayoutController seedController()
{
    auto layout = vt::layout::Layout::loadDirectory(QStringLiteral(VTM_SEED_DIR));
    REQUIRE(layout);
    return LayoutController(std::move(*layout));
}

// Row of the zone in the controller's model, or -1.
int rowOf(LayoutController &c, const QString &zoneId)
{
    ZoneModel *m = c.zones();
    for (int r = 0; r < m->rowCount(); ++r) {
        if (m->data(m->index(r), ZoneModel::ZoneIdRole).toString() == zoneId)
            return r;
    }
    return -1;
}

QVariant role(LayoutController &c, const QString &zoneId, int role)
{
    const int r = rowOf(c, zoneId);
    REQUIRE(r >= 0);
    return c.zones()->data(c.zones()->index(r), role);
}

} // namespace

TEST_CASE("Meal period from time of day", "[controller]")
{
    CHECK(LayoutController::mealPeriodAt(QTime(7, 30)) == u"breakfast"_s);
    CHECK(LayoutController::mealPeriodAt(QTime(12, 0)) == u"lunch"_s);
    CHECK(LayoutController::mealPeriodAt(QTime(19, 0)) == u"dinner"_s);
}

TEST_CASE("Seed flow: login to order with modifiers to settle", "[controller]")
{
    LayoutController c = seedController();
    c.setMealPeriod(u"lunch"_s);
    QSignalSpy items(&c, &LayoutController::itemAdded);
    QSignalSpy pages(&c, &LayoutController::pageChanged);

    CHECK(c.pageId() == u"login"_s);
    CHECK_FALSE(c.canGoBack());

    c.activate(u"start"_s);
    CHECK(c.pageId() == u"tables"_s);
    c.activate(u"t1"_s);
    CHECK(c.pageId() == u"guest-count"_s);
    c.activate(u"start"_s);                     // index jump -> current meal period
    CHECK(c.pageId() == u"index-lunch"_s);

    // Inherited template zones are present and flagged; the lunch tab is lit.
    CHECK(role(c, u"order-list"_s, ZoneModel::InheritedRole).toBool());
    CHECK_FALSE(role(c, u"title"_s, ZoneModel::InheritedRole).toBool());
    CHECK(role(c, u"tab-lunch"_s, ZoneModel::CurrentRole).toBool());
    CHECK_FALSE(role(c, u"tab-dinner"_s, ZoneModel::CurrentRole).toBool());

    c.activate(u"cat-items-burgers"_s);
    CHECK(c.pageId() == u"items-burgers"_s);
    CHECK(role(c, u"tab-lunch"_s, ZoneModel::CurrentRole).toBool());   // via last index

    // Burger runs Temperature -> Side -> back to the burger page.
    c.activate(u"item-1"_s);
    CHECK(c.pageId() == u"mod-temperature"_s);
    c.activate(u"opt-3"_s);                     // "Medium", then continue sequence
    CHECK(c.pageId() == u"mod-side"_s);
    c.activate(u"skip"_s);
    CHECK(c.pageId() == u"items-burgers"_s);

    REQUIRE(items.size() == 2);
    CHECK(items[0][0].toString() == u"Classic Burger"_s);
    CHECK(items[1][0].toString() == u"Medium"_s);

    // Hotkey on an inherited zone: "p" = Pay.
    CHECK(c.triggerHotkey(u"P"_s));
    CHECK(c.pageId() == u"settle"_s);
    c.activate(u"done"_s);
    CHECK(c.pageId() == u"items-burgers"_s);

    c.activate(u"tab-categories"_s);
    CHECK(c.pageId() == u"index-lunch"_s);

    c.goHome();
    CHECK(c.pageId() == u"login"_s);
    CHECK_FALSE(c.canGoBack());
    CHECK(pages.size() >= 10);
}

TEST_CASE("Resolved styles reach the model", "[controller]")
{
    LayoutController c = seedController();
    c.jumpTo(u"items-burgers"_s);

    const QVariantMap normal = role(c, u"flow-send"_s, ZoneModel::StyleNormalRole).toMap();
    CHECK(normal.value(u"fill"_s).toString() == u"#1f8a4c"_s);            // zone
    CHECK(normal.value(u"font"_s).toString() == u"DejaVu Sans"_s);        // theme
    const QVariantMap selected = role(c, u"flow-send"_s, ZoneModel::StyleSelectedRole).toMap();
    CHECK(selected.value(u"frame"_s).toString() == u"inset"_s);
    const QVariantMap title = role(c, u"title"_s, ZoneModel::StyleNormalRole).toMap();
    CHECK(title.value(u"frame"_s).toString() == u"none"_s);               // theme kind: label
}

TEST_CASE("Unknown targets and disabled zones do nothing", "[controller]")
{
    LayoutController c = seedController();
    CHECK_FALSE(c.jumpTo(u"nowhere"_s));
    CHECK(c.pageId() == u"login"_s);

    REQUIRE(c.jumpTo(u"library"_s));
    QSignalSpy pages(&c, &LayoutController::pageChanged);
    c.activate(u"lib-disabled"_s);
    c.activate(u"no-such-zone"_s);
    CHECK(pages.isEmpty());
    CHECK(c.pageId() == u"library"_s);
}
