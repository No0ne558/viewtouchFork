// Implementation of the pure-C++ POS domain (tax, menu, employees, checks).

#include "core/check.hh"
#include "core/waitlist.hh"
#include "core/day.hh"
#include "core/employee.hh"
#include "core/menu.hh"
#include "core/tax.hh"

#include <algorithm>

namespace vt::core {

// --- tax ---------------------------------------------------------------------

std::string toString(TaxClass c)
{
    switch (c) {
    case TaxClass::None: return "none";
    case TaxClass::Food: return "food";
    case TaxClass::Alcohol: return "alcohol";
    case TaxClass::Merchandise: return "merchandise";
    case TaxClass::Room: return "room";
    }
    return "food";
}

TaxClass taxClassFromString(const std::string &s)
{
    if (s == "none") return TaxClass::None;
    if (s == "alcohol") return TaxClass::Alcohol;
    if (s == "merchandise") return TaxClass::Merchandise;
    if (s == "room") return TaxClass::Room;
    return TaxClass::Food;
}

// --- menu --------------------------------------------------------------------

std::string toString(Qualifier q)
{
    switch (q) {
    case Qualifier::None: return "";
    case Qualifier::No: return "no";
    case Qualifier::Lite: return "lite";
    case Qualifier::Extra: return "extra";
    case Qualifier::Side: return "side";
    case Qualifier::Only: return "only";
    case Qualifier::Double: return "double";
    }
    return "";
}

Qualifier qualifierFromString(const std::string &s)
{
    if (s == "no") return Qualifier::No;
    if (s == "lite") return Qualifier::Lite;
    if (s == "extra") return Qualifier::Extra;
    if (s == "side") return Qualifier::Side;
    if (s == "only") return Qualifier::Only;
    if (s == "double") return Qualifier::Double;
    return Qualifier::None;
}

std::string qualifierPrefix(Qualifier q)
{
    switch (q) {
    case Qualifier::None: return "";
    case Qualifier::No: return "No ";
    case Qualifier::Lite: return "Lite ";
    case Qualifier::Extra: return "Extra ";
    case Qualifier::Side: return "Side of ";
    case Qualifier::Only: return "Only ";
    case Qualifier::Double: return "Double ";
    }
    return "";
}

Money qualifiedPrice(Money unit, Qualifier q)
{
    switch (q) {
    case Qualifier::No:
    case Qualifier::Lite:
        return Money();
    case Qualifier::Double:
        return unit * 2;
    default:
        return unit;
    }
}

// --- employees -----------------------------------------------------------------

std::set<std::string> permissionsForRole(const std::string &role)
{
    if (role == "admin" || role == "manager")
        return {perm::Order, perm::Settle, perm::Discount, perm::Void, perm::Manager, perm::EditLayout};
    if (role == "cashier" || role == "server" || role == "bartender")
        return {perm::Order, perm::Settle, perm::Discount};
    if (role == "host" || role == "kiosk")
        return {perm::Order};   // the waitlist and seating; guests ordering on their own
    // A busser clocks in and out (and gets a share of the tips).
    return {};
}

// --- day ---------------------------------------------------------------------------

std::string toString(CashMovement::Kind k)
{
    switch (k) {
    case CashMovement::Kind::Payout: return "payout";
    case CashMovement::Kind::PaidIn: return "paidIn";
    case CashMovement::Kind::TipPayout: return "tipPayout";
    }
    return "payout";
}

CashMovement::Kind cashMovementKindFromString(const std::string &s)
{
    if (s == "paidIn") return CashMovement::Kind::PaidIn;
    if (s == "tipPayout") return CashMovement::Kind::TipPayout;
    return CashMovement::Kind::Payout;
}

// --- enums -------------------------------------------------------------------------

std::string toString(CheckType t)
{
    switch (t) {
    case CheckType::DineIn: return "dineIn";
    case CheckType::Takeout: return "takeout";
    case CheckType::Quick: return "quick";
    case CheckType::Delivery: return "delivery";
    }
    return "dineIn";
}

CheckType checkTypeFromString(const std::string &s)
{
    if (s == "takeout") return CheckType::Takeout;
    if (s == "quick") return CheckType::Quick;
    if (s == "delivery") return CheckType::Delivery;
    return CheckType::DineIn;
}

std::string toString(CheckStatus s)
{
    switch (s) {
    case CheckStatus::Open: return "open";
    case CheckStatus::Closed: return "closed";
    case CheckStatus::Discarded: return "discarded";
    case CheckStatus::Merged: return "merged";
    }
    return "open";
}

CheckStatus checkStatusFromString(const std::string &s)
{
    if (s == "closed") return CheckStatus::Closed;
    if (s == "discarded") return CheckStatus::Discarded;
    if (s == "merged") return CheckStatus::Merged;
    return CheckStatus::Open;
}

std::string toString(TenderKind k)
{
    switch (k) {
    case TenderKind::Cash: return "cash";
    case TenderKind::Card: return "card";
    case TenderKind::Discount: return "discount";
    case TenderKind::GiftCard: return "giftcard";
    case TenderKind::HouseAccount: return "house";
    }
    return "cash";
}

std::string toString(Party::Status s)
{
    switch (s) {
    case Party::Status::Booked: return "booked";
    case Party::Status::Waiting: return "waiting";
    case Party::Status::Notified: return "notified";
    case Party::Status::Seated: return "seated";
    case Party::Status::Left: return "left";
    case Party::Status::NoShow: return "noShow";
    }
    return "waiting";
}

Party::Status partyStatusFromString(const std::string &s)
{
    if (s == "booked") return Party::Status::Booked;
    if (s == "notified") return Party::Status::Notified;
    if (s == "seated") return Party::Status::Seated;
    if (s == "left") return Party::Status::Left;
    if (s == "noShow") return Party::Status::NoShow;
    return Party::Status::Waiting;
}

TenderKind tenderKindFromString(const std::string &s)
{
    if (s == "card") return TenderKind::Card;
    if (s == "discount") return TenderKind::Discount;
    if (s == "giftcard") return TenderKind::GiftCard;
    if (s == "house") return TenderKind::HouseAccount;
    return TenderKind::Cash;
}

// --- check ---------------------------------------------------------------------------

Money OrderLine::total() const
{
    if (voided)
        return Money();
    Money each = qualifiedPrice(unitPrice, qualifier);
    for (const Modifier &m : modifiers)
        each += m.price();
    return each * quantity;
}

OrderLine *Check::line(std::int64_t lineId)
{
    auto it = std::ranges::find(lines, lineId, &OrderLine::id);
    return it == lines.end() ? nullptr : &*it;
}

const OrderLine *Check::line(std::int64_t lineId) const
{
    auto it = std::ranges::find(lines, lineId, &OrderLine::id);
    return it == lines.end() ? nullptr : &*it;
}

OrderLine *Check::lastItemLine()
{
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (!it->isComment() && !it->sent && !it->voided)
            return &*it;
    }
    return nullptr;
}

OrderLine &Check::addItem(const MenuItem &item, Qualifier q)
{
    OrderLine l;
    l.id = nextLineId++;
    l.itemId = item.id;
    l.name = item.name;
    l.unitPrice = item.price;
    l.taxClass = item.taxClass;
    l.qualifier = q;
    l.printer = item.printer;
    l.kitchenName = item.kitchenName;
    l.kitchenColor = item.kitchenColor;
    l.kitchenHide = item.kitchenHide;
    l.noDiscount = item.noDiscount;
    l.noStaffDiscount = item.noStaffDiscount;
    lines.push_back(l);
    return lines.back();
}

bool Check::addModifier(std::int64_t lineId, const MenuItem &modifier, Qualifier q)
{
    OrderLine *l = line(lineId);
    if (!l || l->sent || l->voided || l->isComment())
        return false;
    l->modifiers.push_back({modifier.id, modifier.name, modifier.price, q, {}, modifier.kitchenName, modifier.kitchenHide});
    return true;
}

OrderLine &Check::addComment(const std::string &text)
{
    OrderLine l;
    l.id = nextLineId++;
    l.name = text;
    l.taxClass = TaxClass::None;
    lines.push_back(l);
    return lines.back();
}

bool Check::removeLine(std::int64_t lineId)
{
    const OrderLine *l = line(lineId);
    if (!l || l->sent)
        return false;
    std::erase_if(lines, [&](const OrderLine &x) { return x.id == lineId; });
    return true;
}

bool Check::voidLine(std::int64_t lineId)
{
    OrderLine *l = line(lineId);
    if (!l || !l->sent || l->voided)
        return false;
    l->voided = true;
    return true;
}

std::vector<OrderLine> Check::sendable(bool everything) const
{
    std::vector<OrderLine> out;
    for (const OrderLine &l : lines) {
        if (!l.sent && (everything || !held(l)))
            out.push_back(l);
    }
    return out;
}

int Check::heldCount() const
{
    return int(std::ranges::count_if(lines, [this](const OrderLine &l) { return held(l); }));
}

int Check::fireNextCourse()
{
    int next = 0;
    for (const OrderLine &l : lines) {
        if (held(l) && (next == 0 || l.course < next))
            next = l.course;
    }
    if (next > 0)
        firedCourse = next;
    return next;
}

int Check::sendAll(std::int64_t now, bool everything)
{
    if (everything) {
        for (const OrderLine &l : lines)
            firedCourse = std::max(firedCourse, l.course);
    }
    // Each send is one kitchen ticket, told apart by its time: keep them
    // distinct even for two sends within the same millisecond.
    for (const OrderLine &l : lines) {
        if (l.sent && l.sentAt >= now)
            now = l.sentAt + 1;
    }
    int count = 0;
    for (OrderLine &l : lines) {
        if (!l.sent && !held(l)) {
            l.sent = true;
            l.sentAt = now;
            ++count;
        }
    }
    return count;
}

int Check::unsentCount() const
{
    return int(std::ranges::count_if(lines, [](const OrderLine &l) { return !l.sent; }));
}

std::optional<OrderLine> Check::takeLine(std::int64_t lineId)
{
    auto it = std::ranges::find(lines, lineId, &OrderLine::id);
    if (it == lines.end())
        return std::nullopt;
    OrderLine l = std::move(*it);
    lines.erase(it);
    return l;
}

OrderLine &Check::adoptLine(OrderLine line)
{
    line.id = nextLineId++;
    lines.push_back(std::move(line));
    return lines.back();
}

void Check::absorb(Check &other)
{
    for (OrderLine &l : other.lines)
        adoptLine(std::move(l));
    for (Payment &p : other.payments) {
        p.id = nextPaymentId++;
        payments.push_back(std::move(p));
    }
    other.lines.clear();
    other.payments.clear();
    guests += other.guests;
    rush = rush || other.rush;
    vip = vip || other.vip;
    if (customer.empty())
        customer = other.customer;
}

Payment &Check::addPayment(const Tender &tender, Money amount)
{
    Payment p;
    p.id = nextPaymentId++;
    p.tenderId = tender.id;
    p.tenderName = tender.name;
    p.kind = tender.kind;
    if (tender.kind == TenderKind::Discount) {
        p.percentBp = tender.percentBp;
        p.staffMeal = tender.staffMeal;
    }
    else
        p.amount = amount;
    payments.push_back(p);
    return payments.back();
}

bool Check::removePayment(std::int64_t paymentId)
{
    return std::erase_if(payments, [&](const Payment &p) { return p.id == paymentId; }) > 0;
}

Totals Check::totals(const TaxRates &rates) const
{
    Totals t;

    std::map<TaxClass, Money> byClass;
    Money discountable, staffDiscountable;   // what percent discounts and staff meals apply to
    for (const OrderLine &l : lines) {
        const Money lt = l.total();
        t.items += lt;
        if (!l.isGiftCard() && !l.noDiscount)
            discountable += lt;
        if (!l.isGiftCard() && !l.noDiscount && !l.noStaffDiscount)
            staffDiscountable += lt;
        if (l.taxClass != TaxClass::None)
            byClass[l.taxClass] += lt;
    }

    // Discounts: each is a share of the items total; together they never
    // exceed it.
    for (const Payment &p : payments) {
        if (p.kind != TenderKind::Discount)
            continue;
        // Percent off what may be discounted, or a fixed amount (rewards, promotions).
        const Money off = (p.staffMeal ? staffDiscountable : discountable).percent(p.percentBp) + p.amount;
        t.discounts += off;
        if (p.staffMeal)
            t.staffMeals += off;
    }
    if (t.discounts > t.items)
        t.discounts = t.items;
    t.subtotal = t.items - t.discounts;

    // Spread the discount over tax classes in proportion to their sales,
    // giving rounding leftovers to the largest class, then tax each class
    // on its net total.
    Money taxableTotal;
    for (const auto &[cls, amount] : byClass)
        taxableTotal += amount;
    // The part of the discount that falls on taxable sales (comments and
    // untaxed lines take the rest).
    const Money taxableDiscount = taxableTotal.cents() > 0 && t.items.cents() > 0
        ? Money::fromCents(t.discounts.cents() * taxableTotal.cents() / t.items.cents())
        : Money();
    Money allocated;
    TaxClass largest = TaxClass::None;
    Money largestAmount;
    std::map<TaxClass, Money> discountShare;
    for (const auto &[cls, amount] : byClass) {
        if (largest == TaxClass::None || amount > largestAmount) {
            largest = cls;
            largestAmount = amount;
        }
        if (taxableTotal.cents() > 0) {
            const Money share = Money::fromCents(taxableDiscount.cents() * amount.cents() / taxableTotal.cents());
            discountShare[cls] = share;
            allocated += share;
        }
    }
    if (largest != TaxClass::None)
        discountShare[largest] += taxableDiscount - allocated;

    for (const auto &[cls, amount] : byClass) {
        if (cls == TaxClass::Food && type == CheckType::Takeout && !rates.taxTakeoutFood)
            continue;
        const Money net = amount - discountShare[cls];
        const Money tax = net.cents() > 0 ? net.ppm(rates.ratePpm(cls)) : Money();
        if (tax.cents() != 0)
            t.taxByClass[cls] = tax;
        t.tax += tax;
    }

    // Gratuity: on the (discounted) subtotal, not taxed, added after tax.
    t.gratuity = t.subtotal.percent(gratuityBp);
    t.total = t.subtotal + t.tax + t.gratuity;
    for (const Payment &p : payments) {
        t.tips += p.tip;
        if (p.kind != TenderKind::Discount)
            t.paid += p.amount;
        if (p.kind == TenderKind::Cash)
            t.cashPaid += p.amount;
    }
    // Paid in cash where pennies are gone: what was owed when the cash came
    // is rounded to the nearest 5 (10) cents.
    if (rates.cashRoundingCents > 1 && t.cashPaid.cents() > 0) {
        const std::int64_t step = rates.cashRoundingCents;
        const std::int64_t due = (t.total - (t.paid - t.cashPaid)).cents();
        if (due > 0)
            t.rounding = Money::fromCents((due + step / 2) / step * step - due);
    }
    t.balance = t.total + t.rounding - t.paid;
    t.change = t.balance.cents() < 0 ? -t.balance : Money();
    return t;
}

} // namespace vt::core
