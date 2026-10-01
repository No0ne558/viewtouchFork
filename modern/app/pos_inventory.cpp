// PosService: inventory - what each line uses up (recipes), stock taken
// when orders go to the kitchen and given back on a void, items sold out by
// themselves when their stock runs short, low-stock alerts, food cost.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <algorithm>
#include <cmath>

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
    const double qty = std::max(1, l.quantity);
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

} // namespace vt::app
