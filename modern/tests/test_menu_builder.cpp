#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "app/pos_json.hh"
#include "storage/pos_store.hh"
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

#include "app/menu_import.hh"
#include "layoutcontroller.hh"
#include "layout_fixture.hh"

#include <QTemporaryDir>

TEST_CASE("Menu import: the usual spreadsheet shapes", "[menubuilder][import]")
{
    using app::readMenuCsv;
    // A header in any order, quotes, a $ and a thousands comma.
    auto r = readMenuCsv(u"Category,Item,Price,Description\n"
                         u"Tacos,Carne Asada,$3.50,\"Steak, onion, cilantro\"\n"
                         u"Platters,\"Family Pack \"\"Big\"\"\",\"1,250.00\",\n"
                         u",,,\n"
                         u"Tacos,Nachos,,\n"_s);
    REQUIRE(r.items.size() == 2);
    CHECK(r.items[0].name == u"Carne Asada"_s);
    CHECK(r.items[0].price == 3.5);
    CHECK(r.items[0].category == u"Tacos"_s);
    CHECK(r.items[0].description == u"Steak, onion, cilantro"_s);
    CHECK(r.items[1].name == u"Family Pack \"Big\""_s);
    CHECK(r.items[1].price == 1250.0);
    CHECK(r.problems == QStringList{u"Row 5: no price for Nachos"_s});

    // No header: name, price, category. Semicolons and a comma for the cents.
    r = readMenuCsv(u"Horchata;2,75;Bebidas\r\nAgua de Jamaica;2,50;Bebidas\r\n"_s);
    REQUIRE(r.items.size() == 2);
    CHECK(r.items[0].price == 2.75);
    CHECK(r.items[1].category == u"Bebidas"_s);

    // Tabs; Spanish headers; what's on it.
    r = readMenuCsv(u"Nombre\tPrecio\tCategoría\tIngredientes\nTorta\t9.00\tTortas\tfrijol, aguacate\n"_s);
    REQUIRE(r.items.size() == 1);
    CHECK(r.items[0].category == u"Tortas"_s);
    CHECK(r.items[0].onIt == u"frijol, aguacate"_s);
    CHECK(r.columns.contains(u"onIt"_s));
}

TEST_CASE("Menu import: into the menu, categories made, prices updated if asked", "[menubuilder][import]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const QVariantList rows{
        QVariantMap{{u"name"_s, u"Carne Asada"_s}, {u"price"_s, 3.5}, {u"category"_s, u"Tacos"_s}, {u"onIt"_s, u"onion, cilantro"_s}},
        QVariantMap{{u"name"_s, u"Al Pastor"_s}, {u"price"_s, 3.25}, {u"category"_s, u"tacos"_s}},
        QVariantMap{{u"name"_s, u"Classic Burger"_s}, {u"price"_s, 12.0}},   // already on the menu
        QVariantMap{{u"name"_s, u"Garden Burger"_s}, {u"price"_s, 11.0}}};   // no category: the one chosen
    CHECK(pos.importMenuRows(rows, u"burgers"_s, false) == 3);
    const auto item = [&](const QString &name) {
        for (const QVariant &m : pos.menuItems())
            if (m.toMap()[u"name"_s] == name)
                return m.toMap();
        return QVariantMap{};
    };
    CHECK(item(u"Carne Asada"_s)[u"family"_s] == u"tacos"_s);
    CHECK(item(u"Al Pastor"_s)[u"family"_s] == u"tacos"_s);              // the same category, any case
    CHECK(item(u"Carne Asada"_s)[u"onIt"_s].toStringList() == QStringList{u"onion"_s, u"cilantro"_s});
    CHECK(item(u"Garden Burger"_s)[u"family"_s] == u"burgers"_s);
    CHECK(item(u"Classic Burger"_s)[u"price"_s] == u"$11.50"_s);         // left as it was
    CHECK(pos.importMenuRows({rows[2]}, u"burgers"_s, true) == 1);       // the new price, asked for
    CHECK(item(u"Classic Burger"_s)[u"price"_s] == u"$12.00"_s);
}

TEST_CASE("Menu import: reading the file on this device", "[menubuilder][import]")
{
    QTemporaryDir dir;
    QFile csv(dir.filePath(u"menu.csv"_s));
    REQUIRE(csv.open(QIODevice::WriteOnly));
    csv.write("Item,Price,Category\nCaf\xe9 de Olla,2.50,Drinks\n");   // Latin-1, as older spreadsheets save
    csv.close();
    auto l = test::loadTestLayout();
    REQUIRE(l);
    LayoutController c(*l);
    QVariantMap read = c.readMenuFile(QUrl::fromLocalFile(csv.fileName()));
    REQUIRE(read[u"items"_s].toList().size() == 1);
    CHECK(read[u"items"_s].toList()[0].toMap()[u"name"_s] == u"Café de Olla"_s);
    QFile xlsx(dir.filePath(u"menu.xlsx"_s));
    REQUIRE(xlsx.open(QIODevice::WriteOnly));
    xlsx.write("PK\x03\x04 a spreadsheet");
    xlsx.close();
    read = c.readMenuFile(QUrl::fromLocalFile(xlsx.fileName()));
    CHECK(read[u"error"_s].toString().contains(u"CSV"_s));
}

TEST_CASE("Starter menus: added alongside, ready to order", "[menubuilder][templates]")
{
    PosService pos(test::seedPosData(true), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const QVariantList templates = pos.menuTemplates();
    REQUIRE(templates.size() == 4);
    const int before = int(pos.menuItems().size());
    for (const QVariant &t : templates) {
        INFO(t.toMap()[u"id"_s].toString().toStdString());
        REQUIRE(pos.applyMenuTemplate(t.toMap()[u"id"_s].toString()));
    }
    CHECK(pos.menuItems().size() > before + 30);   // those already on the menu (Classic Burger, Fries...) skipped
    QVariantMap taco, latte;
    for (const QVariant &m : pos.menuItems()) {
        if (m.toMap()[u"name"_s] == u"Carne Asada Taco"_s)
            taco = m.toMap();
        if (m.toMap()[u"name"_s] == u"Latte"_s)
            latte = m.toMap();
    }
    REQUIRE_FALSE(taco.isEmpty());
    CHECK(taco[u"price"_s] == u"$3.50"_s);
    CHECK(taco[u"onIt"_s].toStringList() == QStringList{u"onion"_s, u"cilantro"_s});
    CHECK(taco[u"station"_s] == u"grill"_s);
    CHECK(latte[u"printer"_s] == u"bar"_s);
    // Applied again: nothing doubled.
    const auto count = pos.menuItems().size();
    REQUIRE(pos.applyMenuTemplate(u"taqueria"_s));
    CHECK(pos.menuItems().size() == count);
    // A taco orders, asks its salsa and tortilla, and can be had No onion.
    REQUIRE(pos.startCheck(core::CheckType::Quick));
    REQUIRE(pos.addItem(taco[u"id"_s].toString()));
    QStringList asked;
    for (const QVariant &g : pos.choosingInfo()[u"groups"_s].toList())
        asked << g.toMap()[u"name"_s].toString();
    CHECK(asked.contains(u"Salsa"_s));
    CHECK(asked.contains(u"Tortilla"_s));
    CHECK_FALSE(pos.finishChoosing());   // salsa and tortilla are required
}

TEST_CASE("Ready to go: the starter menu is; mistakes are caught", "[menubuilder][check]")
{
    PosService pos(test::seedPosData(true), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const auto problems = [&](bool seriousOnly) {
        QStringList out;
        for (const QVariant &p : pos.menuProblems())
            if (!seriousOnly || p.toMap()[u"serious"_s].toBool())
                out << p.toMap()[u"text"_s].toString();
        return out;
    };
    INFO(problems(false).join(u"\n"_s).toStdString());
    CHECK(problems(true).isEmpty());

    // Mistakes.
    auto &menu = pos.shared()->menu;
    menu[0].printer = "pizza-oven";
    menu[1].modifierGroups.push_back("gone");
    menu[2].name = menu[3].name;
    pos.shared()->settings.modifierGroups.push_back({"empty", "Salsa", 1, 1, {}, false, false});
    REQUIRE(pos.saveCategory({{u"name"_s, u"Desserts"_s}}));
    const QStringList found = problems(false);
    INFO(found.join(u"\n"_s).toStdString());
    CHECK(found.filter(u"pizza-oven"_s).size() == 1);
    CHECK(found.filter(u"choice group that's gone"_s).size() == 1);
    CHECK(found.filter(u"There are two"_s).size() == 1);
    CHECK(found.filter(u"Salsa has no options"_s).size() == 1);
    CHECK(found.filter(u"Desserts has no items yet"_s).size() == 1);
    CHECK(problems(true).size() == 3);   // an empty category is a note; the kitchen prints pizza-oven's
    CHECK(problems(true).filter(u"pizza-oven"_s).isEmpty());

    // No kitchen printer to take them: they print nowhere.
    auto &printers = pos.shared()->settings.printers;
    std::erase_if(printers, [](const core::PrinterConfig &p) { return p.id == "kitchen"; });
    REQUIRE_FALSE(printers.empty());
    CHECK(problems(true).filter(u"pizza-oven"_s).size() == 1);
    // A store with no printers at all (kitchen screens): nothing to say.
    printers.clear();
    CHECK(problems(false).filter(u"printer"_s).isEmpty());
}

TEST_CASE("Menu Builder: prices typed with a comma", "[menubuilder]")
{
    PosService pos(test::seedPosData(true), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const auto priceOf = [&](const QString &name) {
        for (const auto &m : pos.shared()->menu)
            if (QString::fromStdString(m.name) == name)
                return m.price.cents();
        return decltype(pos.shared()->menu.front().price.cents())(-1);
    };
    const QString family = QString::fromStdString(pos.shared()->menu.front().family);
    REQUIRE(pos.saveMenuItemCard({{u"name"_s, u"Churros"_s}, {u"price"_s, u"3,50"_s}, {u"family"_s, family}}));
    CHECK(priceOf(u"Churros"_s) == 350);
    REQUIRE(pos.saveMenuItemCard({{u"name"_s, u"Party Tray"_s}, {u"price"_s, u"$1,250.00"_s}, {u"family"_s, family}}));
    CHECK(priceOf(u"Party Tray"_s) == 125000);
    CHECK_FALSE(pos.saveMenuItemCard({{u"name"_s, u"Flan"_s}, {u"price"_s, u"cheap"_s}, {u"family"_s, family}}));
}

// Prints what the check finds in a store's database: VTM_PROBLEMS_DB=<file>.
TEST_CASE("Ready to go: a store's database", "[.][problemsdb]")
{
    const QByteArray path = qgetenv("VTM_PROBLEMS_DB");
    REQUIRE_FALSE(path.isEmpty());
    vt::storage::PosStore store(QString::fromLocal8Bit(path));
    QString error;
    REQUIRE(store.open(&error));
    QStringList errors;
    auto data = store.load(&errors);
    REQUIRE(data);
    PosService pos(std::move(*data), nullptr);
    for (const QVariant &p : pos.menuProblems())
        WARN((p.toMap()[u"serious"_s].toBool() ? "SERIOUS " : "note ") << p.toMap()[u"text"_s].toString().toStdString());
}

TEST_CASE("Printers: what was learned about one is kept, until its address changes", "[print][learned]")
{
    PosService pos(test::seedPosData(true), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    auto &printers = pos.shared()->settings.printers;
    REQUIRE_FALSE(printers.empty());
    const int at = 0;
    printers[at].type = "network";
    printers[at].host = "192.168.1.101";
    printers[at].reportsStatus = true;
    CHECK(app::settingsFromJson(app::toJson(pos.shared()->settings)).printers[at].reportsStatus);   // stored
    QVariantMap record = app::toJson(printers[at]).toVariantMap();
    record.remove(u"reportsStatus"_s);                    // the form doesn't have it
    record[u"name"_s] = u"Kitchen Epson"_s;
    REQUIRE(pos.adminSave(u"printers"_s, at, record));
    CHECK(printers[at].reportsStatus);                    // renamed: the same printer
    record[u"host"_s] = u"192.168.88.40"_s;
    REQUIRE(pos.adminSave(u"printers"_s, at, record));
    CHECK_FALSE(printers[at].reportsStatus);              // another address: maybe another printer
}

TEST_CASE("Sent by a screen: reports and settings only for managers", "[security][managersonly]")
{
    PosService pos(test::seedPosData(true), nullptr);
    const auto ask = [&](const QString &m, const QVariantList &a) {
        QVariant out;
        pos.invoke(m, a, [&](const QVariant &v) { out = v; });
        return out;
    };
    REQUIRE(pos.loginWithPin(u"2222"_s));   // a server
    CHECK(ask(u"adminRecords"_s, {u"employees"_s}).toList().isEmpty());
    CHECK(ask(u"adminFields"_s, {u"store"_s}).toList().isEmpty());
    CHECK(ask(u"adminNewRecord"_s, {u"employees"_s}).toMap().isEmpty());
    CHECK(ask(u"report"_s, {u"sales"_s, 0}).toMap().value(u"rows"_s).toList().isEmpty());
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1234"_s));   // a manager
    CHECK_FALSE(ask(u"adminRecords"_s, {u"employees"_s}).toList().isEmpty());
    CHECK_FALSE(ask(u"report"_s, {u"sales"_s, 0}).toMap().value(u"title"_s).toString().contains(u"managers"_s));
}

TEST_CASE("Menu Builder: undo, a change at a time; not over a change made elsewhere", "[menubuilder][menuundo]")
{
    test::RecordingSink sink;
    PosService pos(test::seedPosData(true), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const auto menu0 = pos.shared()->menu;
    const auto groups0 = pos.shared()->settings.modifierGroups;
    CHECK(pos.menuUndoText().isEmpty());

    // A starter menu: many saves, one step.
    REQUIRE(pos.applyMenuTemplate(u"pizza"_s));
    CHECK(pos.menuUndoText() == u"Starter menu"_s);
    REQUIRE(pos.deleteMenuItemCard(u"classic-burger"_s));
    CHECK(pos.menuUndoText() == u"Remove an item"_s);
    // A save that changes nothing: no step.
    const QString name = QString::fromStdString(pos.shared()->menu.front().name);
    REQUIRE(pos.saveMenuItemCard({{u"id"_s, QString::fromStdString(pos.shared()->menu.front().id)}, {u"name"_s, name}}));
    CHECK(pos.menuUndoText() == u"Remove an item"_s);

    REQUIRE(pos.undoMenuChange());
    CHECK(pos.findItem(u"classic-burger"_s));          // back, and stored again
    CHECK(sink.savedMenuIds().contains(u"classic-burger"_s));
    CHECK(pos.menuUndoText() == u"Starter menu"_s);
    REQUIRE(pos.undoMenuChange());
    CHECK(pos.shared()->menu == menu0);                 // as it was, choice groups too
    CHECK(pos.shared()->settings.modifierGroups == groups0);
    CHECK_FALSE(pos.undoMenuChange());                  // nothing more

    // Changed elsewhere since (Manager -> Menu): not undone over it.
    REQUIRE(pos.saveCategory({{u"name"_s, u"Desserts"_s}}));
    pos.shared()->menu.front().price = vt::Money::fromCents(1);   // as another editor would
    CHECK_FALSE(pos.undoMenuChange());
    CHECK(pos.menuUndoText().isEmpty());
    CHECK(pos.shared()->menu.front().price.cents() == 1);

    // Only managers.
    REQUIRE(pos.saveCategory({{u"name"_s, u"Sides"_s}}));
    pos.logout();
    REQUIRE(pos.loginWithPin(u"2222"_s));
    CHECK_FALSE(pos.undoMenuChange());
}
