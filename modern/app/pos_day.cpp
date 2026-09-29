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

const QStringList kReportIds = {u"sales"_s, u"items"_s, u"servers"_s, u"labor"_s, u"drawer"_s};

} // namespace

void PosService::startDay()
{
    day_ = BusinessDay{++lastDayId_, now(), 0};
    if (sink_)
        sink_->saveDay(day_, {});
}

QString PosService::dayLabel(const BusinessDay &day) const
{
    const QDateTime opened = QDateTime::fromMSecsSinceEpoch(day.openedAt);
    return QLocale().toString(opened.date(), QLocale::ShortFormat) + u"  "_s + clockText(day.openedAt);
}

ReportContext PosService::reportContext(const QString &period) const
{
    return ReportContext{settings_, ss(period), [](std::int64_t ms) { return ss(clockText(ms)); }, now()};
}

// --- receipts ------------------------------------------------------------------------

bool PosService::printReceipt()
{
    const Check *c = currentCheck();
    if (!c) {
        for (const Check &closed : closedToday_) {
            if (closed.id == lastClosedId_)
                c = &closed;
        }
    }
    if (!c)
        return fail(tr("No check to print."));
    if (!printer_)
        return fail(tr("No printer is set up."));
    printer_->printReceipt(settings_, *c);
    emit notice(tr("Printing receipt for %1").arg(qs(c->label)));
    return true;
}

bool PosService::noSale()
{
    if (!require(perm::Settle, tr("Opening the drawer")))
        return false;
    if (!printer_)
        return fail(tr("No printer is set up."));
    printer_->openDrawer(settings_);
    emit notice(tr("Drawer opened (no sale)"));
    return true;
}

// --- split check ---------------------------------------------------------------------

void PosService::setCheckFilter(const QString &label)
{
    if (label == checkFilter_)
        return;
    checkFilter_ = label;
    emit openChecksChanged();
}

QVariantList PosService::splitTargets() const
{
    QVariantList out;
    const Check *c = currentCheck();
    if (!c)
        return out;
    for (const auto &[id, other] : open_) {
        if (id != c->id && other.label == c->label && other.type == c->type) {
            out.append(QVariantMap{{u"id"_s, qint64(id)}, {u"label"_s, tr("Check #%1").arg(id)},
                                   {u"total"_s, format(other.totals(settings_.tax).total)},
                                   {u"count"_s, int(other.lines.size())}});
        }
    }
    out.append(QVariantMap{{u"id"_s, qint64(0)}, {u"label"_s, tr("New check")}, {u"total"_s, QString()},
                           {u"count"_s, 0}});
    return out;
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
        auto it = open_.find(targetCheckId);
        if (it == open_.end() || it->second.label != c->label)
            return fail(tr("That check is not at this table."));
        if (!it->second.payments.empty())
            return fail(tr("Remove payments on check #%1 first.").arg(targetCheckId));
        target = &it->second;
    } else {
        Check n;
        n.id = ++lastCheckId_;
        n.type = c->type;
        n.label = c->label;
        n.guests = 1;
        n.serverId = c->serverId;
        n.serverName = c->serverName;
        n.openedAt = now();
        const auto id = n.id;
        open_.emplace(id, std::move(n));
        c = current();   // map insert keeps references, but be explicit
        target = &open_.at(id);
    }

    std::optional<OrderLine> line = c->takeLine(selectedLine_);
    const QString name = qs(line->displayName());
    target->adoptLine(std::move(*line));
    selectedLine_ = 0;
    if (sink_) {
        sink_->saveCheck(*target);
        sink_->saveCheck(*c);
    }
    emit notice(tr("Moved %1 to check #%2").arg(name).arg(target->id));
    emit checkChanged();
    emit openChecksChanged();
    return true;
}

// --- drawer --------------------------------------------------------------------------

bool PosService::openDrawerSession()
{
    if (!require(perm::Settle, tr("Opening a drawer")))
        return false;
    if (drawer_ && drawer_->open())
        return fail(tr("%1 is already open.").arg(qs(drawer_->name)));
    DrawerSession d;
    d.id = ++lastDrawerId_;
    d.openedAt = now();
    d.openedBy = user_->name;
    d.startingCash = Money::fromCents(entry_.toLongLong());
    drawer_ = d;
    entry_.clear();
    emit entryChanged();
    if (sink_)
        sink_->saveDrawer(*drawer_);
    if (printer_)
        printer_->openDrawer(settings_);
    emit notice(tr("%1 opened with %2").arg(qs(d.name), format(d.startingCash)));
    emit drawerChanged();
    emit dayChanged();
    return true;
}

bool PosService::countDrawer()
{
    if (!require(perm::Settle, tr("Counting the drawer")))
        return false;
    if (!drawer_ || !drawer_->open())
        return fail(tr("No drawer is open."));
    if (entry_.isEmpty())
        return fail(tr("Count the cash, enter the amount, then Count Drawer."));
    DrawerSession &d = *drawer_;
    d.counted = Money::fromCents(entry_.toLongLong());
    d.expected = expectedCash(d, closedToday_, settings_.tax);
    d.closedAt = now();
    d.closedBy = user_->name;
    entry_.clear();
    emit entryChanged();
    if (sink_)
        sink_->saveDrawer(d);
    const Money diff = d.overShort();
    if (printer_)
        printer_->printReport(settings_, drawerReport(&d, closedToday_, reportContext(tr("Drawer count"))));
    emit notice(diff.cents() == 0 ? tr("Drawer balanced")
                : diff.cents() < 0 ? tr("Drawer is short %1").arg(format(-diff))
                                   : tr("Drawer is over %1").arg(format(diff)));
    emit drawerChanged();
    emit dayChanged();
    return true;
}

QVariantMap PosService::drawerInfo() const
{
    if (!drawer_)
        return {{u"exists"_s, false}, {u"open"_s, false}};
    const DrawerSession &d = *drawer_;
    Money cash;
    for (const Check &c : closedToday_) {
        if (c.drawerSession == d.id)
            cash += cashIntoDrawer(c, settings_.tax);
    }
    const Money expected = d.open() ? d.startingCash + cash : d.expected;
    return {
        {u"exists"_s, true}, {u"open"_s, d.open()}, {u"name"_s, qs(d.name)},
        {u"openedBy"_s, qs(d.openedBy)}, {u"opened"_s, clockText(d.openedAt)},
        {u"startingCash"_s, format(d.startingCash)}, {u"cashSales"_s, format(cash)},
        {u"expected"_s, format(expected)}, {u"counted"_s, format(d.counted)},
        {u"closedBy"_s, qs(d.closedBy)}, {u"overShort"_s, format(d.overShort())},
        {u"overShortCents"_s, qint64(d.overShort().cents())},
    };
}

// --- business day --------------------------------------------------------------------

QVariantMap PosService::dayInfo() const
{
    QStringList blockers;
    if (!open_.empty())
        blockers << (open_.size() == 1 ? tr("1 check is still open")
                                       : tr("%1 checks are still open").arg(open_.size()));
    if (drawer_ && drawer_->open())
        blockers << tr("%1 has not been counted").arg(qs(drawer_->name));
    Money net;
    for (const Check &c : closedToday_)
        net += c.totals(settings_.tax).subtotal;
    return {
        {u"id"_s, qint64(day_.id)}, {u"opened"_s, dayLabel(day_)},
        {u"openChecks"_s, int(open_.size())}, {u"closedChecks"_s, int(closedToday_.size())},
        {u"netSales"_s, format(net)}, {u"drawerOpen"_s, drawer_ && drawer_->open()},
        {u"blockers"_s, blockers}, {u"ready"_s, blockers.isEmpty()},
    };
}

QVariantList PosService::days() const
{
    QVariantList out{QVariantMap{{u"id"_s, qint64(0)}, {u"label"_s, tr("Today (since %1)").arg(clockText(day_.openedAt))}}};
    for (const PastDay &p : pastDays_)
        out.append(QVariantMap{{u"id"_s, qint64(p.day.id)}, {u"label"_s, dayLabel(p.day)}});
    return out;
}

bool PosService::endOfDay()
{
    if (!require(perm::Manager, tr("End of day")))
        return false;
    if (!open_.empty())
        return fail(open_.size() == 1 ? tr("Settle the open check first.")
                                      : tr("Settle the %1 open checks first.").arg(open_.size()));
    if (drawer_ && drawer_->open())
        return fail(tr("Count %1 first.").arg(qs(drawer_->name)));

    QJsonObject reports;
    for (const QString &id : kReportIds)
        reports.insert(id, toJson(buildReport(id)));
    day_.closedAt = now();
    if (sink_)
        sink_->saveDay(day_, reports);
    pastDays_.insert(pastDays_.begin(), PastDay{day_, reports});
    if (printer_)
        printer_->printReport(settings_, buildReport(u"sales"_s));

    closedToday_.clear();
    std::erase_if(punches_, [](const TimePunch &p) { return !p.open(); });
    drawer_.reset();
    lastClosedId_ = 0;
    const std::int64_t closedId = day_.id;
    startDay();
    emit notice(tr("Day %1 closed. A new day has started.").arg(closedId));
    emit drawerChanged();
    emit dayChanged();
    return true;
}

// --- reports ---------------------------------------------------------------------------

Report PosService::buildReport(const QString &id) const
{
    const ReportContext ctx = reportContext(tr("Today, since %1").arg(clockText(day_.openedAt)));
    if (id == u"items")
        return itemSales(closedToday_, menu_, ctx);
    if (id == u"servers")
        return serverSales(closedToday_, ctx);
    if (id == u"labor")
        return laborReport(punches_, employees_, ctx);
    if (id == u"drawer")
        return drawerReport(drawer_ ? &*drawer_ : nullptr, closedToday_, ctx);
    return salesSummary(closedToday_, ctx);
}

QVariantMap PosService::report(const QString &id, qint64 dayId) const
{
    if (dayId == 0 || dayId == day_.id)
        return toVariant(buildReport(id));
    for (const PastDay &p : pastDays_) {
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
    if (!printer_)
        return fail(tr("No printer is set up."));
    Report r;
    if (dayId == 0 || dayId == day_.id) {
        r = buildReport(id);
    } else {
        for (const PastDay &p : pastDays_) {
            if (p.day.id == dayId)
                r = reportFromJson(p.reports.value(id).toObject());
        }
    }
    printer_->printReport(settings_, r);
    emit notice(tr("Printing %1").arg(qs(r.title)));
    return true;
}

} // namespace vt::app
