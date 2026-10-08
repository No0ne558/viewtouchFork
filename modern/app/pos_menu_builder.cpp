// PosService: the Menu Builder (Manager -> Menu Builder). Categories (their
// order, color, meal periods and what new items in them start with), an
// item's card (name, price, category, photo, choices, what's on it), and
// what's on an item: its own group of ingredients that come on it, so No,
// Lite, Extra and on the Side work for it everywhere.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

QString slugOf(const QString &text)
{
    static const QRegularExpression nonWord(u"[^a-z0-9]+"_s);
    QString s = text.toLower().normalized(QString::NormalizationForm_KD);
    s.replace(nonWord, u"-"_s);
    while (s.startsWith(u'-'))
        s.remove(0, 1);
    while (s.endsWith(u'-'))
        s.chop(1);
    return s.isEmpty() ? u"item"_s : s;
}

template <typename Taken>
std::string freeId(const QString &wanted, Taken taken)
{
    QString id = slugOf(wanted);
    for (int n = 2; taken(id.toStdString()); ++n)
        id = slugOf(wanted) + u'-' + QString::number(n);
    return id.toStdString();
}

// What's on an item: its own group.
std::string onItGroupId(const std::string &itemId)
{
    return "on-" + itemId;
}

// A list, or typed: "lettuce, tomato, onion".
QStringList strings(const QVariant &v)
{
    QStringList out;
    if (v.typeId() == QMetaType::QString) {
        for (const QString &s : v.toString().split(u',', Qt::SkipEmptyParts))
            if (!s.trimmed().isEmpty())
                out << s.trimmed();
        return out;
    }
    for (const QVariant &x : v.toList())
        if (const QString s = x.toString().trimmed(); !s.isEmpty())
            out << s;
    return out;
}

} // namespace

void PosService::menuChanged()
{
    refreshSoldOut();
    ++s_->adminRevision;
    emit s_->adminChanged();
}

// --- categories ----------------------------------------------------------------------

bool PosService::saveCategory(const QVariantMap &record)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return false;
    const QString name = record.value(u"name"_s).toString().trimmed();
    if (name.isEmpty())
        return fail(tr("The category needs a name."));
    // The categories as shown (families not set up yet become set up now).
    std::vector<MenuCategory> list = s_->categories();
    const std::string id = ss(record.value(u"id"_s).toString());
    auto it = std::ranges::find_if(list, [&](const MenuCategory &c) { return !id.empty() && c.id == id; });
    for (const MenuCategory &c : list)
        if (c.id != id && QString::compare(qs(c.name), name, Qt::CaseInsensitive) == 0)
            return fail(tr("There's already a category called %1.").arg(name));
    MenuCategory c = it != list.end() ? *it : MenuCategory{};
    c.name = ss(name);
    if (c.id.empty())
        c.id = freeId(name, [&](const std::string &x) {
            return std::ranges::any_of(list, [&](const MenuCategory &o) { return o.id == x; })
                   || std::ranges::any_of(s_->menu, [&](const MenuItem &m) { return m.family == x; });
        });
    if (record.contains(u"color"_s))
        c.color = ss(record.value(u"color"_s).toString());
    if (record.contains(u"periods"_s)) {
        c.periods.clear();
        for (const QString &p : strings(record.value(u"periods"_s))) {
            if (std::ranges::none_of(s_->settings.mealPeriods, [&](const MealPeriod &m) { return qs(m.id) == p; }))
                return fail(tr("There is no meal period '%1'.").arg(p));
            c.periods.push_back(ss(p));
        }
        // Every meal period: all day.
        if (c.periods.size() == s_->settings.mealPeriods.size())
            c.periods.clear();
    }
    if (record.contains(u"printer"_s))
        c.printer = ss(record.value(u"printer"_s).toString());
    if (record.contains(u"station"_s))
        c.station = ss(record.value(u"station"_s).toString());
    if (record.contains(u"taxClass"_s))
        c.taxClass = ss(record.value(u"taxClass"_s).toString());
    if (it != list.end())
        *it = c;
    else
        list.push_back(c);
    s_->settings.menuCategories = list;
    settingsChanged();
    menuChanged();
    emit notice(tr("Saved"));
    return true;
}

bool PosService::moveCategory(const QString &id, int by)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return false;
    std::vector<MenuCategory> list = s_->categories();
    const auto it = std::ranges::find_if(list, [&](const MenuCategory &c) { return qs(c.id) == id; });
    if (it == list.end())
        return fail(tr("That category is gone."));
    const int from = int(it - list.begin());
    const int to = std::clamp(from + (by > 0 ? 1 : -1), 0, int(list.size()) - 1);
    if (to == from)
        return true;
    std::swap(list[from], list[to]);
    s_->settings.menuCategories = list;
    settingsChanged();
    menuChanged();
    return true;
}

bool PosService::moveCategoryTo(const QString &id, int position)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return false;
    std::vector<MenuCategory> list = s_->categories();
    const auto it = std::ranges::find_if(list, [&](const MenuCategory &c) { return qs(c.id) == id; });
    if (it == list.end())
        return fail(tr("That category is gone."));
    MenuCategory c = *it;
    list.erase(it);
    list.insert(list.begin() + std::clamp(position, 0, int(list.size())), c);
    s_->settings.menuCategories = list;
    settingsChanged();
    menuChanged();
    return true;
}

// Its place among its category's items (dragged in the Menu Builder): the
// category's items trade the places they have in the menu.
bool PosService::moveMenuItemTo(const QString &id, int position)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return false;
    auto &menu = s_->menu;
    const auto it = std::ranges::find_if(menu, [&](const MenuItem &m) { return qs(m.id) == id; });
    if (it == menu.end())
        return fail(tr("'%1' is not on the menu.").arg(id));
    const std::string family = it->family;
    std::vector<int> places;
    std::vector<MenuItem> items;
    for (int i = 0; i < int(menu.size()); ++i)
        if (menu[i].family == family && !menu[i].isModifier) {
            places.push_back(i);
            items.push_back(menu[i]);
        }
    const auto from = std::ranges::find_if(items, [&](const MenuItem &m) { return qs(m.id) == id; });
    MenuItem moved = *from;
    items.erase(from);
    items.insert(items.begin() + std::clamp(position, 0, int(items.size())), moved);
    for (std::size_t k = 0; k < places.size(); ++k) {
        if (menu[places[k]].id == items[k].id)
            continue;
        menu[places[k]] = items[k];
        if (s_->sink)
            s_->sink->saveMenuItem(menu[places[k]], places[k]);
    }
    menuChanged();
    return true;
}

bool PosService::deleteCategory(const QString &id)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return false;
    const auto items = std::ranges::count_if(s_->menu, [&](const MenuItem &m) { return qs(m.family) == id && !m.isModifier; });
    if (items > 0)
        return fail(tr("Move or remove its %n item(s) first.", nullptr, int(items)));
    std::vector<MenuCategory> list = s_->categories();
    std::erase_if(list, [&](const MenuCategory &c) { return qs(c.id) == id; });
    s_->settings.menuCategories = list;
    settingsChanged();
    menuChanged();
    emit notice(tr("Category removed"));
    return true;
}

// --- choice groups ---------------------------------------------------------------------

bool PosService::saveChoiceGroup(const QVariantMap &record)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return false;
    const QString name = record.value(u"name"_s).toString().trimmed();
    if (name.isEmpty())
        return fail(tr("The choice group needs a name."));
    auto &list = s_->settings.modifierGroups;
    const std::string id = ss(record.value(u"id"_s).toString());
    auto it = std::ranges::find_if(list, [&](const ModifierGroup &g) { return !id.empty() && g.id == id; });
    ModifierGroup g = it != list.end() ? *it : ModifierGroup{};
    g.name = ss(name);
    g.min = std::max(0, record.value(u"min"_s).toInt());
    g.max = std::max(0, record.value(u"max"_s).toInt());
    if (g.max > 0 && g.min > g.max)
        return fail(tr("It can't require more choices than it allows."));
    g.askHow = record.value(u"askHow"_s).toBool();
    std::vector<ModifierOption> options;
    for (const QVariant &v : record.value(u"options"_s).toList()) {
        const QVariantMap o = v.toMap();
        const QString optName = o.value(u"name"_s).toString().trimmed();
        if (optName.isEmpty())
            continue;   // a row left empty
        bool ok = true;
        const QString priceText = o.value(u"price"_s).toString().remove(qs(s_->settings.currencySymbol)).trimmed();
        const double price = priceText.isEmpty() ? 0 : priceText.toDouble(&ok);
        if (!ok || price < 0)
            return fail(tr("%1: type its price, like 1.50 (or leave it empty).").arg(optName));
        if (std::ranges::any_of(options, [&](const ModifierOption &x) { return QString::compare(qs(x.name), optName, Qt::CaseInsensitive) == 0; }))
            return fail(tr("%1 is in it twice.").arg(optName));
        // Kept: what the Menu Builder doesn't show (a combo's menu item).
        ModifierOption opt;
        if (it != list.end())
            if (const auto old = std::ranges::find_if(it->options, [&](const ModifierOption &x) { return qs(x.name) == optName; });
                old != it->options.end())
                opt = *old;
        opt.name = ss(optName);
        opt.price = Money::fromCents(std::llround(price * 100.0));
        opt.included = o.value(u"included"_s).toBool();
        if (o.contains(u"kitchenName"_s))
            opt.kitchenName = ss(o.value(u"kitchenName"_s).toString().trimmed());
        options.push_back(std::move(opt));
    }
    if (options.empty())
        return fail(tr("Add its options."));
    if (g.min > int(options.size()))
        return fail(tr("It requires more choices than it has options."));
    g.options = std::move(options);
    if (it != list.end()) {
        *it = g;
    } else {
        g.id = freeId(name, [&](const std::string &x) {
            return std::ranges::any_of(list, [&](const ModifierGroup &o) { return o.id == x; });
        });
        list.push_back(g);
    }
    settingsChanged();
    menuChanged();
    emit notice(tr("Saved"));
    return true;
}

bool PosService::deleteChoiceGroup(const QString &id)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return false;
    auto &list = s_->settings.modifierGroups;
    if (std::erase_if(list, [&](const ModifierGroup &g) { return qs(g.id) == id; }) == 0)
        return fail(tr("That choice group is gone."));
    // Items stop asking for it.
    for (int i = 0; i < int(s_->menu.size()); ++i) {
        MenuItem &m = s_->menu[i];
        if (std::erase(m.modifierGroups, ss(id)) > 0 && s_->sink)
            s_->sink->saveMenuItem(m, i);
    }
    settingsChanged();
    menuChanged();
    emit notice(tr("Choice group removed"));
    return true;
}

// --- an item's card ------------------------------------------------------------------

bool PosService::saveMenuItemCard(const QVariantMap &card)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return false;
    const QString name = card.value(u"name"_s).toString().trimmed();
    if (name.isEmpty())
        return fail(tr("The item needs a name."));
    bool priced = false;
    const double price = card.value(u"price"_s).toString().remove(qs(s_->settings.currencySymbol)).trimmed().toDouble(&priced);
    if (card.contains(u"price"_s) && (!priced || price < 0))
        return fail(tr("Type the price, like 11.50."));
    const std::string id = ss(card.value(u"id"_s).toString());
    auto &menu = s_->menu;
    auto it = std::ranges::find_if(menu, [&](const MenuItem &m) { return !id.empty() && m.id == id; });
    const bool adding = it == menu.end();
    for (const MenuItem &m : menu)
        if (m.id != id && !m.isModifier && QString::compare(qs(m.name), name, Qt::CaseInsensitive) == 0)
            return fail(tr("%1 is already on the menu.").arg(name));
    const std::string family = card.contains(u"family"_s) ? ss(card.value(u"family"_s).toString())
                                                         : (adding ? std::string() : it->family);
    if (family.empty())
        return fail(tr("Choose its category."));
    const std::vector<MenuCategory> categories = s_->categories();
    const auto category = std::ranges::find_if(categories, [&](const MenuCategory &c) { return c.id == family; });
    if (category == categories.end())
        return fail(tr("There is no category '%1'.").arg(qs(family)));

    MenuItem item = adding ? MenuItem{} : *it;
    if (adding) {
        // What its category's items start with.
        item.id = freeId(name, [&](const std::string &x) {
            return std::ranges::any_of(menu, [&](const MenuItem &m) { return m.id == x; });
        });
        item.printer = category->printer.empty() ? "kitchen" : category->printer;
        item.station = category->station;
        item.taxClass = taxClassFromString(category->taxClass.empty() ? "food" : category->taxClass);
        item.available = true;
    } else if (item.family != family) {
        // Moved to another category: its kitchen and tax come along only if
        // they were the old category's.
        const auto old = std::ranges::find_if(categories, [&](const MenuCategory &c) { return c.id == item.family; });
        if (old != categories.end()) {
            if (item.printer == (old->printer.empty() ? "kitchen" : old->printer) && !category->printer.empty())
                item.printer = category->printer;
            if (item.station == old->station)
                item.station = category->station;
        }
    }
    item.name = ss(name);
    item.family = family;
    if (card.contains(u"price"_s))
        item.price = Money::fromCents(std::llround(price * 100.0));
    if (card.contains(u"image"_s))
        item.image = ss(card.value(u"image"_s).toString());
    if (card.contains(u"description"_s))
        item.description = ss(card.value(u"description"_s).toString().trimmed());
    if (card.contains(u"available"_s))
        item.available = card.value(u"available"_s).toBool();
    if (card.contains(u"kioskHide"_s))
        item.kioskHide = card.value(u"kioskHide"_s).toBool();
    if (card.contains(u"favorite"_s))
        item.favorite = card.value(u"favorite"_s).toBool();
    if (card.contains(u"taxClass"_s))
        item.taxClass = taxClassFromString(ss(card.value(u"taxClass"_s).toString()));
    if (card.contains(u"printer"_s))
        item.printer = ss(card.value(u"printer"_s).toString());
    if (card.contains(u"station"_s))
        item.station = ss(card.value(u"station"_s).toString());

    // Its choices: groups picked from the list (its own What's on it first).
    const std::string onIt = onItGroupId(item.id);
    if (card.contains(u"groups"_s)) {
        std::vector<std::string> groups;
        for (const QString &g : strings(card.value(u"groups"_s))) {
            if (!s_->settings.modifierGroup(ss(g)))
                return fail(tr("There is no choice group '%1'.").arg(g));
            if (ss(g) != onIt)
                groups.push_back(ss(g));
        }
        const bool hadOnIt = std::ranges::find(item.modifierGroups, onIt) != item.modifierGroups.end();
        item.modifierGroups = groups;
        if (hadOnIt)
            item.modifierGroups.insert(item.modifierGroups.begin(), onIt);
    }
    // What's on it: "lettuce, tomato, onion" -> its own group, each one that
    // comes on it (No, Lite, Extra, on the Side).
    if (card.contains(u"onIt"_s)) {
        const QStringList ingredients = strings(card.value(u"onIt"_s));
        auto &groups = s_->settings.modifierGroups;
        std::erase_if(groups, [&](const ModifierGroup &g) { return g.id == onIt; });
        std::erase(item.modifierGroups, onIt);
        if (!ingredients.isEmpty()) {
            ModifierGroup g;
            g.id = onIt;
            g.name = ss(tr("What's on it"));
            g.min = 0;
            g.max = 0;
            g.askHow = true;
            for (const QString &i : ingredients) {
                ModifierOption o;
                o.name = ss(i);
                o.included = true;
                g.options.push_back(std::move(o));
            }
            groups.push_back(std::move(g));
            item.modifierGroups.insert(item.modifierGroups.begin(), onIt);
        }
        settingsChanged();
    }

    if (adding) {
        // After the last item of its category, so it shows at its end.
        auto at = menu.end();
        for (auto i = menu.begin(); i != menu.end(); ++i)
            if (i->family == family)
                at = i + 1;
        it = menu.insert(at, item);
        if (s_->sink)
            for (int i = int(it - menu.begin()); i < int(menu.size()); ++i)
                s_->sink->saveMenuItem(menu[i], i);
    } else {
        *it = item;
        if (s_->sink)
            s_->sink->saveMenuItem(*it, int(it - menu.begin()));
    }
    menuChanged();
    emit notice(adding ? tr("%1 added").arg(name) : tr("Saved"));
    return true;
}

bool PosService::duplicateMenuItem(const QString &id)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return false;
    auto &menu = s_->menu;
    const auto it = std::ranges::find_if(menu, [&](const MenuItem &m) { return qs(m.id) == id; });
    if (it == menu.end())
        return fail(tr("'%1' is not on the menu.").arg(id));
    MenuItem copy = *it;
    // "Fish Tacos 2": a name of its own, to change.
    QString name;
    for (int n = 2;; ++n) {
        name = u"%1 %2"_s.arg(qs(it->name)).arg(n);
        if (std::ranges::none_of(menu, [&](const MenuItem &m) { return QString::compare(qs(m.name), name, Qt::CaseInsensitive) == 0; }))
            break;
    }
    copy.name = ss(name);
    copy.number.clear();   // numbers are one item's
    copy.ticketsSoldBefore = 0;
    copy.id = freeId(name, [&](const std::string &x) { return std::ranges::any_of(menu, [&](const MenuItem &m) { return m.id == x; }); });
    // Its own What's on it, copied too.
    const std::string oldOnIt = onItGroupId(it->id), newOnIt = onItGroupId(copy.id);
    for (std::string &g : copy.modifierGroups)
        if (g == oldOnIt)
            g = newOnIt;
    if (const ModifierGroup *g = s_->settings.modifierGroup(oldOnIt)) {
        ModifierGroup own = *g;
        own.id = newOnIt;
        s_->settings.modifierGroups.push_back(std::move(own));
        settingsChanged();
    }
    const int at = int(it - menu.begin()) + 1;
    menu.insert(menu.begin() + at, copy);
    if (s_->sink)
        for (int i = at; i < int(menu.size()); ++i)
            s_->sink->saveMenuItem(menu[i], i);
    menuChanged();
    emit notice(tr("%1 added: change its name and price").arg(name));
    return true;
}

// "Tacos: Carne Asada 3.50, Al Pastor 3.25" or one per line ("Horchata 2.75"):
// before a colon, the category (made if there's none); each item's price last.
int PosService::addMenuItemsFromText(const QString &categoryId, const QString &text)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return 0;
    static const QRegularExpression priced(uR"(^(.*?)[\s\-–:]*\$?\s*(\d+(?:[.,]\d{1,2})?)\s*$)"_s);
    QString category = categoryId;
    struct Entry { QString category, name; double price; };
    std::vector<Entry> entries;
    QStringList problems;
    for (QString line : text.split(u'\n')) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        // "Tacos:" (or "Tacos: ..."): what follows goes in it.
        if (const qsizetype colon = line.indexOf(u':'); colon > 0 && !line.left(colon).contains(QRegularExpression(u"\\d"_s))) {
            const QString name = line.left(colon).trimmed();
            line = line.mid(colon + 1).trimmed();
            const auto cats = s_->categories();
            const auto c = std::ranges::find_if(cats, [&](const MenuCategory &x) {
                return QString::compare(qs(x.name), name, Qt::CaseInsensitive) == 0 || qs(x.id) == name;
            });
            if (c != cats.end()) {
                category = qs(c->id);
            } else {
                if (!saveCategory({{u"name"_s, name}}))
                    return 0;
                for (const MenuCategory &x : s_->categories())
                    if (qs(x.name) == name)
                        category = qs(x.id);
            }
            if (line.isEmpty())
                continue;
        }
        // Commas between items; "4,25" is a price (a comma then cents).
        static const QRegularExpression between(uR"(,(?!\d{1,2}\s*(?:,|$)))"_s);
        for (QString part : line.split(between, Qt::SkipEmptyParts)) {
            part = part.trimmed();
            if (part.isEmpty())
                continue;
            const auto m = priced.match(part);
            if (!m.hasMatch() || m.captured(1).trimmed().isEmpty()) {
                problems << part;
                continue;
            }
            entries.push_back({category, m.captured(1).trimmed(), m.captured(2).replace(u',', u'.').toDouble()});
        }
    }
    if (!problems.isEmpty()) {
        fail(tr("Give each its price, like \"Al Pastor 3.25\": %1").arg(problems.join(u", "_s)));
        return 0;
    }
    if (category.isEmpty() && !entries.empty() && entries.front().category.isEmpty()) {
        fail(tr("Choose a category first, or start with one: \"Tacos: Carne Asada 3.50, ...\"."));
        return 0;
    }
    int added = 0;
    QStringList skipped;
    for (const Entry &e : entries) {
        if (std::ranges::any_of(s_->menu, [&](const MenuItem &x) { return QString::compare(qs(x.name), e.name, Qt::CaseInsensitive) == 0; })) {
            skipped << e.name;
            continue;
        }
        if (!saveMenuItemCard({{u"name"_s, e.name}, {u"price"_s, QString::number(e.price, 'f', 2)}, {u"family"_s, e.category}}))
            return added;
        ++added;
    }
    emit notice(skipped.isEmpty() ? tr("Added %n item(s)", nullptr, added)
                                  : tr("Added %n item(s); already on the menu: %1", nullptr, added).arg(skipped.join(u", "_s)));
    return added;
}

// Rows read from a spreadsheet (LayoutController::readMenuFile): each in
// its category (made if new; none named: `categoryId`); an item already on
// the menu is skipped, or gets the new price with `updatePrices`.
int PosService::importMenuRows(const QVariantList &rows, const QString &categoryId, bool updatePrices)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return 0;
    int added = 0, updated = 0;
    QStringList skipped;
    for (const QVariant &v : rows) {
        const QVariantMap r = v.toMap();
        const QString name = r.value(u"name"_s).toString().trimmed();
        const double price = r.value(u"price"_s).toDouble();
        QString category = categoryId;
        if (const QString wanted = r.value(u"category"_s).toString().trimmed(); !wanted.isEmpty()) {
            const auto cats = s_->categories();
            const auto c = std::ranges::find_if(cats, [&](const MenuCategory &x) {
                return QString::compare(qs(x.name), wanted, Qt::CaseInsensitive) == 0 || qs(x.id) == wanted;
            });
            if (c == cats.end()) {
                if (!saveCategory({{u"name"_s, wanted}}))
                    return added;
                for (const MenuCategory &x : s_->categories())
                    if (qs(x.name) == wanted)
                        category = qs(x.id);
            } else {
                category = qs(c->id);
            }
        }
        if (category.isEmpty()) {
            fail(tr("%1 has no category: choose one, or add a Category column.").arg(name));
            return added;
        }
        auto existing = std::ranges::find_if(s_->menu, [&](const MenuItem &m) {
            return !m.isModifier && QString::compare(qs(m.name), name, Qt::CaseInsensitive) == 0;
        });
        if (existing != s_->menu.end()) {
            if (!updatePrices) {
                skipped << name;
                continue;
            }
            if (!saveMenuItemCard({{u"id"_s, qs(existing->id)}, {u"name"_s, qs(existing->name)},
                                   {u"price"_s, QString::number(price, 'f', 2)}}))
                return added;
            ++updated;
            continue;
        }
        QVariantMap card{{u"name"_s, name}, {u"price"_s, QString::number(price, 'f', 2)}, {u"family"_s, category}};
        if (const QString d = r.value(u"description"_s).toString().trimmed(); !d.isEmpty())
            card.insert(u"description"_s, d);
        if (const QString on = r.value(u"onIt"_s).toString().trimmed(); !on.isEmpty())
            card.insert(u"onIt"_s, on);
        if (!saveMenuItemCard(card))
            return added;
        ++added;
    }
    QString what = tr("Added %n item(s)", nullptr, added);
    if (updated)
        what += u"; "_s + tr("new prices for %n", nullptr, updated);
    if (!skipped.isEmpty())
        what += u"; "_s + tr("already on the menu: %1").arg(skipped.join(u", "_s));
    emit notice(what);
    return added + updated;
}

bool PosService::deleteMenuItemCard(const QString &id)
{
    if (!require(perm::Manager, tr("Changing the menu")))
        return false;
    auto &menu = s_->menu;
    const auto it = std::ranges::find_if(menu, [&](const MenuItem &m) { return qs(m.id) == id; });
    if (it == menu.end())
        return fail(tr("'%1' is not on the menu.").arg(id));
    const QString name = qs(it->name);
    const std::string onIt = onItGroupId(it->id);
    menu.erase(it);
    if (s_->sink)
        s_->sink->deleteMenuItem(ss(id));
    if (std::erase_if(s_->settings.modifierGroups, [&](const ModifierGroup &g) { return g.id == onIt; }) > 0)
        settingsChanged();
    menuChanged();
    emit notice(tr("%1 removed from the menu").arg(name));
    return true;
}

} // namespace vt::app
