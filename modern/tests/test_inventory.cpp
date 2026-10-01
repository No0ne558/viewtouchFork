#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/async_writer.hh"
#include "storage/pos_store.hh"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Inventory: recipes use stock up, sold out by itself, low stock, food cost.

namespace {

double onHand(PosService &pos, const char *id)
{
    const core::Ingredient *g = pos.shared()->ingredient(id);
    REQUIRE(g);
    return g->onHand;
}

int indexOf(PosService &pos, const char *id)
{
    for (int i = 0; i < int(pos.shared()->ingredients.size()); ++i)
        if (pos.shared()->ingredients[i].id == id)
            return i;
    return -1;
}

void setOnHand(PosService &pos, const char *id, double amount)
{
    const int i = indexOf(pos, id);
    QVariantMap r = pos.adminRecords(u"inventory"_s)[i].toMap();
    r[u"onHand"_s] = amount;
    REQUIRE(pos.adminSave(u"inventory"_s, i, r));
}

QStringList cellsOf(const QVariantMap &report, const QString &first)
{
    for (const QVariant &v : report[u"rows"_s].toList()) {
        const QStringList cells = v.toMap()[u"cells"_s].toStringList();
        if (!cells.isEmpty() && cells.first() == first)
            return cells;
    }
    return {};
}

} // namespace

TEST_CASE("Inventory: sending uses stock, a void gives it back, modifiers count", "[inventory]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.shared()->ingredients.size() >= 10);
    const double buns = onHand(pos, "bun");
    const double bacon = onHand(pos, "bacon");
    const double potatoes = onHand(pos, "potatoes");

    REQUIRE(pos.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"bacon-burger"_s);                  // bun, patty, cheese, 2 bacon
    pos.addItem(u"fries"_s);                         // a modifier: 6 oz potatoes
    pos.addItem(u"cheeseburger"_s);
    CHECK(onHand(pos, "bun") == buns);               // nothing until it goes to the kitchen
    REQUIRE(pos.sendOrder());
    CHECK(onHand(pos, "bun") == buns - 2);
    CHECK(onHand(pos, "bacon") == bacon - 2);
    CHECK(onHand(pos, "potatoes") == potatoes - 6);

    REQUIRE(pos.voidItem());                         // the cheeseburger, never made
    CHECK(onHand(pos, "bun") == buns - 1);

    // Two of an item: twice the recipe; "No" on a modifier uses none.
    pos.addItem(u"two-eggs"_s);
    pos.lines();
    const qint64 eggsBefore = qint64(onHand(pos, "eggs"));
    REQUIRE(pos.sendOrder());
    CHECK(qint64(onHand(pos, "eggs")) == eggsBefore - 2);
}

TEST_CASE("Inventory: sold out by itself when short, back when restocked; low-stock warning", "[inventory]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QStringList notices;
    QObject::connect(&pos, &PosService::notice, [&](const QString &n) { notices << n; });

    setOnHand(pos, "veggie-patty", 1);               // low (at 4) - and one left
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"veggie-burger"_s);
    REQUIRE(pos.sendOrder());
    CHECK(onHand(pos, "veggie-patty") == 0);
    CHECK(pos.soldOut().contains(u"veggie-burger"_s));
    CHECK(notices.join(u'|').contains(u"now sold out: Veggie Burger"_s));
    CHECK_FALSE(pos.addItem(u"veggie-burger"_s));     // can't be ordered
    CHECK(pos.lowStock().size() >= 1);
    CHECK(pos.adminRecords(u"inventory"_s)[indexOf(pos, "veggie-patty")].toMap()[u"_detail"_s]
              .toString().contains(u"OUT"_s));

    // Crossing the low mark warns once.
    notices.clear();
    setOnHand(pos, "bun", 13);                       // low at 12
    pos.addItem(u"kids-burger"_s);
    REQUIRE(pos.sendOrder());
    CHECK(notices.join(u'|').contains(u"Running low: Burger Buns (12 each left)"_s));

    // Restocked: back on the menu.
    setOnHand(pos, "veggie-patty", 12);
    CHECK_FALSE(pos.soldOut().contains(u"veggie-burger"_s));
    // One a person 86'd stays sold out after a restock.
    REQUIRE(pos.setAvailable(u"cobb"_s, false));
    setOnHand(pos, "chicken", 30);
    CHECK(pos.soldOut().contains(u"cobb"_s));
}

TEST_CASE("Recipes in Manager -> Menu, ingredients in Manager -> Inventory", "[inventory][admin]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    // A new ingredient, then a recipe that uses it (by name or id).
    QVariantMap g = pos.adminNewRecord(u"inventory"_s);
    g[u"name"_s] = u"Avocado"_s;
    g[u"unit"_s] = u"each"_s;
    g[u"onHand"_s] = 10.0;
    g[u"lowAt"_s] = 3.0;
    g[u"cost"_s] = 1.25;
    REQUIRE(pos.adminSave(u"inventory"_s, -1, g));
    REQUIRE(pos.shared()->ingredient("avocado"));

    int cobb = -1;
    for (int i = 0; i < int(pos.shared()->menu.size()); ++i)
        if (pos.shared()->menu[i].id == "cobb") cobb = i;
    QVariantMap r = pos.adminRecords(u"menu"_s)[cobb].toMap();
    CHECK(r[u"recipe"_s].toString().startsWith(u"lettuce 4\nchicken 1"_s));
    r[u"recipe"_s] = r[u"recipe"_s].toString() + u"\nAvocado 0.5"_s;
    REQUIRE(pos.adminSave(u"menu"_s, cobb, r));
    CHECK(pos.shared()->menu[cobb].recipe.back() == core::RecipeLine{"avocado", 0.5});
    r[u"recipe"_s] = u"unicorn 1"_s;
    CHECK_FALSE(pos.adminSave(u"menu"_s, cobb, r));
    r[u"recipe"_s] = u"bacon -2"_s;
    CHECK_FALSE(pos.adminSave(u"menu"_s, cobb, r));

    // Saved with the item and the ingredient.
    const core::MenuItem &m = pos.shared()->menu[cobb];
    CHECK(app::menuItemFromJson(app::toJson(m)) == m);
    const core::Ingredient &a = *pos.shared()->ingredient("avocado");
    CHECK(app::ingredientFromJson(app::toJson(a)) == a);
}

TEST_CASE("Food cost report: sales against recipe cost, and the stock", "[inventory][reports]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    REQUIRE(pos.startCheck(core::CheckType::Takeout));
    pos.addItem(u"classic-burger"_s);                // $11.50; bun .35 + patty 1.60 + lettuce .06 = $2.01
    pos.addItem(u"water"_s);                         // no recipe
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());

    const QVariantMap report = pos.report(u"foodcost"_s);
    const QStringList burger = cellsOf(report, u"Classic Burger"_s);
    REQUIRE(burger.size() == 5);
    CHECK(burger[1] == u"1"_s);
    CHECK(burger[3] == u"$2.01"_s);
    CHECK(burger[4] == u"17.5%"_s);
    CHECK(cellsOf(report, u"Water"_s)[3] == u"-"_s);
    CHECK(cellsOf(report, u"Burger Buns"_s).size() == 5);
}

TEST_CASE("Inventory is saved and comes back; starter stock on a new store", "[inventory][store]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    const auto seed = test::seedPosData();
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        REQUIRE(store.seed(seed.settings, seed.menu, seed.employees, nullptr, seed.ingredients));
    }
    {
        storage::PosStore store(path);
        REQUIRE(store.open());
        storage::AsyncWriter writer(path);
        storage::SqlPosSink sink(writer);
        PosService pos(*store.load(), &sink);
        REQUIRE(pos.loginWithPin(u"1111"_s));
        REQUIRE(pos.startCheck(core::CheckType::Takeout));
        pos.addItem(u"kids-burger"_s);
        pos.finishChoosing();
        REQUIRE(pos.sendOrder());
    }
    storage::PosStore store(path);
    REQUIRE(store.open());
    const auto data = store.load();
    REQUIRE(data);
    REQUIRE(data->ingredients.size() == seed.ingredients.size());
    CHECK(data->ingredients[0].id == "bun");
    CHECK(data->ingredients[0].onHand == seed.ingredients[0].onHand - 1);
}
