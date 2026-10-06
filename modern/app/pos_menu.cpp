// PosService: the menu while ordering - modifier groups (choose Temperature,
// Toppings...), prices that change with the meal period, and 86'ing.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

std::string PosService::currentMealPeriod() const
{
    const QTime t = QDateTime::fromMSecsSinceEpoch(now()).time();
    return mealPeriodAt(s_->settings.mealPeriods, t.hour() * 60 + t.minute());
}

// --- choosing modifiers ---------------------------------------------------------------------

namespace {
int chosenIn(const OrderLine &l, const std::string &group)
{
    return int(std::ranges::count_if(l.modifiers, [&](const Modifier &m) { return m.group == group; }));
}
} // namespace

QVariantMap PosService::choosingInfo() const
{
    const Check *c = currentCheck();
    const OrderLine *l = c ? c->line(choosingLine_) : nullptr;
    if (!l)
        return {{u"active"_s, false}};
    const MenuItem *item = findItem(qs(l->itemId));
    QVariantList groups;
    for (const std::string &gid : item ? item->modifierGroups : std::vector<std::string>{}) {
        const ModifierGroup *g = s_->settings.modifierGroup(gid);
        if (!g)
            continue;
        QVariantList options;
        for (int i = 0; i < int(g->options.size()); ++i) {
            const ModifierOption &o = g->options[i];
            const bool chosen = std::ranges::any_of(l->modifiers, [&](const Modifier &m) {
                return m.group == g->id && m.name == o.name;
            });
            const MenuItem *linked = o.itemId.empty() ? nullptr : findItem(qs(o.itemId));
            options.append(QVariantMap{{u"index"_s, i}, {u"name"_s, qs(o.name)},
                                       {u"price"_s, o.price.cents() ? format(o.price) : QString()},
                                       {u"chosen"_s, chosen}, {u"soldOut"_s, linked && !linked->available}});
        }
        const int n = chosenIn(*l, g->id);
        const QString rule = g->min == 1 && g->max == 1 ? tr("Choose 1")
                             : g->min > 0               ? tr("Choose at least %1").arg(g->min)
                             : g->max == 1              ? tr("Optional")
                             : g->max > 1               ? tr("Up to %1").arg(g->max)
                                                        : tr("Any");
        groups.append(QVariantMap{{u"id"_s, qs(g->id)}, {u"name"_s, qs(g->name)}, {u"rule"_s, rule},
                                  {u"chosen"_s, n}, {u"done"_s, n >= g->min}, {u"options"_s, options}});
    }
    return {{u"active"_s, true}, {u"lineId"_s, qint64(l->id)}, {u"item"_s, qs(l->displayName())},
            {u"groups"_s, groups}};
}

QString PosService::missingChoice(const std::vector<OrderLine> &lines) const
{
    for (const OrderLine &l : lines) {
        const MenuItem *item = l.isComment() || l.voided ? nullptr : findItem(qs(l.itemId));
        if (!item)
            continue;
        for (const std::string &gid : item->modifierGroups) {
            const ModifierGroup *g = s_->settings.modifierGroup(gid);
            if (g && g->min > 0 && chosenIn(l, g->id) < g->min)
                return tr("%1 needs a %2. Touch it, then choose.").arg(qs(l.displayName()), qs(g->name));
        }
    }
    return {};
}

bool PosService::chooseLine(qint64 lineId)
{
    Check *c = current();
    const OrderLine *l = c ? c->line(lineId) : nullptr;
    if (!l || l->sent)
        return fail(tr("Only items not yet sent can be changed."));
    const MenuItem *item = findItem(qs(l->itemId));
    if (!item || item->modifierGroups.empty())
        return fail(tr("%1 has no choices.").arg(qs(l->displayName())));
    choosingLine_ = l->id;
    selectedLine_ = l->id;
    emit checkChanged();
    return true;
}

bool PosService::chooseOption(const QString &groupId, int index)
{
    Check *c = current();
    OrderLine *l = c ? c->line(choosingLine_) : nullptr;
    if (!l)
        return fail(tr("Nothing to choose for."));
    const ModifierGroup *g = s_->settings.modifierGroup(ss(groupId));
    if (!g || index < 0 || index >= int(g->options.size()))
        return false;
    const ModifierOption &o = g->options[index];
    const MenuItem *linked = o.itemId.empty() ? nullptr : findItem(qs(o.itemId));
    // Touching a chosen option takes it off.
    auto same = [&](const Modifier &m) { return m.group == g->id && m.name == o.name; };
    if (std::ranges::any_of(l->modifiers, same)) {
        std::erase_if(l->modifiers, same);
    } else {
        if (g->max == 1)   // one choice: the new one replaces it
            std::erase_if(l->modifiers, [&](const Modifier &m) { return m.group == g->id; });
        else if (g->max > 1 && chosenIn(*l, g->id) >= g->max)
            return fail(tr("%1: up to %2.").arg(qs(g->name)).arg(g->max));
        if (linked && !linked->available)
            return fail(tr("%1 is sold out.").arg(qs(o.name)));
        Modifier m;
        m.itemId = o.itemId;   // its stock (a combo's side or drink)
        if (linked)
            m.station = linked->station;   // made at the fryer, say
        m.name = o.name;
        m.unitPrice = o.price;
        m.group = g->id;
        m.kitchenName = o.kitchenName;
        m.kitchenHide = o.kitchenHide;
        // A combo's part goes on the combo's ticket only if it is made there
        // too: a drink poured at the counter (or the bar) isn't the kitchen's.
        if (linked) {
            const auto where = [](const std::string &p) { return p.empty() ? std::string("kitchen") : p; };
            if (linked->kitchenHide || where(linked->printer) != where(l->printer))
                m.kitchenHide = true;
        }
        // Keep the group's choices together, in the order of the groups.
        l->modifiers.push_back(m);
    }
    changed(*c);
    return true;
}

bool PosService::finishChoosing()
{
    const QVariantMap info = choosingInfo();
    if (!info.value(u"active"_s).toBool())
        return true;
    for (const QVariant &v : info.value(u"groups"_s).toList()) {
        const QVariantMap g = v.toMap();
        if (!g.value(u"done"_s).toBool())
            return fail(tr("%1: %2.").arg(g.value(u"name"_s).toString(), g.value(u"rule"_s).toString().toLower()));
    }
    choosingLine_ = 0;
    emit checkChanged();
    return true;
}

bool PosService::cancelChoosing()
{
    Check *c = current();
    if (!c || choosingLine_ == 0)
        return true;
    c->removeLine(choosingLine_);   // still unsent: the item comes off
    choosingLine_ = 0;
    selectedLine_ = 0;
    changed(*c);
    return true;
}

// --- 86 (sold out) ----------------------------------------------------------------------------

bool PosService::setAvailable(const QString &itemId, bool available)
{
    if (!require(perm::Order, tr("Marking items sold out")))
        return false;
    for (int i = 0; i < int(s_->menu.size()); ++i) {
        MenuItem &m = s_->menu[i];
        if (qs(m.id) != itemId)
            continue;
        if (m.available == available)
            return true;
        m.available = available;
        m.autoSoldOut = false;   // a person decided
        if (s_->sink)
            s_->sink->saveMenuItem(m, i);
        emit notice(available ? tr("%1 is back").arg(qs(m.name)) : tr("%1 is sold out (86)").arg(qs(m.name)));
        ++s_->adminRevision;
        emit s_->adminChanged();
        return true;
    }
    return fail(tr("'%1' is not on the menu.").arg(itemId));
}

int PosService::ticketsSold(const MenuItem &item) const
{
    // Earlier days', plus every ticket on today's checks, open or closed.
    int sold = item.ticketsSoldBefore;
    const auto count = [&](const Check &c) {
        if (c.training)
            return;
        for (const OrderLine &l : c.lines)
            if (!l.voided && l.itemId == item.id)
                sold += l.quantity;
    };
    for (const auto &[id, c] : s_->open)
        count(c);
    for (const Check &c : s_->closedToday)
        count(c);
    return sold;
}

int PosService::ticketsLeft(const MenuItem &item) const
{
    return item.ticketCapacity > 0 ? std::max(0, item.ticketCapacity - ticketsSold(item)) : -1;
}

QStringList PosService::popularItems() const
{
    // Today's checks, closed and open (not practice): how many of each item.
    std::map<std::string, int> sold;
    const auto count = [&](const Check &c) {
        if (c.training || c.status == CheckStatus::Discarded || c.status == CheckStatus::Merged)
            return;
        for (const OrderLine &l : c.lines)
            if (!l.isComment() && !l.voided && !l.isGiftCard())
                sold[l.itemId] += std::max(1, l.quantity);
    };
    for (const Check &c : s_->closedToday)
        count(c);
    for (const auto &[id, c] : s_->open)
        count(c);
    std::vector<std::pair<int, std::string>> ranked;
    for (const auto &[id, n] : sold)
        if (const MenuItem *m = findItem(qs(id)); m && !m->isModifier)
            ranked.emplace_back(n, id);
    std::ranges::stable_sort(ranked, [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList out;
    for (const auto &[n, id] : ranked)
        if (out.size() < 24)
            out << qs(id);
    return out;
}

QStringList PosService::soldOut() const
{
    QStringList out;
    for (const MenuItem &m : s_->menu) {
        if (!m.available || ticketsLeft(m) == 0)
            out << qs(m.id) << qs(m.name).toLower();
    }
    return out;
}

QVariantList PosService::menuItems() const
{
    QVariantList out;
    if (!user())
        return out;
    const std::string period = currentMealPeriod();
    for (const MenuItem &m : s_->menu) {
        out.append(QVariantMap{{u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)}, {u"family"_s, qs(m.family)},
                               {u"price"_s, format(m.priceDuring(period))}, {u"modifier"_s, m.isModifier},
                               {u"available"_s, m.available && ticketsLeft(m) != 0}, {u"image"_s, qs(m.image)},
                               {u"color"_s, qs(m.kitchenColor)}, {u"byWeight"_s, m.byWeight},
                               {u"unit"_s, qs(m.weightUnit)}, {u"number"_s, qs(m.number)}});
    }
    return out;
}

} // namespace vt::app
