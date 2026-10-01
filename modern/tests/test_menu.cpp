#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "layoutcontroller.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QDateTime>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// The menu while ordering: modifier groups, meal-period prices, 86.

namespace {

QVariantMap groupOf(const PosService &pos, const QString &id)
{
    for (const QVariant &g : pos.choosingInfo()[u"groups"_s].toList()) {
        if (g.toMap()[u"id"_s] == id)
            return g.toMap();
    }
    return {};
}

std::int64_t todayAt(int hour)
{
    return QDateTime(QDate::currentDate(), QTime(hour, 0)).toMSecsSinceEpoch();
}

} // namespace

TEST_CASE("Modifier groups: choose one, up to N, required before Done", "[menu][modifiers]")
{
    PosService pos(test::seedPosData(true), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.addItem(u"house-salad"_s));                // $8.50, dressing + protein
    QVariantMap choosing = pos.choosingInfo();
    REQUIRE(choosing[u"active"_s].toBool());
    CHECK(choosing[u"item"_s] == u"House Salad"_s);
    CHECK(groupOf(pos, u"dressing"_s)[u"rule"_s] == u"Choose 1"_s);
    CHECK_FALSE(groupOf(pos, u"dressing"_s)[u"done"_s].toBool());
    CHECK(groupOf(pos, u"salad-protein"_s)[u"done"_s].toBool());   // optional

    CHECK_FALSE(pos.finishChoosing());                    // dressing first
    REQUIRE(pos.chooseOption(u"dressing"_s, 0));           // Ranch
    REQUIRE(pos.chooseOption(u"dressing"_s, 1));           // Blue Cheese replaces it
    QVariantList mods = pos.lines().first().toMap()[u"modifiers"_s].toList();
    REQUIRE(mods.size() == 1);
    CHECK(mods.first().toMap()[u"name"_s] == u"Blue Cheese"_s);
    REQUIRE(pos.chooseOption(u"salad-protein"_s, 2));      // Salmon + 6.00
    CHECK(pos.lines().first().toMap()[u"price"_s] == u"$14.50"_s);
    REQUIRE(pos.chooseOption(u"salad-protein"_s, 2));      // touched again: off
    CHECK(pos.lines().first().toMap()[u"price"_s] == u"$8.50"_s);
    REQUIRE(pos.finishChoosing());
    CHECK_FALSE(pos.choosingInfo()[u"active"_s].toBool());

    // Up to three fillings.
    REQUIRE(pos.addItem(u"omelette"_s));
    for (int i : {0, 1, 2})
        REQUIRE(pos.chooseOption(u"omelette-fillings"_s, i));
    CHECK_FALSE(pos.chooseOption(u"omelette-fillings"_s, 3));
    REQUIRE(pos.chooseOption(u"omelette-fillings"_s, 1));  // Ham off...
    REQUIRE(pos.chooseOption(u"omelette-fillings"_s, 3));  // ...Peppers on
    CHECK(groupOf(pos, u"omelette-fillings"_s)[u"chosen"_s] == 3);
    CHECK_FALSE(pos.finishChoosing());                    // toast is required too
    REQUIRE(pos.chooseOption(u"toast"_s, 2));              // Sourdough
    REQUIRE(pos.finishChoosing());

    // Cancel Item takes it off.
    REQUIRE(pos.addItem(u"two-eggs"_s));
    CHECK(pos.lines().size() == 3);
    REQUIRE(pos.cancelChoosing());
    CHECK(pos.lines().size() == 2);
    CHECK_FALSE(pos.choosingInfo()[u"active"_s].toBool());

    // Items without groups don't ask.
    REQUIRE(pos.addItem(u"water"_s));
    CHECK_FALSE(pos.choosingInfo()[u"active"_s].toBool());

    // The choices are saved with the check.
    const core::Check &c = pos.shared()->open.begin()->second;
    CHECK(c.lines[0].modifiers[0].group == "dressing");
    CHECK(app::checkFromJson(app::toJson(c))->lines[0].modifiers[0].group == "dressing");
}

TEST_CASE("Ordering an item with choices opens the Choose page", "[menu][modifiers][ui]")
{
    auto l = layout::Layout::loadDirectory(QStringLiteral(VTM_SEED_DIR));
    REQUIRE(l);
    PosService pos(test::seedPosData(true), nullptr);
    LayoutController c(*l);
    c.setMealPeriod(u"lunch"_s);
    c.setPos(&pos);
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(c.jumpTo(u"items-salads"_s));
    c.activate(u"item-1"_s);                                // House Salad
    CHECK(c.pageId() == u"modifiers"_s);
    REQUIRE(pos.chooseOption(u"dressing"_s, 0));
    c.finishChoosing();
    CHECK(c.pageId() == u"items-salads"_s);
}

TEST_CASE("Prices change with the meal period", "[menu][prices]")
{
    app::PosData data = test::seedPosData();
    for (core::MenuItem &m : data.menu) {
        if (m.id == "classic-burger")
            m.periodPrices["dinner"] = Money::fromCents(1250);
    }
    PosService pos(data, nullptr);
    std::int64_t clock = todayAt(12);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1111"_s));
    REQUIRE(pos.addItem(u"classic-burger"_s));
    CHECK(pos.lines().last().toMap()[u"price"_s] == u"$11.50"_s);   // lunch
    clock = todayAt(19);
    REQUIRE(pos.addItem(u"classic-burger"_s));
    CHECK(pos.lines().last().toMap()[u"price"_s] == u"$12.50"_s);   // dinner
    CHECK(pos.lines().first().toMap()[u"price"_s] == u"$11.50"_s);  // already ordered: unchanged
}

TEST_CASE("Sold out (86): can't be ordered, marked on its buttons", "[menu][86]")
{
    auto l = layout::Layout::loadDirectory(QStringLiteral(VTM_SEED_DIR));
    REQUIRE(l);
    PosService pos(test::seedPosData(true), nullptr);
    LayoutController c(*l);
    c.setPos(&pos);
    REQUIRE(pos.loginWithPin(u"1111"_s));                 // servers may 86
    REQUIRE(pos.setAvailable(u"cobb"_s, false));
    CHECK(pos.soldOut().contains(u"cobb"_s));
    CHECK(pos.soldOut().contains(u"cobb"_s));
    CHECK_FALSE(pos.addItem(u"cobb"_s));
    CHECK_FALSE(pos.menuItems().isEmpty());

    REQUIRE(c.jumpTo(u"items-salads"_s));
    ZoneModel *m = c.zones();
    bool marked = false, othersFine = true;
    for (int r = 0; r < m->rowCount(); ++r) {
        const QModelIndex i = m->index(r);
        if (m->data(i, ZoneModel::LabelRole) == u"Cobb"_s)
            marked = m->data(i, ZoneModel::SoldOutRole).toBool() && !m->data(i, ZoneModel::ZoneEnabledRole).toBool();
        else if (m->data(i, ZoneModel::SoldOutRole).toBool())
            othersFine = false;
    }
    CHECK(marked);
    CHECK(othersFine);

    REQUIRE(pos.setAvailable(u"cobb"_s, true));
    CHECK(pos.addItem(u"cobb"_s));
    CHECK_FALSE(pos.setAvailable(u"nothing"_s, false));
}

TEST_CASE("Manager screens: modifier groups and an item's groups and prices", "[menu][admin]")
{
    PosService pos(test::seedPosData(true), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    QVariantMap g = pos.adminNewRecord(u"modifierGroups"_s);
    g[u"name"_s] = u"Size"_s;
    g[u"options"_s] = u"Small\nLarge + 1.50\n"_s;
    REQUIRE(pos.adminSave(u"modifierGroups"_s, -1, g));
    const core::ModifierGroup *size = pos.shared()->settings.modifierGroup("size");
    REQUIRE(size);
    REQUIRE(size->options.size() == 2);
    CHECK(size->options[1].name == "Large");
    CHECK(size->options[1].price.cents() == 150);
    g[u"options"_s] = u"Large + lots"_s;
    CHECK_FALSE(pos.adminSave(u"modifierGroups"_s, -1, g));
    g[u"options"_s] = u""_s;
    CHECK_FALSE(pos.adminSave(u"modifierGroups"_s, -1, g));

    // The coffee: a size, and an evening price.
    const QVariantList menu = pos.adminRecords(u"menu"_s);
    int coffee = -1;
    for (int i = 0; i < menu.size(); ++i) {
        if (menu[i].toMap()[u"id"_s] == u"coffee"_s)
            coffee = i;
    }
    REQUIRE(coffee >= 0);
    QVariantMap item = menu[coffee].toMap();
    item[u"modifierGroups"_s] = u"nope"_s;
    CHECK_FALSE(pos.adminSave(u"menu"_s, coffee, item));
    item[u"modifierGroups"_s] = u"size"_s;
    item[u"periodPrices"_s] = u"dinner 3"_s;
    CHECK_FALSE(pos.adminSave(u"menu"_s, coffee, item));
    item[u"periodPrices"_s] = u"dinner = 3.25"_s;
    REQUIRE(pos.adminSave(u"menu"_s, coffee, item));
    const core::MenuItem &saved = pos.shared()->menu[coffee];
    CHECK(saved.modifierGroups == std::vector<std::string>{"size"});
    CHECK(saved.periodPrices.at("dinner").cents() == 325);
    const QVariantMap back = pos.adminRecords(u"menu"_s)[coffee].toMap();
    CHECK(back[u"modifierGroups"_s] == u"size"_s);
    CHECK(back[u"periodPrices"_s] == u"dinner = 3.25"_s);

    // Saved with the settings and the menu.
    CHECK(app::settingsFromJson(app::toJson(pos.shared()->settings)).modifierGroups
          == pos.shared()->settings.modifierGroups);
    CHECK(app::menuItemFromJson(app::toJson(saved)) == saved);
}

TEST_CASE("Demo menu: every group an item names exists and makes sense", "[menu][modifiers][seed]")
{
    const app::PosData data = test::seedPosData(true);
    CHECK(data.settings.modifierGroups.size() >= 15);
    int itemsWithGroups = 0;
    for (const core::MenuItem &m : data.menu) {
        if (!m.modifierGroups.empty())
            ++itemsWithGroups;
        for (const std::string &g : m.modifierGroups) {
            INFO(m.id << " -> " << g);
            CHECK(data.settings.modifierGroup(g) != nullptr);
        }
    }
    CHECK(itemsWithGroups >= 16);
    for (const core::ModifierGroup &g : data.settings.modifierGroups) {
        INFO(g.id);
        CHECK_FALSE(g.options.empty());
        CHECK(g.min <= int(g.options.size()));
        CHECK((g.max == 0 || g.min <= g.max));
    }
}

TEST_CASE("Demo drinks and breakfast: sizes, bottles, any number of add-ons", "[menu][modifiers][seed]")
{
    PosService pos(test::seedPosData(true), nullptr);
    REQUIRE(pos.loginWithPin(u"1111"_s));

    REQUIRE(pos.addItem(u"soda"_s));                        // $2.95
    REQUIRE(pos.chooseOption(u"drink-size"_s, 2));          // Large + 1.00
    REQUIRE(pos.finishChoosing());
    CHECK(pos.lines().last().toMap()[u"price"_s] == u"$3.95"_s);

    REQUIRE(pos.addItem(u"house-wine"_s));                  // $8.00
    CHECK_FALSE(pos.finishChoosing());                      // which wine, and how much
    REQUIRE(pos.chooseOption(u"wine"_s, 0));
    REQUIRE(pos.chooseOption(u"wine-pour"_s, 1));           // Bottle + 22.00
    REQUIRE(pos.finishChoosing());
    CHECK(pos.lines().last().toMap()[u"price"_s] == u"$30.00"_s);

    REQUIRE(pos.addItem(u"pancakes"_s));                    // $9.50
    REQUIRE(pos.chooseOption(u"syrup"_s, 0));
    for (int i = 0; i < 5; ++i)                             // add-ons: any number
        REQUIRE(pos.chooseOption(u"breakfast-add-ons"_s, i));
    REQUIRE(pos.finishChoosing());
    CHECK(pos.lines().last().toMap()[u"modifiers"_s].toList().size() == 6);
    CHECK(pos.lines().last().toMap()[u"price"_s] == u"$21.50"_s);   // 9.50 + 3 + 3 + 2.50 + 2.75 + 0.75

    REQUIRE(pos.addItem(u"kids-burger"_s));                 // $7.50
    REQUIRE(pos.chooseOption(u"kids-side"_s, 1));           // Apple Slices
    REQUIRE(pos.chooseOption(u"kids-drink"_s, 2));          // Chocolate Milk + 0.50
    REQUIRE(pos.finishChoosing());
    CHECK(pos.lines().last().toMap()[u"price"_s] == u"$8.00"_s);
}

TEST_CASE("Send, Fire and Close wait for required choices", "[menu][modifiers]")
{
    PosService pos(test::seedPosData(true), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    REQUIRE(pos.selectTable(u"T7"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    REQUIRE(pos.addItem(u"tea"_s));                         // hot or iced?
    pos.addItem(u"water"_s);                                // left the Choose page without choosing
    const qint64 tea = pos.lines().first().toMap()[u"id"_s].toLongLong();
    CHECK(pos.lines().first().toMap()[u"needsChoice"_s].toBool());
    CHECK(pos.lines().first().toMap()[u"choices"_s].toBool());
    CHECK_FALSE(pos.lines().last().toMap()[u"choices"_s].toBool());

    CHECK_FALSE(pos.sendOrder());
    REQUIRE(pos.tender(u"cash"_s));
    CHECK_FALSE(pos.closeCheck());

    // Back to its choices from the check.
    REQUIRE(pos.chooseLine(tea));
    CHECK(pos.choosingInfo()[u"item"_s] == u"Tea"_s);
    REQUIRE(pos.chooseOption(u"hot-or-iced"_s, 1));
    REQUIRE(pos.finishChoosing());
    CHECK_FALSE(pos.lines().first().toMap()[u"needsChoice"_s].toBool());
    REQUIRE(pos.sendOrder());
    CHECK_FALSE(pos.chooseLine(tea));                       // sent: no more changes

    // A held course is checked when it is fired.
    REQUIRE(pos.setCourse(2));
    REQUIRE(pos.addItem(u"lemonade"_s));
    REQUIRE(pos.sendOrder() == false);                      // course 2 is on hold anyway
    CHECK_FALSE(pos.fireCourse());                          // lemonade needs a size
    REQUIRE(pos.chooseLine(pos.lines().last().toMap()[u"id"_s].toLongLong()));
    REQUIRE(pos.chooseOption(u"drink-size"_s, 0));
    REQUIRE(pos.finishChoosing());
    REQUIRE(pos.fireCourse());
    REQUIRE(pos.tender(u"cash"_s));                         // the lemonade
    REQUIRE(pos.closeCheck());
}
