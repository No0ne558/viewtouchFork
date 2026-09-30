// PosService: business day, cash drawer, reports, receipts, split checks.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>
#include <QLocale>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

QString clockText(std::int64_t ms)
{
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms).time(), QLocale::ShortFormat);
}

QVariantMap toVariant(const Report &r)
{
    QVariantList rows;
    for (const ReportRow &row : r.rows) {
        QStringList cells;
        for (const std::string &c : row.cells)
            cells << qs(c);
        QString kind = u"line"_s;
        if (row.kind == ReportRow::Kind::Section) kind = u"section"_s;
        else if (row.kind == ReportRow::Kind::Total) kind = u"total"_s;
        else if (row.kind == ReportRow::Kind::Note) kind = u"note"_s;
        rows.append(QVariantMap{{u"kind"_s, kind}, {u"cells"_s, cells}});
    }
    QStringList columns;
    for (const std::string &c : r.columns)
        columns << qs(c);
    return {{u"id"_s, qs(r.id)}, {u"title"_s, qs(r.title)}, {u"subtitle"_s, qs(r.subtitle)},
            {u"columns"_s, columns}, {u"rows"_s, rows}};
}

const QStringList kReportIds = {u"sales"_s, u"items"_s, u"servers"_s, u"tips"_s, u"labor"_s, u"drawer"_s};

} // namespace

void PosShared::startDay()
{
    day = BusinessDay{++lastDayId, now(), 0};
    if (sink)
        sink->saveDay(day, {});
}

QString PosService::dayLabel(const BusinessDay &day) const
{
    const QDateTime opened = QDateTime::fromMSecsSinceEpoch(day.openedAt);
    return QLocale().toString(opened.date(), QLocale::ShortFormat) + u"  "_s + clockText(day.openedAt);
}

ReportContext PosService::reportContext(const QString &period) const
{
    return ReportContext{s_->settings, ss(period), [](std::int64_t ms) { return ss(clockText(ms)); }, now()};
}

// --- receipts ------------------------------------------------------------------------

bool PosService::printReceipt()
{
    const Check *c = currentCheck();
    if (!c) {
        for (const Check &closed : s_->closedToday) {
            if (closed.id == lastClosedId_)
                c = &closed;
        }
    }
    if (!c)
        return fail(tr("No check to print."));
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    s_->printer->printReceipt(s_->settings, *c, receiptPrinter());
    emit notice(tr("Printing receipt for %1").arg(qs(c->label)));
    return true;
}

bool PosService::noSale()
{
    if (!require(perm::Settle, tr("Opening the drawer")))
        return false;
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    s_->printer->openDrawer(s_->settings, receiptPrinter());
    emit notice(tr("Drawer opened (no sale)"));
    return true;
}

// --- split check ---------------------------------------------------------------------

void PosService::setCheckFilter(const QString &label)
{
    if (label == checkFilter_)
        return;
    checkFilter_ = label;
    emit openChecksChanged();   // this terminal only
}

bool PosService::splitLine(qint64 targetCheckId)
{
    if (!require(perm::Order, tr("Splitting checks")))
        return false;
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    if (!c->line(selectedLine_))
        return fail(tr("Touch an item on the check first."));
    if (!c->payments.empty())
        return fail(tr("Remove payments before splitting this check."));

    Check *target = nullptr;
    if (targetCheckId != 0) {
        auto it = s_->open.find(targetCheckId);
        if (it == s_->open.end() || it->second.label != c->label)
            return fail(tr("That check is not at this table."));
        if (!it->second.payments.empty())
            return fail(tr("Remove payments on check #%1 first.").arg(targetCheckId));
        if (const QString holder = lockHolder(targetCheckId); !holder.isEmpty())
            return fail(tr("Check #%1 is open on %2.").arg(targetCheckId).arg(holder));
        target = &it->second;
    } else {
        Check n;
        n.id = ++s_->lastCheckId;
        n.type = c->type;
        n.label = c->label;
        n.guests = 1;
        n.serverId = c->serverId;
        n.serverName = c->serverName;
        n.openedAt = now();
        const auto id = n.id;
        s_->open.emplace(id, std::move(n));
        c = current();   // map insert keeps references, but be explicit
        target = &s_->open.at(id);
    }

    std::optional<OrderLine> line = c->takeLine(selectedLine_);
    const QString name = qs(line->displayName());
    target->adoptLine(std::move(*line));
    selectedLine_ = 0;
    if (s_->sink) {
        s_->sink->saveCheck(*target);
        s_->sink->saveCheck(*c);
    }
    emit notice(tr("Moved %1 to check #%2").arg(name).arg(target->id));
    emit checkChanged();
    emit s_->checksChanged();
    return true;
}

// --- drawers (one per terminal) ------------------------------------------------------

namespace {
// Drawers from before per-terminal drawers have no terminal: any terminal may use them.
bool belongsTo(const DrawerSession &d, const std::string &terminal)
{
    return d.terminal.empty() || d.terminal == terminal;
}
} // namespace

DrawerSession *PosShared::openDrawerFor(const std::string &terminal)
{
    for (DrawerSession &d : drawers) {
        if (d.open() && belongsTo(d, terminal))
            return &d;
    }
    return nullptr;
}

const DrawerSession *PosShared::latestDrawerFor(const std::string &terminal) const
{
    const DrawerSession *latest = nullptr;
    for (const DrawerSession &d : drawers) {
        if (belongsTo(d, terminal) && (!latest || d.id > latest->id))
            latest = &d;
    }
    return latest;
}

bool PosService::openDrawerSession()
{
    if (!require(perm::Settle, tr("Opening a drawer")))
        return false;
    const std::string terminal = terminal_.toStdString();
    if (const DrawerSession *open = s_->openDrawerFor(terminal))
        return fail(tr("%1 is already open.").arg(qs(open->name)));
    DrawerSession d;
    d.id = ++s_->lastDrawerId;
    d.name = ss(tr("%1 drawer").arg(terminal_));
    d.terminal = terminal;
    d.openedAt = now();
    d.openedBy = user()->name;
    d.startingCash = Money::fromCents(entry_.toLongLong());
    s_->drawers.push_back(d);
    entry_.clear();
    emit entryChanged();
    if (s_->sink)
        s_->sink->saveDrawer(d);
    if (s_->printer)
        s_->printer->openDrawer(s_->settings, receiptPrinter());
    emit notice(tr("%1 opened with %2").arg(qs(d.name), format(d.startingCash)));
    emit s_->drawerChanged();
    emit s_->dayChanged();
    return true;
}

bool PosService::countDrawer()
{
    if (!require(perm::Settle, tr("Counting the drawer")))
        return false;
    DrawerSession *open = s_->openDrawerFor(terminal_.toStdString());
    if (!open)
        return fail(tr("No drawer is open on this terminal."));
    if (entry_.isEmpty())
        return fail(tr("Count the cash, enter the amount, then Count Drawer."));
    DrawerSession &d = *open;
    d.counted = Money::fromCents(entry_.toLongLong());
    d.expected = expectedCash(d, s_->closedToday, s_->settings.tax);
    d.closedAt = now();
    d.closedBy = user()->name;
    entry_.clear();
    emit entryChanged();
    if (s_->sink)
        s_->sink->saveDrawer(d);
    const Money diff = d.overShort();
    if (s_->printer)
        s_->printer->printReport(s_->settings, drawerReport({d}, s_->closedToday, reportContext(tr("Drawer count"))),
                                 receiptPrinter());
    emit notice(diff.cents() == 0 ? tr("Drawer balanced")
                : diff.cents() < 0 ? tr("Drawer is short %1").arg(format(-diff))
                                   : tr("Drawer is over %1").arg(format(diff)));
    emit s_->drawerChanged();
    emit s_->dayChanged();
    return true;
}

bool PosService::payout(CashMovement::Kind kind)
{
    if (kind == CashMovement::Kind::TipPayout)
        return cashOutTips();
    if (!require(perm::Manager, kind == CashMovement::Kind::PaidIn ? tr("Paying in") : tr("Paying out")))
        return false;
    DrawerSession *d = s_->openDrawerFor(terminal_.toStdString());
    if (!d)
        return fail(tr("Open this terminal's drawer first."));
    const Money amount = Money::fromCents(entry_.toLongLong());
    if (amount.cents() <= 0)
        return fail(tr("Enter the amount on the keypad first."));
    CashMovement m;
    m.id = d->nextMovementId++;
    m.kind = kind;
    m.amount = amount;
    m.reason = ss(text_.trimmed());
    m.by = user()->name;
    m.at = now();
    d->movements.push_back(m);
    entry_.clear();
    text_.clear();
    emit entryChanged();
    if (s_->sink)
        s_->sink->saveDrawer(*d);
    if (s_->printer)
        s_->printer->openDrawer(s_->settings, receiptPrinter());
    emit notice(kind == CashMovement::Kind::PaidIn ? tr("Paid in %1").arg(format(amount))
                                                   : tr("Paid out %1").arg(format(amount)));
    emit s_->drawerChanged();
    emit s_->dayChanged();
    return true;
}

bool PosService::cashOutTips()
{
    if (!require(perm::Order, tr("Cashing out tips")))
        return false;
    DrawerSession *d = s_->openDrawerFor(terminal_.toStdString());
    if (!d)
        return fail(tr("Open this terminal's drawer first."));
    const Employee *e = user();
    const Money owed = core::tipsOwed(e->id, s_->closedToday, s_->drawers, s_->settings.tax);
    if (owed.cents() <= 0)
        return fail(tr("No tips are owed to %1.").arg(qs(e->name)));
    CashMovement m;
    m.id = d->nextMovementId++;
    m.kind = CashMovement::Kind::TipPayout;
    m.amount = owed;
    m.reason = e->name;
    m.by = e->name;
    m.employeeId = e->id;
    m.at = now();
    d->movements.push_back(m);
    if (s_->sink)
        s_->sink->saveDrawer(*d);
    if (s_->printer)
        s_->printer->openDrawer(s_->settings, receiptPrinter());
    emit notice(tr("Paid %1 in tips to %2").arg(format(owed), qs(e->name)));
    emit s_->drawerChanged();
    emit s_->dayChanged();
    return true;
}

QString PosService::tipsOwed() const
{
    const Employee *e = user();
    if (!e)
        return {};
    return format(core::tipsOwed(e->id, s_->closedToday, s_->drawers, s_->settings.tax));
}

QVariantMap PosService::drawerInfo() const
{
    const DrawerSession *latest = s_->latestDrawerFor(terminal_.toStdString());
    if (!latest)
        return {{u"exists"_s, false}, {u"open"_s, false}};
    const DrawerSession &d = *latest;
    Money cash;
    for (const Check &c : s_->closedToday) {
        if (c.drawerSession == d.id)
            cash += cashIntoDrawer(c, s_->settings.tax);
    }
    QVariantList movements;
    for (const CashMovement &m : d.movements) {
        const QString what = m.kind == CashMovement::Kind::PaidIn ? tr("Paid in")
                             : m.kind == CashMovement::Kind::TipPayout ? tr("Tips to %1").arg(qs(m.reason))
                                                                       : tr("Paid out");
        movements.append(QVariantMap{{u"what"_s, m.kind == CashMovement::Kind::TipPayout || m.reason.empty()
                                                    ? what : what + u": "_s + qs(m.reason)},
                                     {u"amount"_s, format(m.effect())}, {u"time"_s, clockText(m.at)}});
    }
    const Money expected = d.open() ? d.startingCash + cash + d.movementsTotal() : d.expected;
    return {
        {u"exists"_s, true}, {u"open"_s, d.open()}, {u"name"_s, qs(d.name)},
        {u"openedBy"_s, qs(d.openedBy)}, {u"opened"_s, clockText(d.openedAt)},
        {u"startingCash"_s, format(d.startingCash)}, {u"cashSales"_s, format(cash)},
        {u"movements"_s, movements},
        {u"expected"_s, format(expected)}, {u"counted"_s, format(d.counted)},
        {u"closedBy"_s, qs(d.closedBy)}, {u"overShort"_s, format(d.overShort())},
        {u"overShortCents"_s, qint64(d.overShort().cents())},
    };
}

// --- tips and gratuity ----------------------------------------------------------------

bool PosService::addTip(std::int64_t percentBp)
{
    if (!require(perm::Settle, tr("Adding tips")))
        return false;
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    // The selected card payment, else the most recent card payment.
    Payment *target = nullptr;
    for (Payment &p : c->payments) {
        if (p.kind == TenderKind::Card && (!target || p.id == selectedPayment_ || target->id != selectedPayment_))
            target = &p;
    }
    if (!target)
        return fail(tr("Tips go on a card payment. Add one first."));
    const Totals t = c->totals(s_->settings.tax);
    Money tip;
    if (percentBp > 0) {
        tip = (t.subtotal + t.tax).percent(percentBp);   // on the check before gratuity
    } else {
        if (entry_.isEmpty())
            return fail(tr("Type the tip on the keypad, then Tip."));
        tip = Money::fromCents(entry_.toLongLong());
        entry_.clear();
        emit entryChanged();
    }
    target->tip = tip;
    emit notice(tip.cents() ? tr("Tip %1 on %2").arg(format(tip), qs(target->tenderName)) : tr("Tip removed"));
    changed(*c);
    return true;
}

bool PosService::setGratuity(std::int64_t percentBp)
{
    if (!require(perm::Order, tr("Changing gratuity")))
        return false;
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    if (percentBp < 0 || percentBp > 10000)
        return fail(tr("Gratuity is between 0 and 100%."));
    if (c->autoGratuity && percentBp < c->gratuityBp && !require(perm::Manager, tr("Lowering the party gratuity")))
        return false;
    c->gratuityBp = percentBp;
    c->autoGratuity = false;
    emit notice(percentBp ? tr("Gratuity %1%").arg(double(percentBp) / 100.0) : tr("Gratuity removed"));
    changed(*c);
    return true;
}

// --- business day --------------------------------------------------------------------

QVariantMap PosService::dayInfo() const
{
    QStringList blockers;
    if (!s_->open.empty())
        blockers << (s_->open.size() == 1 ? tr("1 check is still open")
                                       : tr("%1 checks are still open").arg(s_->open.size()));
    for (const DrawerSession &d : s_->drawers) {
        if (d.open())
            blockers << tr("%1 has not been counted").arg(qs(d.name));
    }
    Money net;
    for (const Check &c : s_->closedToday)
        net += c.totals(s_->settings.tax).subtotal;
    const bool anyOpen = std::ranges::any_of(s_->drawers, &DrawerSession::open);
    return {
        {u"id"_s, qint64(s_->day.id)}, {u"opened"_s, dayLabel(s_->day)},
        {u"openChecks"_s, int(s_->open.size())}, {u"closedChecks"_s, int(s_->closedToday.size())},
        {u"netSales"_s, format(net)}, {u"drawerOpen"_s, anyOpen},
        {u"blockers"_s, blockers}, {u"ready"_s, blockers.isEmpty()},
    };
}

QVariantList PosService::days() const
{
    QVariantList out{QVariantMap{{u"id"_s, qint64(0)}, {u"label"_s, tr("Today (since %1)").arg(clockText(s_->day.openedAt))}}};
    for (const PastDay &p : s_->pastDays)
        out.append(QVariantMap{{u"id"_s, qint64(p.day.id)}, {u"label"_s, dayLabel(p.day)}});
    return out;
}

bool PosService::endOfDay()
{
    if (!require(perm::Manager, tr("End of day")))
        return false;
    if (!s_->open.empty())
        return fail(s_->open.size() == 1 ? tr("Settle the open check first.")
                                      : tr("Settle the %1 open checks first.").arg(s_->open.size()));
    for (const DrawerSession &d : s_->drawers) {
        if (d.open())
            return fail(tr("Count %1 first.").arg(qs(d.name)));
    }

    QJsonObject reports;
    for (const QString &id : kReportIds)
        reports.insert(id, toJson(buildReport(id)));
    s_->day.closedAt = now();
    if (s_->sink)
        s_->sink->saveDay(s_->day, reports);
    s_->pastDays.insert(s_->pastDays.begin(), PastDay{s_->day, reports});
    if (s_->printer)
        s_->printer->printReport(s_->settings, buildReport(u"sales"_s), receiptPrinter());

    s_->closedToday.clear();
    std::erase_if(s_->punches, [](const TimePunch &p) { return !p.open(); });
    s_->drawers.clear();
    lastClosedId_ = 0;
    const std::int64_t closedId = s_->day.id;
    s_->startDay();
    emit notice(tr("Day %1 closed. A new day has started.").arg(closedId));
    emit s_->drawerChanged();
    emit s_->dayChanged();
    return true;
}

// --- reports ---------------------------------------------------------------------------

Report PosService::buildReport(const QString &id) const
{
    const ReportContext ctx = reportContext(tr("Today, since %1").arg(clockText(s_->day.openedAt)));
    if (id == u"items")
        return itemSales(s_->closedToday, s_->menu, ctx);
    if (id == u"servers")
        return serverSales(s_->closedToday, ctx);
    if (id == u"labor")
        return laborReport(s_->punches, s_->employees, ctx);
    if (id == u"drawer")
        return drawerReport(s_->drawers, s_->closedToday, ctx);
    if (id == u"tips")
        return tipsReport(s_->closedToday, s_->drawers, ctx);
    return salesSummary(s_->closedToday, ctx);
}

QVariantMap PosService::report(const QString &id, qint64 dayId)
{
    if (dayId == 0 || dayId == s_->day.id)
        return toVariant(buildReport(id));
    for (const PastDay &p : s_->pastDays) {
        if (p.day.id == dayId) {
            const QJsonObject stored = p.reports.value(id).toObject();
            if (stored.isEmpty())
                return {{u"title"_s, tr("No report")}, {u"rows"_s, QVariantList()}};
            return toVariant(reportFromJson(stored));
        }
    }
    return {{u"title"_s, tr("No such day")}, {u"rows"_s, QVariantList()}};
}

bool PosService::printReport(const QString &id, qint64 dayId)
{
    if (!require(perm::Manager, tr("Printing reports")))
        return false;
    if (!s_->printer)
        return fail(tr("No printer is set up."));
    Report r;
    if (dayId == 0 || dayId == s_->day.id) {
        r = buildReport(id);
    } else {
        for (const PastDay &p : s_->pastDays) {
            if (p.day.id == dayId)
                r = reportFromJson(p.reports.value(id).toObject());
        }
    }
    s_->printer->printReport(s_->settings, r, receiptPrinter());
    emit notice(tr("Printing %1").arg(qs(r.title)));
    return true;
}

} // namespace vt::app
