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

TEST_CASE("Menu Builder: a choice group made, used, changed, removed", "[menubuilder][choices]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const auto group = [&](const QString &name) {
        for (const QVariant &g : pos.choiceGroups())
            if (g.toMap()[u"name"_s] == name)
                return g.toMap();
        return QVariantMap{};
    };
    const QVariantList options{QVariantMap{{u"name"_s, u"Pico de gallo"_s}, {u"price"_s, u""_s}},
                               QVariantMap{{u"name"_s, u"Salsa Verde"_s}, {u"price"_s, u"0.50"_s}},
                               QVariantMap{{u"name"_s, u""_s}},   // a row left empty
                               QVariantMap{{u"name"_s, u"Onion"_s}, {u"included"_s, true}}};
    REQUIRE(pos.saveChoiceGroup({{u"name"_s, u"Salsa"_s}, {u"min"_s, 1}, {u"max"_s, 1}, {u"askHow"_s, true},
                                 {u"options"_s, options}}));
    QVariantMap salsa = group(u"Salsa"_s);
    REQUIRE_FALSE(salsa.isEmpty());
    CHECK(salsa[u"rule"_s] == u"Pick 1"_s);
    CHECK(salsa[u"options"_s].toList().size() == 3);
    CHECK(salsa[u"options"_s].toList()[1].toMap()[u"price"_s].toDouble() == 0.5);
    CHECK(salsa[u"options"_s].toList()[2].toMap()[u"included"_s].toBool());
    CHECK_FALSE(pos.saveChoiceGroup({{u"name"_s, u"Bad"_s}, {u"options"_s, QVariantList{QVariantMap{{u"name"_s, u"A"_s}, {u"price"_s, u"x"_s}}}}}));
    CHECK_FALSE(pos.saveChoiceGroup({{u"name"_s, u"Empty"_s}, {u"options"_s, QVariantList{}}}));
    CHECK_FALSE(pos.saveChoiceGroup({{u"name"_s, u"Twice"_s},
                                     {u"options"_s, QVariantList{QVariantMap{{u"name"_s, u"A"_s}}, QVariantMap{{u"name"_s, u"a"_s}}}}}));

    // On an item's card; changed to "up to 2"; removed: the item stops asking.
    REQUIRE(pos.saveMenuItemCard({{u"id"_s, u"classic-burger"_s}, {u"name"_s, u"Classic Burger"_s},
                                  {u"groups"_s, QStringList{u"temperature"_s, salsa[u"id"_s].toString()}}}));
    CHECK(group(u"Salsa"_s)[u"usedBy"_s].toStringList() == QStringList{u"Classic Burger"_s});
    REQUIRE(pos.saveChoiceGroup({{u"id"_s, salsa[u"id"_s]}, {u"name"_s, u"Salsa"_s}, {u"min"_s, 0}, {u"max"_s, 2},
                                 {u"options"_s, options}}));
    CHECK(group(u"Salsa"_s)[u"rule"_s] == u"Up to 2"_s);
    REQUIRE(pos.deleteChoiceGroup(salsa[u"id"_s].toString()));
    CHECK(group(u"Salsa"_s).isEmpty());
    for (const core::MenuItem &m : pos.shared()->menu)
        if (m.id == "classic-burger")
            CHECK(std::ranges::find(m.modifierGroups, salsa[u"id"_s].toString().toStdString()) == m.modifierGroups.end());
}

TEST_CASE("Menu Builder: many items typed at once, in any of the usual ways", "[menubuilder][fast]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    // A new category, then items several ways; another category's line.
    const int added = pos.addMenuItemsFromText(u"burgers"_s,
        u"Tacos: Carne Asada 3.50, Al Pastor $3.25, Pollo - 3\n"
        u"Lengua 4,25\n"
        u"\n"
        u"Drinks: Horchata 2.75\n"
        u"Agua Fresca 2.50"_s);
    CHECK(added == 6);
    const auto priceOf = [&](const QString &name) {
        for (const QVariant &m : pos.menuItems())
            if (m.toMap()[u"name"_s] == name)
                return m.toMap()[u"family"_s].toString() + u' ' + m.toMap()[u"price"_s].toString();
        return QString();
    };
    CHECK(priceOf(u"Carne Asada"_s) == u"tacos $3.50"_s);
    CHECK(priceOf(u"Al Pastor"_s) == u"tacos $3.25"_s);
    CHECK(priceOf(u"Pollo"_s) == u"tacos $3.00"_s);
    CHECK(priceOf(u"Lengua"_s) == u"tacos $4.25"_s);   // a comma for the cents
    CHECK(priceOf(u"Horchata"_s) == u"drinks $2.75"_s);
    CHECK(priceOf(u"Agua Fresca"_s) == u"drinks $2.50"_s);

    // In the category chosen; one without a price says which; already there: skipped.
    CHECK(pos.addMenuItemsFromText(u"tacos"_s, u"Barbacoa 3.75\nCarne Asada 3.50"_s) == 1);
    CHECK(pos.addMenuItemsFromText(u"tacos"_s, u"Suadero\nChorizo 3"_s) == 0);   // nothing added: Suadero has no price
    CHECK(priceOf(u"Chorizo"_s).isEmpty());
    CHECK(pos.addMenuItemsFromText({}, u"Mole 9"_s) == 0);                        // which category?
}

TEST_CASE("Menu Builder: Duplicate copies an item with its choices and what's on it", "[menubuilder][fast]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.saveMenuItemCard({{u"name"_s, u"Fish Tacos"_s}, {u"price"_s, u"13.50"_s}, {u"family"_s, u"plates"_s},
                                  {u"onIt"_s, u"cabbage, crema"_s}, {u"groups"_s, QStringList{u"dressing"_s}}}));
    QString id;
    for (const QVariant &m : pos.menuItems())
        if (m.toMap()[u"name"_s] == u"Fish Tacos"_s)
            id = m.toMap()[u"id"_s].toString();
    REQUIRE(pos.duplicateMenuItem(id));
    QVariantMap copy;
    for (const QVariant &m : pos.menuItems())
        if (m.toMap()[u"name"_s] == u"Fish Tacos 2"_s)
            copy = m.toMap();
    REQUIRE_FALSE(copy.isEmpty());
    CHECK(copy[u"price"_s] == u"$13.50"_s);
    CHECK(copy[u"onIt"_s].toStringList() == QStringList{u"cabbage"_s, u"crema"_s});
    CHECK(copy[u"groups"_s].toStringList() == QStringList{u"dressing"_s});
    // Its own What's on it: changing it doesn't change the original's.
    REQUIRE(pos.saveMenuItemCard({{u"id"_s, copy[u"id"_s]}, {u"name"_s, u"Shrimp Tacos"_s}, {u"onIt"_s, u"slaw"_s}}));
    for (const QVariant &m : pos.menuItems())
        if (m.toMap()[u"name"_s] == u"Fish Tacos"_s)
            CHECK(m.toMap()[u"onIt"_s].toStringList() == QStringList{u"cabbage"_s, u"crema"_s});
}

TEST_CASE("Menu Builder: an item or a category moved to an exact place", "[menubuilder][drag]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const auto burgers = [&] {
        QStringList ids;
        for (const QVariant &m : pos.menuItems())
            if (m.toMap()[u"family"_s] == u"burgers"_s && !m.toMap()[u"modifier"_s].toBool())
                ids << m.toMap()[u"id"_s].toString();
        return ids;
    };
    const QStringList before = burgers();
    REQUIRE(before.first() == u"classic-burger"_s);
    REQUIRE(pos.moveMenuItemTo(u"classic-burger"_s, 3));
    QStringList expected = before;
    expected.move(0, 3);
    CHECK(burgers() == expected);
    REQUIRE(pos.moveMenuItemTo(u"classic-burger"_s, 0));
    CHECK(burgers() == before);
    // The other categories' items keep their places.
    CHECK(pos.shared()->menu.front().id == "classic-burger");

    const auto order = [&] {
        QStringList ids;
        for (const QVariant &c : pos.menuCategories())
            ids << c.toMap()[u"id"_s].toString();
        return ids;
    };
    REQUIRE(pos.moveCategoryTo(u"drinks"_s, 0));
    CHECK(order().first() == u"drinks"_s);
}
