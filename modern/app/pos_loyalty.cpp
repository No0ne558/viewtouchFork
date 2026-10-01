// PosService: loyalty (points for what customers spend, rewards that take
// money off) and promotions (happy hour, buy one get one...) applied by
// themselves while they run. Both show on the check as discounts.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

bool runningAt(const PosSettings::Promotion &p, std::int64_t ms)
{
    if (!p.active)
        return false;
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms);
    if (!(p.days & (1 << (t.date().dayOfWeek() % 7))))
        return false;
    if (p.startMinute == p.endMinute)
        return true;
    const int m = t.time().hour() * 60 + t.time().minute();
    return p.startMinute < p.endMinute ? m >= p.startMinute && m < p.endMinute
                                       : m >= p.startMinute || m < p.endMinute;   // past midnight
}

} // namespace

// --- promotions --------------------------------------------------------------------------

Money PosService::promotionAmount(const PosSettings::Promotion &p, const Check &c) const
{
    // What the matching lines are worth, one unit at a time.
    std::vector<Money> units;
    for (const OrderLine &l : c.lines) {
        if (l.voided || l.isComment() || l.isGiftCard())
            continue;
        const MenuItem *m = findItem(qs(l.itemId));
        const bool family = m && std::ranges::find(p.families, m->family) != p.families.end();
        const bool item = std::ranges::find(p.items, l.itemId) != p.items.end();
        if (!family && !item)
            continue;
        const int qty = std::max(1, l.quantity);
        for (int i = 0; i < qty; ++i)
            units.push_back(Money::fromCents(l.total().cents() / qty));
    }
    Money off;
    if (p.buy <= 0) {
        for (Money u : units)
            off += u.percent(p.percentBp);
        return off;
    }
    // Buy N get M: in every N + M, the cheapest M at percent off.
    std::ranges::sort(units, std::greater<>());
    const int group = p.buy + std::max(1, p.get);
    for (int start = 0; start + group <= int(units.size()); start += group) {
        for (int k = start + p.buy; k < start + group; ++k)
            off += units[k].percent(p.percentBp);
    }
    return off;
}

void PosService::applyPromotions(Check &c)
{
    if (c.status != CheckStatus::Open)
        return;
    std::vector<std::pair<const PosSettings::Promotion *, Money>> wanted;
    for (const PosSettings::Promotion &p : s_->settings.promotions) {
        if (!runningAt(p, now()))
            continue;
        const Money off = promotionAmount(p, c);
        if (off.cents() > 0)
            wanted.emplace_back(&p, off);
    }
    // Already as wanted: leave the payments (and their ids) alone.
    std::vector<std::pair<std::string, Money>> have, want;
    for (const Payment &pay : c.payments)
        if (pay.reference.starts_with("promo:"))
            have.emplace_back(pay.reference, pay.amount);
    for (const auto &[p, off] : wanted)
        want.emplace_back("promo:" + p->id, off);
    if (have == want)
        return;
    std::erase_if(c.payments, [](const Payment &pay) { return pay.reference.starts_with("promo:"); });
    for (const auto &[p, off] : wanted) {
        Payment pay;
        pay.id = c.nextPaymentId++;
        pay.tenderId = "promo";
        pay.tenderName = p->name;
        pay.kind = TenderKind::Discount;
        pay.amount = off;
        pay.reference = "promo:" + p->id;
        c.payments.push_back(pay);
    }
}

QVariantList PosService::promotionsNow() const
{
    QVariantList out;
    for (const PosSettings::Promotion &p : s_->settings.promotions)
        if (runningAt(p, now()))
            out.append(qs(p.name));
    return out;
}

// --- loyalty -----------------------------------------------------------------------------

int PosService::pointsFor(const Check &c) const
{
    if (!s_->settings.loyaltyEnabled)
        return 0;
    // On what they paid for, after discounts, not gift cards.
    Money giftCards;
    for (const OrderLine &l : c.lines)
        if (l.isGiftCard() && !l.voided)
            giftCards += l.total();
    const Money spent = c.totals(s_->settings.tax).subtotal - giftCards;
    return spent.cents() > 0 ? int(spent.cents() / 100 * s_->settings.pointsPerDollar) : 0;
}

void PosService::earnPoints(Check &c)
{
    CustomerRecord *r = s_->customer(c.customerId);
    const int points = r ? pointsFor(c) : 0;
    c.pointsEarned = points;
    if (points > 0) {
        r->points += points;
        r->lifetimePoints += points;
        saveCustomerRecord(*r);
    }
}

void PosService::takeBackPoints(Check &c)
{
    if (CustomerRecord *r = s_->customer(c.customerId); r && c.pointsEarned > 0) {
        r->points = std::max(0, r->points - c.pointsEarned);
        r->lifetimePoints = std::max(0, r->lifetimePoints - c.pointsEarned);
        saveCustomerRecord(*r);
    }
    c.pointsEarned = 0;
}

bool PosService::redeemReward(int index)
{
    if (!require(perm::Settle, tr("Rewards")))
        return false;
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    if (!s_->settings.loyaltyEnabled || index < 0 || index >= int(s_->settings.rewards.size()))
        return fail(tr("There is no such reward."));
    CustomerRecord *r = s_->customer(c->customerId);
    if (!r)
        return fail(tr("Put the customer on the check first."));
    const PosSettings::Reward &reward = s_->settings.rewards[index];
    if (r->points < reward.points)
        return fail(tr("%1 has %2 points; this takes %3.").arg(qs(r->name)).arg(r->points).arg(reward.points));
    const Totals t = c->totals(s_->settings.tax);
    const Money off = std::min(reward.value, t.subtotal);
    if (off.cents() <= 0)
        return fail(tr("Nothing to take it off."));
    Payment pay;
    pay.id = c->nextPaymentId++;
    pay.tenderId = "reward";
    pay.tenderName = ss(tr("Reward: %1 off").arg(format(reward.value)));
    pay.kind = TenderKind::Discount;
    pay.amount = off;
    pay.reference = "reward:" + std::to_string(reward.points);
    c->payments.push_back(pay);
    r->points -= reward.points;
    saveCustomerRecord(*r);
    noteEvent(*c, tr("Reward %1 (%2 points)").arg(format(off)).arg(reward.points), "discount");
    emit notice(tr("%1 off for %2 points; %3 left").arg(format(off)).arg(reward.points).arg(r->points));
    changed(*c);
    return true;
}

bool PosService::customerJoin(const QString &phone)
{
    if (!require(perm::Order, tr("Rewards sign-up")))
        return false;
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    const std::string digits = CustomerRecord::digits(ss(phone));
    if (digits.size() < 7)
        return fail(tr("Enter a phone number."));
    const auto it = std::ranges::find_if(s_->customers, [&](const CustomerRecord &r) {
        return CustomerRecord::digits(r.phone) == digits;
    });
    if (it != s_->customers.end())
        return useCustomer(qs(it->id));
    return saveCustomer({{u"phone"_s, phone.trimmed()}, {u"name"_s, QString()}});
}

bool PosService::sendReceipt(const QString &how, const QString &to)
{
    const Check *last = nullptr;
    for (const Check &c : s_->closedToday)
        if (c.id == lastClosedId_)
            last = &c;
    if (!last)
        return fail(tr("No check to send."));
    if (how == u"print")
        return printReceipt();
    if (how != u"text")
        return true;   // no receipt
    if (CustomerRecord::digits(ss(to)).size() < 7)
        return fail(tr("Enter a phone number."));
    if (!s_->sendText)
        return fail(tr("Texting isn't set up (Store Settings)."));
    const Totals t = last->totals(s_->settings.tax);
    QStringList items;
    for (const OrderLine &l : last->lines)
        if (!l.voided && !l.isComment())
            items << (l.quantity > 1 ? u"%1 x %2"_s.arg(l.quantity).arg(qs(l.name)) : qs(l.name));
    const QString text = tr("%1 - check #%2\n%3\nTotal %4%5\nThank you!")
                             .arg(qs(s_->settings.storeName)).arg(last->id).arg(items.join(u", "_s), format(t.total),
                                  t.tips.cents() ? tr(" + tip %1").arg(format(t.tips)) : QString());
    s_->sendText(to.trimmed(), text);
    emit notice(tr("Receipt texted"));
    return true;
}

} // namespace vt::app
