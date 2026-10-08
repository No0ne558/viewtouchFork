// PosService: cards taken on a reader (a Stripe smart reader, or the simulated
// one). The terminal's reader collects the card; this side prepares the
// charge, records the approved payment, hands readers their connection
// tokens, and refunds a card payment taken back. The store's Stripe secret
// key never leaves this computer (PosShared::stripeConnectionToken / stripeRefund).

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QPointer>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

QString cardText(const Payment &p)
{
    if (p.last4.empty())
        return {};
    QString brand = qs(p.cardBrand);
    if (!brand.isEmpty())
        brand[0] = brand[0].toUpper();
    return (brand.isEmpty() ? QString() : brand + u' ') + u"•••• "_s + qs(p.last4);
}

} // namespace

QString PosService::terminalCardReader() const
{
    for (const TerminalConfig &t : s_->settings.terminals)
        if (qs(t.name) == terminal_)
            return qs(t.cardReader);
    return {};
}

QVariantMap PosService::cardCharge(const QString &tenderId)
{
    const auto no = [&](const QString &why) {
        emit notice(why);
        return QVariantMap{{u"ok"_s, false}, {u"error"_s, why}};
    };
    const Tender *t = s_->settings.tender(ss(tenderId));
    if (!t || t->kind != TenderKind::Card)
        return {{u"ok"_s, false}, {u"notCard"_s, true}};   // not for the reader: paid as usual
    if (!can(QString::fromLatin1(perm::Settle)))
        return no(tr("Taking payments needs permission."));
    Check *c = current();
    if (!c)
        return no(tr("No check is open."));
    if (forAnotherDay(*c))
        return no(tr("This order is for %1: take the payment that day.").arg(dueText(c->dueAt)));
    const Totals totals = c->totals(s_->settings.tax);
    if (totals.balance.cents() <= 0)
        return no(tr("Nothing is owed on this check."));
    Money amount = entry_.isEmpty() ? totals.balance : Money::fromCents(entry_.toLongLong());
    if (amount.cents() <= 0)
        return no(tr("Enter an amount."));
    if (amount > totals.balance)
        amount = totals.balance;   // a card isn't over-tendered
    // A tip the guest chose on the customer display goes on their card.
    const Money tip = tipChoice_.chosen && tipChoice_.checkId == c->id ? tipFor(*c) : Money();
    return {{u"ok"_s, true}, {u"tenderId"_s, tenderId}, {u"checkId"_s, qint64(c->id)},
            {u"amountCents"_s, qint64(amount.cents())}, {u"tipCents"_s, qint64(tip.cents())},
            {u"currency"_s, qs(s_->settings.cardCurrency)},
            {u"description"_s, u"%1 #%2"_s.arg(qs(c->label)).arg(c->id)},
            {u"amount"_s, format(amount + tip)}};
}

bool PosService::recordCardPayment(const QVariantMap &r)
{
    if (!require(perm::Settle, tr("Taking payments")))
        return false;
    const QString reference = r.value(u"reference"_s).toString();
    if (reference.isEmpty())
        return fail(tr("The card reader didn't say which payment it was."));
    // The same approval twice (a resend after a dropped connection): once.
    const auto has = [&](const Check &c) {
        return std::ranges::any_of(c.payments, [&](const Payment &p) { return qs(p.reference) == reference; });
    };
    for (const auto &[id, c] : s_->open)
        if (has(c))
            return true;
    for (const Check &c : s_->closedToday)
        if (has(c))
            return true;
    const auto it = s_->open.find(r.value(u"checkId"_s).toLongLong());
    if (it == s_->open.end())
        return fail(tr("The check for this card payment is closed: look it up in Stripe and refund it there."));
    Check &c = it->second;
    const Tender *t = s_->settings.tender(ss(r.value(u"tenderId"_s).toString()));
    if (!t || t->kind != TenderKind::Card)
        t = nullptr;
    if (!t)
        for (const Tender &x : s_->settings.tenders)
            if (x.kind == TenderKind::Card && !t)
                t = &x;
    if (!t)
        return fail(tr("Set up a card payment type first (Manager -> Payment Types)."));
    const Money tip = Money::fromCents(std::max<qint64>(0, r.value(u"tipCents"_s).toLongLong()));
    // What was charged, less the tip, pays toward the check.
    const Money charged = Money::fromCents(r.value(u"amountCents"_s).toLongLong());
    const Money amount = charged - tip;
    if (amount.cents() <= 0)
        return fail(tr("The card reader approved nothing."));
    Payment &paid = c.addPayment(*t, amount);
    paid.tip = tip;
    paid.reference = ss(reference);
    paid.processor = ss(r.value(u"processor"_s).toString());
    paid.cardBrand = ss(r.value(u"brand"_s).toString().toLower());
    paid.last4 = ss(r.value(u"last4"_s).toString().right(4));
    const QString card = cardText(paid);
    noteEvent(c, tr("Card approved: %1 %2%3").arg(card.isEmpty() ? qs(t->name) : card, format(amount),
                                                  tip.cents() ? tr(" + %1 tip").arg(format(tip)) : QString()),
              "pay", amount);
    if (c.id == currentId_) {
        entry_.clear();
        emit entryChanged();
    }
    emit notice(tr("Approved: %1").arg(card.isEmpty() ? format(charged) : card + u"  "_s + format(charged)));
    changed(c);
    return true;
}

void PosService::requestReaderToken()
{
    const int seq = ++readerTokenSeq_;
    const auto done = [this, seq](const QString &token, const QString &error) {
        readerToken_ = {{u"seq"_s, seq}, {u"token"_s, token}, {u"error"_s, error}};
        emit sessionChanged();
    };
    if (!s_->stripeConnectionToken || s_->settings.stripeSecretKey.empty()) {
        done({}, tr("The store's Stripe key isn't set (Manager -> Store Settings)."));
        return;
    }
    QPointer<PosService> self(this);
    s_->stripeConnectionToken([self, done](const QString &token, const QString &error) {
        if (self)
            done(token, error);
    });
}

PosService::Refund PosService::refundCardPayment(const Check &c, const Payment &p)
{
    // Practice payments, and cards typed in by hand, just come off.
    if (p.processor != "stripe" || p.reference.empty() || c.training)
        return Refund::NotNeeded;
    if (!s_->stripeRefund || s_->settings.stripeSecretKey.empty()) {
        fail(tr("Refunding a card needs the store's Stripe key (Store Settings), or refund it in the Stripe Dashboard."));
        return Refund::CantNow;
    }
    if (refunding_.contains(p.id)) {
        fail(tr("That refund is on its way."));
        return Refund::CantNow;
    }
    refunding_.insert(p.id);
    emit notice(tr("Refunding %1…").arg(format(p.amount + p.tip)));
    QPointer<PosService> self(this);
    const std::int64_t checkId = c.id, paymentId = p.id;
    s_->stripeRefund(qs(p.reference), (p.amount + p.tip).cents(),
                     [self, checkId, paymentId](const QString &refundId, const QString &error) {
        if (!self)
            return;
        self->refunding_.erase(paymentId);
        const auto it = self->s_->open.find(checkId);
        if (!error.isEmpty() || it == self->s_->open.end()) {
            self->fail(error.isEmpty() ? tr("The check closed before the refund came back: check Stripe.")
                                       : tr("The refund didn't go through: %1").arg(error));
            return;
        }
        Check &check = it->second;
        const auto paid = std::ranges::find(check.payments, paymentId, &Payment::id);
        if (paid == check.payments.end())
            return;
        const Payment removed = *paid;
        check.removePayment(paymentId);
        self->noteEvent(check, tr("Card refunded: %1 %2 (%3)").arg(cardText(removed), self->format(removed.amount + removed.tip),
                                                                    refundId),
                        "unpay", removed.amount);
        emit self->notice(tr("Refunded %1").arg(self->format(removed.amount + removed.tip)));
        self->changed(check);
    });
    return Refund::Started;
}

} // namespace vt::app
