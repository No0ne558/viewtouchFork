// PosService: inventory - what each line uses up (recipes), stock taken
// when orders go to the kitchen and given back on a void, items sold out by
// themselves when their stock runs short, low-stock alerts, food cost.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include <QDateTime>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

// How much of a modifier's recipe a qualifier means: none of it for "No",
// twice for "Extra"/"Double", half for "Lite".
double qualifierFactor(Qualifier q)
{
    switch (q) {
    case Qualifier::No: return 0;
    case Qualifier::Extra:
    case Qualifier::Double: return 2;
    case Qualifier::Lite: return 0.5;
    default: return 1;
    }
}

QString amountText(double v)
{
    return QString::number(std::round(v * 100) / 100, 'g', 8);
}

} // namespace

Ingredient *PosShared::ingredient(const std::string &id)
{
    auto it = std::ranges::find_if(ingredients, [&](const Ingredient &i) { return i.id == id; });
    return it == ingredients.end() ? nullptr : &*it;
}

std::map<std::string, double> PosService::stockUse(const OrderLine &l) const
{
    std::map<std::string, double> use;
    if (l.isComment() || l.isGiftCard())
        return use;
    const auto add = [&](const MenuItem *m, double times) {
        if (!m)
            return;
        for (const RecipeLine &r : m->recipe)
            use[r.ingredientId] += r.quantity * times;
    };
    // By weight: the recipe is per unit (a pound of brisket), times the weight.
    const double qty = std::max(1, l.quantity) * (l.weight > 0 ? l.weight / 1000.0 : 1.0);
    add(findItem(qs(l.itemId)), qty * (l.qualifier == Qualifier::No ? 0 : 1));
    for (const Modifier &m : l.modifiers) {
        if (!m.itemId.empty())
            add(findItem(qs(m.itemId)), qty * qualifierFactor(m.qualifier));
    }
    return use;
}

void PosService::takeStock(const std::vector<OrderLine> &lines, int sign)
{
    std::map<std::string, double> total;
    for (const OrderLine &l : lines) {
        if (l.voided && sign > 0)
            continue;
        for (const auto &[id, q] : stockUse(l))
            total[id] += q;
    }
    if (total.empty())
        return;
    QStringList nowLow;
    for (int i = 0; i < int(s_->ingredients.size()); ++i) {
        Ingredient &g = s_->ingredients[i];
        const auto it = total.find(g.id);
        if (it == total.end() || it->second == 0)
            continue;
        const bool wasLow = g.low();
        g.onHand -= it->second * sign;
        if (!wasLow && g.low())
            nowLow << tr("%1 (%2 %3 left)").arg(qs(g.name), amountText(g.onHand), qs(g.unit));
        if (s_->sink)
            s_->sink->saveIngredient(g, i);
    }
    if (!nowLow.isEmpty())
        emit notice(tr("Running low: %1").arg(nowLow.join(u", "_s)));
    refreshSoldOut();
}

void PosService::refreshSoldOut()
{
    QStringList out, back;
    for (int i = 0; i < int(s_->menu.size()); ++i) {
        MenuItem &m = s_->menu[i];
        if (m.recipe.empty())
            continue;
        bool shortOf = false;
        for (const RecipeLine &r : m.recipe) {
            const Ingredient *g = s_->ingredient(r.ingredientId);
            if (g && r.quantity > 0 && g->onHand + 1e-9 < r.quantity)
                shortOf = true;
        }
        if (shortOf && m.available) {
            m.available = false;
            m.autoSoldOut = true;
            out << qs(m.name);
        } else if (!shortOf && m.autoSoldOut) {
            m.available = true;
            m.autoSoldOut = false;
            back << qs(m.name);
        } else {
            continue;
        }
        if (s_->sink)
            s_->sink->saveMenuItem(m, i);
    }
    if (out.isEmpty() && back.isEmpty())
        return;
    if (!out.isEmpty())
        emit notice(tr("Out of stock, now sold out: %1").arg(out.join(u", "_s)));
    if (!back.isEmpty())
        emit notice(tr("Back in stock: %1").arg(back.join(u", "_s)));
    ++s_->adminRevision;
    emit s_->adminChanged();
}

Report PosService::foodCostReport(const std::vector<Check> &closed, const ReportContext &ctx) const
{
    Report r;
    r.id = "foodcost";
    r.title = "Food Cost";
    r.subtitle = ctx.period;
    r.columns = {"Item", "Sold", "Sales", "Cost", "Cost %"};

    struct Row { std::string name; int sold = 0; Money sales, cost; };
    std::map<std::string, Row> rows;
    const auto costOf = [&](const OrderLine &l) {
        double cents = 0;
        for (const auto &[id, q] : stockUse(l))
            if (const Ingredient *g = const_cast<PosShared *>(s_)->ingredient(id))
                cents += q * double(g->cost.cents());
        return Money::fromCents(std::llround(cents));
    };
    for (const Check &c : closed) {
        for (const OrderLine &l : c.lines) {
            if (l.voided || l.isComment() || l.isGiftCard())
                continue;
            Row &row = rows[l.itemId];
            row.name = l.name;
            row.sold += l.quantity;
            row.sales += l.total();
            row.cost += costOf(l);
        }
    }
    const auto percent = [](Money part, Money whole) {
        return whole.cents() > 0 ? QString::number(100.0 * double(part.cents()) / double(whole.cents()), 'f', 1).toStdString() + "%"
                                 : std::string("-");
    };
    std::vector<const Row *> sorted;
    for (const auto &[id, row] : rows)
        sorted.push_back(&row);
    std::ranges::sort(sorted, [](const Row *a, const Row *b) { return a->sales > b->sales; });
    Money sales, cost;
    if (sorted.empty())
        r.note("Nothing sold.");
    else
        r.section("Sold");
    for (const Row *row : sorted) {
        r.line({row->name, std::to_string(row->sold), ctx.money(row->sales),
                row->cost.cents() ? ctx.money(row->cost) : "-", row->cost.cents() ? percent(row->cost, row->sales) : "-"});
        sales += row->sales;
        cost += row->cost;
    }
    if (!sorted.empty())
        r.total({"All", "", ctx.money(sales), ctx.money(cost), percent(cost, sales)});

    r.section("Stock");
    bool any = false;
    for (const Ingredient &g : s_->ingredients) {
        any = true;
        r.line({g.name + (g.onHand <= 0 ? "  (out)" : g.low() ? "  (low)" : ""), "",
                amountText(g.onHand).toStdString() + " " + g.unit, "", ""});
    }
    if (!any)
        r.note("No ingredients yet (Manager -> Inventory).");
    return r;
}

QVariantList PosService::lowStock() const
{
    QVariantList out;
    for (const Ingredient &g : s_->ingredients) {
        if (g.low())
            out.append(QVariantMap{{u"name"_s, qs(g.name)}, {u"onHand"_s, amountText(g.onHand)},
                                   {u"unit"_s, qs(g.unit)}, {u"out"_s, g.onHand <= 0}});
    }
    return out;
}

// --- deliveries ------------------------------------------------------------------------

bool PosService::receiveDelivery(const QVariantMap &r)
{
    if (!require(perm::Manager, tr("Receiving deliveries")))
        return false;
    Delivery d;
    const QString vendorId = r.value(u"vendor"_s).toString();
    if (!vendorId.isEmpty()) {
        const auto v = std::ranges::find_if(s_->settings.vendors, [&](const Vendor &x) { return qs(x.id) == vendorId; });
        if (v == s_->settings.vendors.end())
            return fail(tr("There is no vendor '%1'.").arg(vendorId));
        d.vendorId = v->id;
        d.vendorName = v->name;
    }
    d.invoice = ss(r.value(u"invoice"_s).toString().trimmed());
    for (const QVariant &v : r.value(u"lines"_s).toList()) {
        const QVariantMap l = v.toMap();
        const double qty = l.value(u"qty"_s).toDouble();
        if (qty == 0)
            continue;
        const auto g = std::ranges::find_if(s_->ingredients, [&](const Ingredient &x) { return qs(x.id) == l.value(u"ingredient"_s).toString(); });
        if (g == s_->ingredients.end())
            return fail(tr("There is no ingredient '%1' (see Manager → Inventory).").arg(l.value(u"ingredient"_s).toString()));
        if (qty < 0)
            return fail(tr("Amounts received can't be negative: %1.").arg(qs(g->name)));
        const double cost = l.contains(u"cost"_s) ? l.value(u"cost"_s).toDouble() : double(g->cost.cents()) / 100.0;
        if (cost < 0)
            return fail(tr("Costs can't be negative: %1.").arg(qs(g->name)));
        d.lines.push_back({g->id, g->name, g->unit, qty, Money::fromCents(std::llround(cost * 100))});
    }
    if (d.lines.empty())
        return fail(tr("Type how much came of each item."));
    // On the shelf now, at the latest cost.
    for (const Delivery::Line &l : d.lines) {
        const auto g = std::ranges::find_if(s_->ingredients, [&](const Ingredient &x) { return x.id == l.ingredientId; });
        g->onHand += l.qty;
        if (l.unitCost.cents() > 0)
            g->cost = l.unitCost;
        if (s_->sink)
            s_->sink->saveIngredient(*g, int(g - s_->ingredients.begin()));
    }
    d.id = ++s_->lastDeliveryId;
    d.at = now();
    d.by = user()->name;
    s_->deliveries.push_back(d);
    if (s_->sink)
        s_->sink->saveDelivery(d);
    refreshSoldOut();   // what was out is back
    ++s_->adminRevision;
    emit s_->adminChanged();
    emit notice(d.vendorName.empty()
                    ? tr("Received %n item(s): %1", "", int(d.lines.size())).arg(format(d.total()))
                    : tr("Received %n item(s) from %1: %2", "", int(d.lines.size())).arg(qs(d.vendorName), format(d.total())));
    return true;
}

QVariantMap PosService::receiving() const
{
    if (!can(u"manager"_s))
        return {};
    QVariantList vendors;
    for (const Vendor &v : s_->settings.vendors)
        vendors.append(QVariantMap{{u"id"_s, qs(v.id)}, {u"name"_s, qs(v.name)}});
    QVariantList ingredients;
    for (const Ingredient &g : s_->ingredients)
        ingredients.append(QVariantMap{{u"id"_s, qs(g.id)}, {u"name"_s, qs(g.name)}, {u"unit"_s, qs(g.unit)},
                                       {u"onHand"_s, g.onHand}, {u"cost"_s, double(g.cost.cents()) / 100.0},
                                       {u"vendor"_s, qs(g.vendor)}, {u"low"_s, g.low()}});
    QVariantList recent;
    for (auto it = s_->deliveries.rbegin(); it != s_->deliveries.rend() && recent.size() < 15; ++it)
        recent.append(QVariantMap{{u"when"_s, QDateTime::fromMSecsSinceEpoch(it->at).toString(u"ddd M/d h:mm AP"_s)},
                                  {u"vendor"_s, qs(it->vendorName)}, {u"invoice"_s, qs(it->invoice)},
                                  {u"items"_s, int(it->lines.size())}, {u"total"_s, format(it->total())},
                                  {u"by"_s, qs(it->by)}});
    return {{u"vendors"_s, vendors}, {u"ingredients"_s, ingredients}, {u"recent"_s, recent}};
}

Report PosService::purchasesReport(const ReportContext &ctx) const
{
    Report r;
    r.id = "purchases";
    r.title = "Purchases";
    r.subtitle = ctx.period;
    r.columns = {"", "Items", "Invoice", "Total"};
    std::vector<const Delivery *> today;
    for (const Delivery &d : s_->deliveries)
        if (d.at >= s_->day.openedAt)
            today.push_back(&d);
    if (today.empty()) {
        r.note("No deliveries received today (Manager -> Inventory -> Receive a Delivery).");
        return r;
    }
    std::map<std::string, Money> byVendor;
    Money total;
    for (const Delivery *d : today) {
        byVendor[d->vendorName.empty() ? "(no vendor)" : d->vendorName] += d->total();
        total += d->total();
    }
    r.section("By vendor");
    for (const auto &[vendor, amount] : byVendor)
        r.line({vendor, "", "", ctx.money(amount)});
    r.total({"Total received", "", "", ctx.money(total)});
    for (const Delivery *d : today) {
        r.section((d->vendorName.empty() ? std::string("Delivery") : d->vendorName) + ", " + ctx.clock(d->at) + ", " + d->by);
        for (const Delivery::Line &l : d->lines) {
            char qty[32];
            std::snprintf(qty, sizeof qty, "%g %s", l.qty, l.unit.c_str());
            r.line({l.name, qty, ctx.money(l.unitCost) + " each", ctx.money(l.total())});
        }
        r.total({"Total", "", d->invoice.empty() ? "" : "#" + d->invoice, ctx.money(d->total())});
    }
    return r;
}

} // namespace vt::app
