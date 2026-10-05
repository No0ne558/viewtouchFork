// PosService: finding any check, today's or from earlier days - by number,
// amount, table, customer, server, item or gift card - and reprinting it.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>
#include <QPointer>
#include <QRegularExpression>
#include <QThreadPool>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr int kMaxHits = 50;
constexpr std::int64_t kDayMs = 24LL * 3'600'000;

bool contains(const std::string &text, const QString &query)
{
    return QString::fromStdString(text).contains(query, Qt::CaseInsensitive);
}

// "#123" or "123": that check; "17.62" or "$17.62": that total; else text
// in the table, customer, phone, server, items or gift card numbers.
bool matches(const Check &c, const QString &q, const TaxRates &rates)
{
    static const QRegularExpression number(u"^#?(\\d{1,9})$"_s);
    static const QRegularExpression amount(u"^\\$?(\\d+)\\.(\\d\\d)$"_s);
    if (const auto m = number.match(q); m.hasMatch()) {
        if (c.id == m.captured(1).toLongLong())
            return true;
        if (q.startsWith(u'#'))
            return false;
        // Digits: also a phone number.
        return QString::fromStdString(CustomerRecord::digits(c.customer.phone)).contains(m.captured(1));
    }
    if (const auto m = amount.match(q); m.hasMatch()) {
        const std::int64_t cents = m.captured(1).toLongLong() * 100 + m.captured(2).toLongLong();
        const Totals t = c.totals(rates);
        return t.total.cents() == cents || (t.total + t.tips).cents() == cents;
    }
    if (contains(c.label, q) || contains(c.customer.name, q) || contains(c.customer.phone, q) || contains(c.serverName, q))
        return true;
    for (const OrderLine &l : c.lines)
        if (contains(l.name, q))
            return true;
    for (const Payment &p : c.payments)
        if (contains(p.reference, q) || contains(p.tenderName, q))
            return true;
    return false;
}

} // namespace

bool PosService::searchChecks(const QString &query, int days)
{
    if (!require(perm::Settle, tr("Finding checks")))
        return false;
    const QString q = query.trimmed();
    if (q.size() < 2)
        return fail(tr("Type at least two letters or digits."));
    days = std::clamp(days, 1, 3660);
    const int request = ++searchRequest_;
    searchHits_.clear();
    selectedHit_ = 0;
    checkSearch_ = {{u"query"_s, q}, {u"loading"_s, true}, {u"days"_s, days}};
    emit sessionChanged();

    // Earlier days are read away from the screen; today's and open checks are here.
    std::vector<Check> here = s_->closedToday;
    for (const auto &[id, c] : s_->open)
        here.push_back(c);
    const auto history = s_->history;
    const TaxRates rates = s_->settings.tax;
    const std::int64_t to = now() + kDayMs, from = now() - std::int64_t(days) * kDayMs;
    QPointer<PosService> self(this);
    QThreadPool::globalInstance()->start([=, here = std::move(here)]() mutable {
        std::vector<Check> hits;
        for (Check &c : here)
            if (c.status != CheckStatus::Discarded && matches(c, q, rates))
                hits.push_back(std::move(c));
        if (history) {
            for (Check &c : history(from, to)) {
                if (matches(c, q, rates) && std::ranges::none_of(hits, [&](const Check &x) { return x.id == c.id; }))
                    hits.push_back(std::move(c));
            }
        }
        std::ranges::sort(hits, std::greater{}, [](const Check &c) { return c.closedAt ? c.closedAt : c.openedAt; });
        const bool more = hits.size() > std::size_t(kMaxHits);
        if (more)
            hits.resize(kMaxHits);
        QMetaObject::invokeMethod(self, [self, request, more, hits = std::move(hits)]() mutable {
            if (!self || request != self->searchRequest_)
                return;   // a newer search replaced it
            self->searchHits_ = std::move(hits);
            self->checkSearch_.insert(u"loading"_s, false);
            self->checkSearch_.insert(u"more"_s, more);
            emit self->sessionChanged();
        }, Qt::QueuedConnection);
    });
    return true;
}

void PosService::selectFoundCheck(qint64 id)
{
    selectedHit_ = id;
    emit sessionChanged();
}

bool PosService::reprintCheck(qint64 id)
{
    if (!require(perm::Settle, tr("Printing receipts")))
        return false;
    const auto it = std::ranges::find_if(searchHits_, [&](const Check &c) { return c.id == id; });
    if (it == searchHits_.end())
        return fail(tr("Find the check first."));
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    s_->printer->printReceipt(s_->settings, *it, receiptPrinter());
    emit notice(tr("Printing a copy of check #%1").arg(id));
    return true;
}

QVariantMap PosService::checkSearch() const
{
    if (checkSearch_.isEmpty())
        return {};
    const auto when = [](std::int64_t ms) {
        return ms ? QDateTime::fromMSecsSinceEpoch(ms).toString(u"ddd MMM d, h:mm AP"_s) : QString();
    };
    const auto statusText = [this](const Check &c) {
        return c.status == CheckStatus::Open ? tr("open") : c.status == CheckStatus::Merged ? tr("merged") : tr("closed");
    };
    QVariantList results;
    for (const Check &c : searchHits_) {
        const Totals t = c.totals(s_->settings.tax);
        results.append(QVariantMap{{u"id"_s, qint64(c.id)}, {u"label"_s, qs(c.label)},
                                   {u"when"_s, when(c.closedAt ? c.closedAt : c.openedAt)},
                                   {u"server"_s, qs(c.serverName)}, {u"customer"_s, qs(c.customer.name)},
                                   {u"total"_s, format(t.total)}, {u"status"_s, statusText(c)}});
    }
    QVariantMap out = checkSearch_;
    out.insert(u"results"_s, results);
    const auto sel = std::ranges::find_if(searchHits_, [&](const Check &c) { return c.id == selectedHit_; });
    if (sel != searchHits_.end()) {
        const Check &c = *sel;
        const Totals t = c.totals(s_->settings.tax);
        QVariantList lines;
        for (const OrderLine &l : c.lines) {
            QStringList mods;
            for (const Modifier &m : l.modifiers)
                mods << qs(m.displayName());
            lines.append(QVariantMap{{u"name"_s, qs(l.displayName())}, {u"quantity"_s, l.quantity},
                                     {u"price"_s, l.isComment() ? QString() : format(l.voided ? Money() : l.total())},
                                     {u"voided"_s, l.voided}, {u"modifiers"_s, mods.join(u", "_s)}});
        }
        QVariantList payments;
        for (const Payment &p : c.payments)
            payments.append(QVariantMap{{u"name"_s, qs(p.tenderName) + (p.reference.empty() || p.kind == TenderKind::HouseAccount ? QString() : u" ("_s + qs(p.reference) + u')')},
                                        {u"amount"_s, p.kind == TenderKind::Discount ? QString() : format(p.amount)},
                                        {u"tip"_s, p.tip.cents() ? format(p.tip) : QString()}});
        out.insert(u"selected"_s, QVariantMap{
            {u"id"_s, qint64(c.id)}, {u"label"_s, qs(c.label)}, {u"server"_s, qs(c.serverName)},
            {u"opened"_s, when(c.openedAt)}, {u"closed"_s, when(c.closedAt)}, {u"status"_s, statusText(c)},
            {u"customer"_s, qs(c.customer.name)}, {u"guests"_s, c.guests},
            {u"lines"_s, lines}, {u"payments"_s, payments},
            {u"subtotal"_s, format(t.subtotal)}, {u"tax"_s, format(t.tax)}, {u"total"_s, format(t.total)},
            {u"tips"_s, t.tips.cents() ? format(t.tips) : QString()}});
    }
    return out;
}

} // namespace vt::app
