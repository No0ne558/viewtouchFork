// PosService: refunds on closed checks, today's or from any day before
// (found with Find a Check). A manager's. A card from a Stripe reader goes
// back through Stripe; cash comes out of the drawer (or the person's bank)
// as a refund pay-out; anything else is recorded (refund it where it was
// taken). Each refund goes on the check (its history) and on today's
// reports, the day the money went back.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QPointer>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

Check *PosService::closedCheckFor(qint64 checkId)
{
    for (Check &c : s_->closedToday)
        if (c.id == checkId)
            return &c;
    for (Check &c : searchHits_)
        if (c.id == checkId)
            return &c;
    return nullptr;
}

bool PosService::refundPayment(qint64 checkId, qint64 paymentId, qint64 cents, const QString &reason)
{
    // The manager who approved it (or the one logged in) is who refunded it.
    const std::string approver = approved_ ? approved_->by : (user() ? user()->name : std::string());
    if (!require(perm::Manager, tr("Refunds")))
        return false;
    Check *c = closedCheckFor(checkId);
    if (!c)
        return fail(tr("Find the check first (Find a Check)."));
    if (c->status != CheckStatus::Closed)
        return fail(tr("Refunds are for closed checks; on an open one, use Undo Payment."));
    const auto p = std::ranges::find(c->payments, paymentId, &Payment::id);
    if (p == c->payments.end())
        return fail(tr("That payment isn't on the check."));
    if (p->kind == TenderKind::Discount || p->kind == TenderKind::GiftCard || p->kind == TenderKind::HouseAccount)
        return fail(tr("%1 can't be refunded here.").arg(qs(p->tenderName)));
    const Money left = p->amount + p->tip - c->refunded(paymentId);
    const Money amount = cents > 0 ? Money::fromCents(cents) : left;
    if (left.cents() <= 0)
        return fail(tr("That payment was already refunded in full."));
    if (amount.cents() <= 0 || amount > left)
        return fail(tr("Refund up to %1.").arg(format(left)));
    const QString why = reason.trimmed();
    if (why.isEmpty())
        return fail(tr("Say why (the reason goes in the check's history)."));
    const QString key = u"%1/%2"_s.arg(checkId).arg(paymentId);
    if (refundsUnderway_.contains(key))
        return fail(tr("That refund is on its way."));

    const Payment paid = *p;
    const QString label = qs(c->label);
    QPointer<PosService> self(this);
    // On the check (both copies: today's and the one found), its history,
    // and today's refunds.
    const std::string method = paid.processor == "stripe" && !paid.reference.empty() ? "stripe"
                             : paid.kind == TenderKind::Cash                         ? "cash"
                                                                                     : "recorded";
    const auto record = [self, checkId, paid, amount, why, approver, label, method](const QString &reference) {
        if (!self)
            return;
        PosShared *s = self->s_;
        Refund r;
        r.id = ++s->lastRefundId;
        r.at = self->now();
        r.paymentId = paid.id;
        r.amount = amount;
        r.reason = ss(why);
        r.by = approver;
        r.reference = ss(reference);
        r.tenderName = paid.tenderName;
        r.method = method;
        r.day = s->day.id;
        r.checkId = checkId;
        r.checkLabel = ss(label);
        const QString what = tr("Refunded %1 (%2): %3").arg(self->format(amount), qs(paid.tenderName), why)
                             + (reference.isEmpty() ? QString() : u" · "_s + reference);
        Check *saved = nullptr;
        for (Check &c : s->closedToday)
            if (c.id == checkId) {
                c.refunds.push_back(r);
                c.note(r.at, approver, ss(what), "refund", amount);
                saved = &c;
            }
        for (Check &c : self->searchHits_)
            if (c.id == checkId) {
                c.refunds.push_back(r);
                c.note(r.at, approver, ss(what), "refund", amount);
                if (!saved)
                    saved = &c;
            }
        s->refundsToday.push_back(r);
        if (s->sink) {
            if (saved)
                s->sink->saveCheck(*saved);
            s->sink->saveRefund(r);
        }
        emit self->notice(tr("Refunded %1 on %2 #%3").arg(self->format(amount), label).arg(checkId));
        emit self->sessionChanged();
        emit s->dayChanged();
    };

    if (paid.processor == "stripe" && !paid.reference.empty()) {
        if (!s_->stripeRefund || s_->settings.stripeSecretKey.empty())
            return fail(tr("Refunding a card needs the store's Stripe key (Store Settings), or refund it in the Stripe Dashboard."));
        refundsUnderway_.insert(key);
        emit notice(tr("Refunding %1 through Stripe…").arg(format(amount)));
        s_->stripeRefund(qs(paid.reference), amount.cents(),
                         [self, key, record](const QString &refundId, const QString &error) {
            if (!self)
                return;
            self->refundsUnderway_.remove(key);
            if (!error.isEmpty()) {
                self->fail(tr("The refund didn't go through: %1").arg(error));
                emit self->sessionChanged();
                return;
            }
            record(refundId);
        });
        emit sessionChanged();
        return true;
    }
    if (paid.kind == TenderKind::Cash) {
        // Out of this person's bank or this terminal's drawer, as a refund.
        DrawerSession *d = serverBank() ? ensureMyBank() : myDrawer();
        if (!d)
            return fail(noDrawerMessage());
        CashMovement m;
        m.id = d->nextMovementId++;
        m.kind = CashMovement::Kind::Payout;
        m.amount = amount;
        m.reason = ss(tr("Refund, %1 #%2: %3").arg(label).arg(checkId).arg(why));
        m.by = approver;
        m.at = now();
        m.category = ss(tr("Refunds"));
        d->movements.push_back(m);
        if (s_->sink)
            s_->sink->saveDrawer(*d);
        if (s_->printer && !serverBank() && terminalHasDrawer())
            s_->printer->openDrawer(s_->settings, receiptPrinter());
        emit s_->drawerChanged();
        record({});
        return true;
    }
    // A card typed in (taken on another machine): give it back there.
    record({});
    emit notice(tr("Recorded. Refund %1 on the card machine it was taken on.").arg(format(amount)));
    return true;
}

} // namespace vt::app
