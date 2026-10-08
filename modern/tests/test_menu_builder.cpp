#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "qt_catch.hh"

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// The Menu Builder: categories, an item's card, what's on it.

namespace {

QVariantMap category(PosService &pos, const QString &id)
{
    for (const QVariant &c : pos.menuCategories())
        if (c.toMap()[u"id"_s] == id)
            return c.toMap();
    return {};
}

QVariantMap item(PosService &pos, const QString &name)
{
    for (const QVariant &m : pos.menuItems())
        if (m.toMap()[u"name"_s] == name)
            return m.toMap();
    return {};
}

} // namespace

TEST_CASE("Menu Builder: a category, then an item in it with what's on it", "[menubuilder]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.saveCategory({{u"name"_s, u"Tacos"_s}, {u"color"_s, u"#b83232"_s},
                              {u"periods"_s, QStringList{u"lunch"_s, u"dinner"_s}},
                              {u"printer"_s, u"kitchen"_s}, {u"station"_s, u"grill"_s}}));
    QVariantMap tacos = category(pos, u"tacos"_s);
    REQUIRE_FALSE(tacos.isEmpty());
    CHECK(tacos[u"color"_s] == u"#b83232"_s);
    CHECK(tacos[u"periods"_s].toStringList() == QStringList{u"lunch"_s, u"dinner"_s});
    CHECK_FALSE(pos.saveCategory({{u"name"_s, u"tacos"_s}}));   // already one

    // Every meal: all day.
    REQUIRE(pos.saveCategory({{u"id"_s, u"tacos"_s}, {u"name"_s, u"Tacos"_s},
                              {u"periods"_s, QStringList{u"breakfast"_s, u"lunch"_s, u"dinner"_s}}}));
    CHECK(category(pos, u"tacos"_s)[u"periods"_s].toStringList().isEmpty());

    REQUIRE(pos.saveMenuItemCard({{u"name"_s, u"Carne Asada"_s}, {u"price"_s, u"3.50"_s}, {u"family"_s, u"tacos"_s},
                                  {u"onIt"_s, u"Cilantro, Onion, Salsa Verde"_s}, {u"groups"_s, QStringList{u"salad-protein"_s}}}));
    const QVariantMap asada = item(pos, u"Carne Asada"_s);
    REQUIRE_FALSE(asada.isEmpty());
    CHECK(asada[u"price"_s] == u"$3.50"_s);
    CHECK(asada[u"station"_s] == u"grill"_s);                    // its category's
    CHECK(asada[u"onIt"_s].toStringList() == QStringList{u"Cilantro"_s, u"Onion"_s, u"Salsa Verde"_s});
    CHECK(asada[u"groups"_s].toStringList() == QStringList{u"salad-protein"_s});
    CHECK_FALSE(pos.saveMenuItemCard({{u"name"_s, u"carne asada"_s}, {u"price"_s, 4}, {u"family"_s, u"tacos"_s}}));
    CHECK_FALSE(pos.saveMenuItemCard({{u"name"_s, u"Al Pastor"_s}, {u"price"_s, u"abc"_s}, {u"family"_s, u"tacos"_s}}));
    CHECK_FALSE(pos.saveMenuItemCard({{u"name"_s, u"Al Pastor"_s}, {u"price"_s, 3}}));   // which category?

    // Ordered: what's on it can be had No / Lite / Extra / on the Side.
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.addItem(asada[u"id"_s].toString()));
    const QVariantList groups = pos.choosingInfo()[u"groups"_s].toList();
    REQUIRE(groups.size() == 2);
    const QVariantMap onIt = groups[0].toMap();
    CHECK(onIt[u"included"_s].toBool());
    REQUIRE(pos.setChoice(onIt[u"id"_s].toString(), 1, u"no"_s));   // no onion
    REQUIRE(pos.finishChoosing());
    CHECK(pos.lines()[0].toMap()[u"modifiers"_s].toList().size() == 1);
    pos.releaseCheck();

    // Changing what's on it; the category can't go while it has items.
    REQUIRE(pos.saveMenuItemCard({{u"id"_s, asada[u"id"_s]}, {u"name"_s, u"Carne Asada"_s}, {u"onIt"_s, QStringList{u"Cilantro"_s}}}));
    CHECK(item(pos, u"Carne Asada"_s)[u"onIt"_s].toStringList() == QStringList{u"Cilantro"_s});
    CHECK_FALSE(pos.deleteCategory(u"tacos"_s));
    REQUIRE(pos.deleteMenuItemCard(asada[u"id"_s].toString()));
    CHECK(item(pos, u"Carne Asada"_s).isEmpty());
    CHECK_FALSE(pos.shared()->settings.modifierGroup("on-" + asada[u"id"_s].toString().toStdString()));
    REQUIRE(pos.deleteCategory(u"tacos"_s));
    CHECK(category(pos, u"tacos"_s).isEmpty());
}

TEST_CASE("Menu Builder: categories in order; moving an item takes its kitchen along", "[menubuilder]")
{
    PosService pos(test::seedPosData(true), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const auto order = [&] {
        QStringList ids;
        for (const QVariant &c : pos.menuCategories())
            ids << c.toMap()[u"id"_s].toString();
        return ids;
    };
    const int burgers = int(order().indexOf(u"burgers"_s));
    REQUIRE(pos.moveCategory(u"burgers"_s, -1));
    CHECK(order().indexOf(u"burgers"_s) == burgers - 1);

    // Coffee (drinks: the bar) moved to Breakfast Plates (the kitchen).
    const QVariantMap coffee = item(pos, u"Coffee"_s);
    REQUIRE(coffee[u"printer"_s] == u"bar"_s);
    REQUIRE(pos.saveMenuItemCard({{u"id"_s, coffee[u"id"_s]}, {u"name"_s, u"Coffee"_s}, {u"family"_s, u"breakfast"_s}}));
    CHECK(item(pos, u"Coffee"_s)[u"printer"_s] == u"kitchen"_s);
    CHECK(item(pos, u"Coffee"_s)[u"family"_s] == u"breakfast"_s);

    // Choice groups list who uses them.
    for (const QVariant &g : pos.choiceGroups())
        if (g.toMap()[u"id"_s] == u"temperature"_s) {
            CHECK(g.toMap()[u"rule"_s] == u"Pick 1"_s);
            CHECK(g.toMap()[u"usedBy"_s].toStringList().contains(u"Classic Burger"_s));
        }

    // Not a manager: no.
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK_FALSE(pos.saveCategory({{u"name"_s, u"Pies"_s}}));
}
