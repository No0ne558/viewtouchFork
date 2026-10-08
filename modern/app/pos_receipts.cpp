// PosService: the guest's receipt. Printed on the screen's receipt printer,
// or one chosen each time (a handheld: Terminals -> Ask each time); after a
// check is paid, printed by itself or offered (print / email / none, per
// terminal); emailed by Stripe for cards taken on a Stripe reader.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

bool stripeCard(const Payment &p)
{
    return p.processor == "stripe" && p.reference.starts_with("pi_");
}

} // namespace

const TerminalConfig *PosService::terminalConfig() const
{
    for (const TerminalConfig &t : s_->settings.terminals)
        if (qs(t.name) == terminal_)
            return &t;
    return nullptr;
}

bool PosService::askReceiptPrinter() const
{
    const TerminalConfig *t = terminalConfig();
    return t && t->receiptPrinter == "ask";
}

const Check *PosService::checkForReceipt(qint64 checkId) const
{
    if (const auto it = s_->open.find(checkId); it != s_->open.end())
        return &it->second;
    for (const Check &c : s_->closedToday)
        if (c.id == checkId)
            return &c;
    for (const Check &c : searchHits_)
        if (c.id == checkId)
            return &c;
    return nullptr;
}

QVariantList PosService::receiptPrinters() const
{
    QVariantList out;
    for (const PrinterConfig &p : s_->settings.printers)
        if (p.receipts && p.type != "none")
            out.append(QVariantMap{{u"id"_s, qs(p.id)}, {u"name"_s, qs(p.name)}});
    return out;
}

void PosService::offerReceipt(const Check &c, bool choosePrinter)
{
    QString email;
    if (const CustomerRecord *r = s_->customer(c.customerId))
        email = qs(r->email);
    receiptOffer_ = {{u"checkId"_s, qint64(c.id)}, {u"label"_s, qs(c.label)},
                     {u"total"_s, format(c.totals(s_->settings.tax).total)},
                     {u"printers"_s, receiptPrinters()}, {u"choosePrinter"_s, choosePrinter},
                     {u"canEmail"_s, std::ranges::any_of(c.payments, stripeCard)}, {u"email"_s, email}};
    emit sessionChanged();
}

void PosService::receiptAfterClosing(const Check &c)
{
    const TerminalConfig *t = terminalConfig();
    const std::string after = t ? t->afterPaying : std::string();
    if (after == "ask") {
        offerReceipt(c, true);
    } else if (after == "print" && s_->printer) {
        if (askReceiptPrinter())
            offerReceipt(c, true);   // printed, but where?
        else
            s_->printer->printReceipt(s_->settings, c, receiptPrinter());
    }
}

bool PosService::printReceiptOn(qint64 checkId, const QString &printerId)
{
    if (!require(perm::Settle, tr("Printing receipts")))
        return false;
    const Check *c = checkForReceipt(checkId);
    if (!c)
        return fail(tr("That check isn't here any more."));
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    const PrinterConfig *p = s_->settings.printer(ss(printerId));
    if (!p)
        return fail(tr("Choose a printer."));
    s_->printer->printReceipt(s_->settings, *c, p->id);
    receiptOffer_.clear();
    emit sessionChanged();
    emit notice(tr("Receipt printing on %1").arg(qs(p->name)));
    return true;
}

bool PosService::emailReceipt(qint64 checkId, const QString &address)
{
    if (!require(perm::Settle, tr("Emailing receipts")))
        return false;
    const Check *c = checkForReceipt(checkId);
    if (!c)
        return fail(tr("That check isn't here any more."));
    const QString email = address.trimmed();
    static const QRegularExpression valid(u"^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$"_s);
    if (!valid.match(email).hasMatch())
        return fail(tr("Type the guest's email address."));
    std::vector<std::string> intents;
    for (const Payment &p : c->payments)
        if (stripeCard(p))
            intents.push_back(p.reference);
    if (intents.empty())
        return fail(tr("Email receipts are for cards taken on a Stripe reader."));
    if (!s_->stripeCall || s_->settings.stripeSecretKey.empty())
        return fail(tr("Emailing a receipt needs the store's Stripe key (Store Settings)."));
    // Kept on file for next time.
    if (CustomerRecord *r = s_->customer(c->customerId); r && r->email.empty()) {
        r->email = ss(email);
        saveCustomerRecord(*r);
    }
    // Stripe sends its receipt for each card payment: the charge's
    // receipt_email (set, it emails one; Stripe sends these in live mode).
    QPointer<PosService> self(this);
    for (const std::string &pi : intents) {
        stripe(u"GET"_s, u"/v1/payment_intents/%1"_s.arg(qs(pi)), {},
               [self, email](const QJsonObject &intent, const QString &error) {
            if (!self)
                return;
            const QString charge = intent.value(u"latest_charge").toString();
            if (!error.isEmpty() || charge.isEmpty()) {
                emit self->notice(tr("Stripe couldn't find that payment: %1").arg(error));
                return;
            }
            self->stripe(u"POST"_s, u"/v1/charges/%1"_s.arg(charge),
                         u"receipt_email="_s + QString::fromLatin1(QUrl::toPercentEncoding(email)),
                         [self, email](const QJsonObject &, const QString &error) {
                if (self)
                    emit self->notice(error.isEmpty() ? tr("Stripe is emailing the receipt to %1").arg(email)
                                                      : tr("The receipt didn't go: %1").arg(error));
            });
        });
    }
    receiptOffer_.clear();
    emit sessionChanged();
    return true;
}

void PosService::noReceipt()
{
    receiptOffer_.clear();
    emit sessionChanged();
}

} // namespace vt::app
