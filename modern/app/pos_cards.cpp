// PosService: cards taken on a reader (a Stripe smart reader, or the simulated
// one). The terminal's reader collects the card; this side prepares the
// charge, records the approved payment, hands readers their connection
// tokens, and refunds a card payment taken back. The store's Stripe secret
// key never leaves this computer (PosShared::stripeConnectionToken / stripeRefund).

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>

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
    return addCardPayment(r);
}

bool PosService::addCardPayment(const QVariantMap &r)
{
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

// --- a countertop reader, run from the store's computer ----------------------------
//
// The register asks for a card; the store creates the PaymentIntent, sends it
// to the reader beside the register (Stripe's server-driven integration),
// asks Stripe how it's going every 1.5 seconds, and records the payment
// when the reader says so. Nothing depends on the register staying connected.

namespace vt::app {

namespace {

QString form(std::initializer_list<std::pair<QString, QString>> fields)
{
    QStringList out;
    for (const auto &[k, v] : fields)
        out << QString::fromLatin1(QUrl::toPercentEncoding(k, "[]")) + u'=' + QString::fromLatin1(QUrl::toPercentEncoding(v));
    return out.join(u'&');
}

constexpr int kCounterPollMs = 1500;
constexpr int kCounterGiveUpPolls = 200;   // five minutes

} // namespace

QString PosService::counterReaderId() const
{
    const QString reader = terminalCardReader();
    return reader.startsWith(u"counter:"_s) ? reader.mid(8) : QString();
}

QString PosService::counterReaderLabel(const QString &id) const
{
    for (const PosSettings::StripeReader &r : s_->settings.stripeReaders)
        if (qs(r.id) == id)
            return qs(r.label);
    return id;
}

void PosService::stripe(const QString &method, const QString &path, const QString &body,
                        std::function<void(const QJsonObject &, const QString &)> done)
{
    if (!s_->stripeCall) {
        done({}, tr("This computer can't reach Stripe."));
        return;
    }
    QPointer<PosService> self(this);
    s_->stripeCall(method, path, body, [self, done = std::move(done)](const QJsonObject &r, const QString &e) {
        if (self)
            done(r, e);
    });
}

void PosService::counterDone(const QString &message)
{
    counterPoll_.stop();
    counter_.clear();
    emit sessionChanged();
    if (!message.isEmpty())
        emit notice(message);
}

bool PosService::startCounterCharge(const QString &tenderId)
{
    const QString reader = counterReaderId();
    if (reader.isEmpty())
        return fail(tr("This screen has no card reader beside it (Manager -> Terminals)."));
    if (s_->settings.stripeSecretKey.empty())
        return fail(tr("The store's Stripe key isn't set (Manager -> Store Settings)."));
    if (!counter_.isEmpty())
        return fail(tr("The card reader is already taking a card."));
    const QVariantMap charge = cardCharge(tenderId);
    if (charge.value(u"notCard"_s).toBool())
        return tender(tenderId);   // cash, a discount...: as usual
    if (!charge.value(u"ok"_s).toBool())
        return false;
    const qint64 total = charge.value(u"amountCents"_s).toLongLong() + charge.value(u"tipCents"_s).toLongLong();
    counter_ = charge;
    counter_.insert(u"status"_s, u"starting"_s);
    counter_.insert(u"reader"_s, reader);
    counter_.insert(u"readerLabel"_s, counterReaderLabel(reader));
    counter_.insert(u"test"_s, s_->settings.stripeSecretKey.starts_with("sk_test_")
                                   || s_->settings.stripeSecretKey.starts_with("rk_test_"));
    emit sessionChanged();
    const QString body = form({{u"amount"_s, QString::number(total)},
                               {u"currency"_s, charge.value(u"currency"_s).toString()},
                               {u"allowed_payment_method_types[]"_s, u"card_present"_s},
                               {u"capture_method"_s, u"automatic"_s},
                               {u"description"_s, charge.value(u"description"_s).toString()},
                               {u"metadata[viewtouch_check]"_s, charge.value(u"checkId"_s).toString()}});
    stripe(u"POST"_s, u"/v1/payment_intents"_s, body, [this, reader](const QJsonObject &pi, const QString &error) {
        if (!error.isEmpty() || counter_.isEmpty())
            return counterDone(error.isEmpty() ? QString() : tr("The card payment couldn't start: %1").arg(error));
        const QString id = pi.value(u"id").toString();
        counter_.insert(u"paymentIntent"_s, id);
        // The tip on the reader's screen, when that's where it's asked.
        const QString skipTip = s_->settings.cardTipOn == "reader" ? u"false"_s : u"true"_s;
        stripe(u"POST"_s, u"/v1/terminal/readers/%1/process_payment_intent"_s.arg(reader),
               form({{u"payment_intent"_s, id}, {u"process_config[skip_tipping]"_s, skipTip},
                     {u"process_config[enable_customer_cancellation]"_s, u"true"_s}}),
               [this, id](const QJsonObject &, const QString &error) {
            if (!error.isEmpty()) {
                stripe(u"POST"_s, u"/v1/payment_intents/%1/cancel"_s.arg(id), {}, [](const QJsonObject &, const QString &) {});
                return counterDone(tr("The card reader didn't take it: %1").arg(error));
            }
            counter_.insert(u"status"_s, u"waiting"_s);
            counterPolls_ = 0;
            connect(&counterPoll_, &QTimer::timeout, this, &PosService::pollCounter, Qt::UniqueConnection);
            counterPoll_.start(kCounterPollMs);
            emit sessionChanged();
        });
    });
    return true;
}

void PosService::pollCounter()
{
    if (counter_.value(u"status"_s).toString() != u"waiting" || counterAsking_)
        return;
    if (++counterPolls_ > kCounterGiveUpPolls) {
        counterPoll_.stop();
        emit notice(tr("No answer from the card reader: check it, then Cancel."));
        return;
    }
    counterAsking_ = true;
    const QString reader = counter_.value(u"reader"_s).toString();
    const QString pi = counter_.value(u"paymentIntent"_s).toString();
    stripe(u"GET"_s, u"/v1/terminal/readers/%1"_s.arg(reader), {}, [this, pi](const QJsonObject &r, const QString &error) {
        counterAsking_ = false;
        if (!error.isEmpty() || counter_.value(u"paymentIntent"_s) != pi)
            return;   // asked again in a moment
        const QJsonObject action = r.value(u"action").toObject();
        if (action.value(u"process_payment_intent").toObject().value(u"payment_intent").toString() != pi)
            return;
        const QString status = action.value(u"status").toString();
        if (status == u"succeeded")
            return counterPaid(pi);
        if (status != u"failed")
            return;
        // A lost connection may still have been paid: Stripe knows.
        if (action.value(u"failure_code").toString() == u"connection_error")
            return counterPaid(pi);
        const QString why = action.value(u"failure_message").toString();
        stripe(u"POST"_s, u"/v1/payment_intents/%1/cancel"_s.arg(pi), {}, [](const QJsonObject &, const QString &) {});
        counterDone(action.value(u"failure_code").toString() == u"customer_canceled"
                        ? tr("The guest canceled on the card reader.")
                        : tr("Card declined: %1").arg(why.isEmpty() ? tr("try another card") : why));
    });
}

void PosService::counterPaid(const QString &pi)
{
    counterPoll_.stop();
    stripe(u"GET"_s, u"/v1/payment_intents/%1"_s.arg(pi), u"expand[]=latest_charge"_s,
           [this, pi](const QJsonObject &intent, const QString &error) {
        if (counter_.value(u"paymentIntent"_s) != pi)
            return;
        const QString status = intent.value(u"status").toString();
        if (!error.isEmpty() || (status != u"succeeded" && status != u"requires_capture" && status != u"processing")) {
            stripe(u"POST"_s, u"/v1/payment_intents/%1/cancel"_s.arg(pi), {}, [](const QJsonObject &, const QString &) {});
            return counterDone(error.isEmpty() ? tr("The card didn't go through.") : tr("Stripe: %1").arg(error));
        }
        const QJsonObject card = intent.value(u"latest_charge").toObject().value(u"payment_method_details").toObject()
                                     .value(u"card_present").toObject();
        const qint64 readerTip = intent.value(u"amount_details").toObject().value(u"tip").toObject().value(u"amount").toInteger();
        QVariantMap paid{{u"reference"_s, pi}, {u"brand"_s, card.value(u"brand").toString()},
                         {u"last4"_s, card.value(u"last4").toString()},
                         {u"amountCents"_s, intent.value(u"amount").toInteger()},
                         {u"tipCents"_s, counter_.value(u"tipCents"_s).toLongLong() + readerTip},
                         {u"checkId"_s, counter_.value(u"checkId"_s)}, {u"tenderId"_s, counter_.value(u"tenderId"_s)},
                         {u"processor"_s, u"stripe"_s}};
        // On the check even if the register logged out meanwhile: the money was taken.
        const bool ok = addCardPayment(paid);
        counterDone(ok ? QString() : tr("The card was approved (%1) but isn't on the check: add it by hand or refund it in Stripe.").arg(pi));
    });
}

bool PosService::cancelCounterCharge()
{
    if (counter_.isEmpty())
        return true;
    const QString reader = counter_.value(u"reader"_s).toString();
    const QString pi = counter_.value(u"paymentIntent"_s).toString();
    if (pi.isEmpty()) {   // not on the reader yet
        counterDone(tr("Card payment canceled."));
        return true;
    }
    stripe(u"POST"_s, u"/v1/terminal/readers/%1/cancel_action"_s.arg(reader), {},
           [this, pi](const QJsonObject &, const QString &error) {
        if (!error.isEmpty() && error.contains(u"busy"_s, Qt::CaseInsensitive)) {
            emit notice(tr("The card is being read: wait a moment."));
            return;
        }
        stripe(u"POST"_s, u"/v1/payment_intents/%1/cancel"_s.arg(pi), {}, [](const QJsonObject &, const QString &) {});
        counterDone(tr("Card payment canceled."));
    });
    return true;
}

bool PosService::presentTestCard(bool decline)
{
    if (counter_.value(u"status"_s).toString() != u"waiting" || !counter_.value(u"test"_s).toBool())
        return fail(tr("Test cards are for Stripe test mode, while the reader waits."));
    const QString reader = counter_.value(u"reader"_s).toString();
    stripe(u"POST"_s, u"/v1/test_helpers/terminal/readers/%1/present_payment_method"_s.arg(reader),
           form({{u"type"_s, u"card_present"_s},
                 {u"card_present[number]"_s, decline ? u"4000000000000002"_s : u"4242424242424242"_s}}),
           [this](const QJsonObject &, const QString &error) {
        if (!error.isEmpty())
            emit notice(tr("Stripe: %1").arg(error));
    });
    return true;
}

// --- pairing countertop readers ----------------------------------------------------

void PosService::pairCounterReader(const QVariantMap &record)
{
    const QString code = record.value(u"code"_s).toString().trimmed();
    const QString label = record.value(u"label"_s).toString().trimmed();
    const auto paired = [this, label](const QJsonObject &reader, const QString &error) {
        if (!error.isEmpty()) {
            emit notice(tr("The reader didn't pair: %1").arg(error));
            return;
        }
        PosSettings::StripeReader r{ss(reader.value(u"id").toString()), ss(reader.value(u"label").toString(label)),
                                    ss(reader.value(u"device_type").toString())};
        std::erase_if(s_->settings.stripeReaders, [&](const PosSettings::StripeReader &x) { return x.id == r.id; });
        s_->settings.stripeReaders.push_back(r);
        settingsChanged();
        emit s_->adminChanged();
        emit notice(tr("Card reader paired: %1. Choose it for a screen in Terminals.").arg(qs(r.label)));
    };
    const auto registerAt = [this, code, label, paired](const QString &location) {
        stripe(u"POST"_s, u"/v1/terminal/readers"_s,
               form({{u"registration_code"_s, code}, {u"label"_s, label.isEmpty() ? code : label},
                     {u"location"_s, location}}),
               paired);
    };
    if (!s_->settings.stripeLocation.empty())
        return registerAt(qs(s_->settings.stripeLocation));
    // Readers belong to a Stripe location (the store's address): Stripe's
    // first one, or a new one from the address given.
    stripe(u"GET"_s, u"/v1/terminal/locations"_s, u"limit=1"_s,
           [this, record, registerAt](const QJsonObject &list, const QString &error) {
        if (!error.isEmpty()) {
            emit notice(tr("The reader didn't pair: %1").arg(error));
            return;
        }
        const QString found = list.value(u"data").toArray().first().toObject().value(u"id").toString();
        const auto use = [this, registerAt](const QString &id) {
            s_->settings.stripeLocation = ss(id);
            settingsChanged();
            registerAt(id);
        };
        if (!found.isEmpty())
            return use(found);
        const auto text = [&](const char16_t *k) { return record.value(QString::fromUtf16(k)).toString().trimmed(); };
        if (text(u"line1").isEmpty() || text(u"city").isEmpty() || text(u"country").isEmpty()) {
            emit notice(tr("Stripe needs the store's address once: fill in Street, City, State, ZIP and Country."));
            return;
        }
        stripe(u"POST"_s, u"/v1/terminal/locations"_s,
               form({{u"display_name"_s, qs(s_->settings.storeName)}, {u"address[line1]"_s, text(u"line1")},
                     {u"address[city]"_s, text(u"city")}, {u"address[state]"_s, text(u"state")},
                     {u"address[postal_code]"_s, text(u"postalCode")}, {u"address[country]"_s, text(u"country").toUpper()}}),
               [this, use](const QJsonObject &location, const QString &error) {
            if (!error.isEmpty()) {
                emit notice(tr("Stripe didn't take the address: %1").arg(error));
                return;
            }
            use(location.value(u"id").toString());
        });
    });
}

} // namespace vt::app
