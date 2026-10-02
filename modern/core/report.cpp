#include "core/report.hh"

#include <algorithm>
#include <cctype>
#include <tuple>
#include <cmath>
#include <map>
#include <optional>
#include <cstdio>
#include <set>

namespace vt::core {

namespace {

std::string count(std::int64_t n) { return std::to_string(n); }

std::string hours(std::int64_t ms)
{
    const std::int64_t hundredths = (ms * 100 + 1'800'000) / 3'600'000;   // rounded
    std::string frac = std::to_string(hundredths % 100);
    if (frac.size() < 2)
        frac.insert(0, "0");
    return std::to_string(hundredths / 100) + "." + frac;
}

std::string capitalized(std::string s)
{
    if (!s.empty())
        s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

} // namespace

std::vector<std::string> Report::find(const std::string &label) const
{
    for (const ReportRow &r : rows) {
        if (!r.cells.empty() && r.cells.front() == label && r.kind != ReportRow::Kind::Section)
            return r.cells;
    }
    return {};
}

std::string ReportContext::money(Money m) const
{
    const std::string s = m.toString();
    return s.front() == '-' ? "-" + settings.currencySymbol + s.substr(1) : settings.currencySymbol + s;
}

Money cashIntoDrawer(const Check &check, const TaxRates &rates)
{
    return check.totals(rates).cashNet();
}

Money expectedCash(const DrawerSession &drawer, const std::vector<Check> &closed, const TaxRates &rates)
{
    Money cash = drawer.startingCash + drawer.movementsTotal();
    for (const Check &c : closed) {
        if (c.drawerSession == drawer.id)
            cash += cashIntoDrawer(c, rates);
    }
    return cash;
}

Money tipsOwed(const std::string &employeeId, const std::vector<Check> &closed,
               const std::vector<DrawerSession> &drawers, const TaxRates &rates)
{
    Money owed;
    for (const Check &c : closed) {
        if (c.serverId == employeeId) {
            const Totals t = c.totals(rates);
            owed += t.tips + t.gratuity;
        }
    }
    for (const DrawerSession &d : drawers) {
        for (const CashMovement &m : d.movements) {
            if (m.kind == CashMovement::Kind::TipPayout && m.employeeId == employeeId)
                owed -= m.amount;
        }
    }
    return owed;
}

Report salesSummary(const std::vector<Check> &closed, const ReportContext &ctx)
{
    const TaxRates &rates = ctx.settings.tax;
    Report r;
    r.id = "sales";
    r.title = "Sales Summary";
    r.subtitle = ctx.period;
    r.columns = {"", "Amount"};

    Money items, discounts, net, tax, total, change, gratuity, tips;
    std::map<TaxClass, Money> taxByClass;
    std::map<std::string, Money> byTender;           // tender name -> amount
    std::vector<std::string> tenderOrder;
    int guests = 0, voidCount = 0;
    Money voidValue;

    for (const Check &c : closed) {
        const Totals t = c.totals(rates);
        items += t.items;
        discounts += t.discounts;
        net += t.subtotal;
        tax += t.tax;
        total += t.total;
        change += t.change;
        gratuity += t.gratuity;
        tips += t.tips;
        guests += c.guests;
        for (const auto &[cls, amount] : t.taxByClass)
            taxByClass[cls] += amount;
        for (const Payment &p : c.payments) {
            if (p.kind == TenderKind::Discount)
                continue;
            if (!byTender.contains(p.tenderName))
                tenderOrder.push_back(p.tenderName);
            byTender[p.tenderName] += p.amount;
        }
        for (const OrderLine &l : c.lines) {
            if (l.voided) {
                OrderLine live = l;
                live.voided = false;
                voidValue += live.total();
                ++voidCount;
            }
        }
    }

    r.section("Sales");
    r.line({"Checks", count(std::int64_t(closed.size()))});
    r.line({"Guests", count(guests)});
    r.line({"Item sales", ctx.money(items)});
    r.line({"Discounts & comps", ctx.money(-discounts)});
    r.total({"Net sales", ctx.money(net)});
    for (const auto &[cls, amount] : taxByClass)
        r.line({capitalized(toString(cls)) + " tax", ctx.money(amount)});
    if (gratuity.cents() != 0)
        r.line({"Gratuity", ctx.money(gratuity)});
    r.total({"Total with tax", ctx.money(total)});

    r.section("Payments");
    for (const std::string &name : tenderOrder)
        r.line({name, ctx.money(byTender[name])});
    if (change.cents() > 0)
        r.line({"Change given", ctx.money(-change)});
    Money collected = -change;
    for (const auto &[name, amount] : byTender)
        collected += amount;
    r.total({"Collected", ctx.money(collected)});
    if (tips.cents() != 0)
        r.line({"Card tips (owed to staff)", ctx.money(tips)});

    r.section("Averages");
    const auto avg = [](Money m, std::int64_t n) { return n > 0 ? Money::fromCents(m.cents()).scaled(1, n) : Money(); };
    r.line({"Per check", ctx.money(avg(net, std::int64_t(closed.size())))});
    r.line({"Per guest", ctx.money(avg(net, guests))});
    if (voidCount > 0) {
        r.section("Voids");
        r.line({"Voided items (" + count(voidCount) + ")", ctx.money(voidValue)});
    }
    return r;
}

Report itemSales(const std::vector<Check> &closed, const std::vector<MenuItem> &menu, const ReportContext &ctx)
{
    Report r;
    r.id = "items";
    r.title = "Item Sales";
    r.subtitle = ctx.period;
    r.columns = {"Item", "Qty", "Sales"};

    std::map<std::string, std::string> familyOf;
    for (const MenuItem &m : menu)
        familyOf[m.id] = m.family.empty() ? "other" : m.family;

    struct Tally { std::int64_t qty = 0; Money sales; };
    std::map<std::string, std::map<std::string, Tally>> families;   // family -> item name -> tally
    for (const Check &c : closed) {
        for (const OrderLine &l : c.lines) {
            if (l.voided || l.isComment())
                continue;
            const auto it = familyOf.find(l.itemId);
            Tally &t = families[it == familyOf.end() ? "other" : it->second][l.name];
            t.qty += l.quantity;
            t.sales += l.total();
        }
    }

    Money grand;
    std::int64_t grandQty = 0;
    for (const auto &[family, items] : families) {
        r.section(capitalized(family));
        Money sum;
        std::int64_t qty = 0;
        for (const auto &[name, t] : items) {
            r.line({name, count(t.qty), ctx.money(t.sales)});
            sum += t.sales;
            qty += t.qty;
        }
        r.total({capitalized(family) + " total", count(qty), ctx.money(sum)});
        grand += sum;
        grandQty += qty;
    }
    if (families.empty())
        r.note("No sales yet.");
    else
        r.total({"All items", count(grandQty), ctx.money(grand)});
    return r;
}

namespace {
std::string hourLabel(int h)
{
    const int twelve = h % 12 == 0 ? 12 : h % 12;
    return std::to_string(twelve) + (h < 12 ? " AM" : " PM");
}

std::string percent(Money part, Money whole)
{
    if (whole.cents() == 0)
        return "0%";
    const std::int64_t tenths = (part.cents() * 1000 + whole.cents() / 2) / whole.cents();
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "%";
}
} // namespace

Report hourlySales(const std::vector<Check> &closed, const ReportContext &ctx)
{
    Report r;
    r.id = "hourly";
    r.title = "Sales by Hour";
    r.subtitle = ctx.period;
    r.columns = {"Hour", "Checks", "Guests", "Net sales"};
    struct Tally { std::int64_t checks = 0; std::int64_t guests = 0; Money net; };
    std::map<int, Tally> hours;
    Tally all;
    for (const Check &c : closed) {
        const int h = ctx.hourOf ? ctx.hourOf(c.closedAt) : int((c.closedAt / 3'600'000) % 24);
        Tally &t = hours[h];
        const Money net = c.totals(ctx.settings.tax).subtotal;
        t.checks += 1;
        t.guests += c.guests;
        t.net += net;
        all.checks += 1;
        all.guests += c.guests;
        all.net += net;
    }
    for (const auto &[h, t] : hours)
        r.line({hourLabel(h) + " - " + hourLabel((h + 1) % 24), count(t.checks), count(t.guests), ctx.money(t.net)});
    if (hours.empty())
        r.note("No sales yet.");
    else
        r.total({"Total", count(all.checks), count(all.guests), ctx.money(all.net)});
    return r;
}

Report categorySales(const std::vector<Check> &closed, const std::vector<MenuItem> &menu, const ReportContext &ctx)
{
    Report r;
    r.id = "categories";
    r.title = "Sales by Category";
    r.subtitle = ctx.period;
    r.columns = {"Category", "Qty", "Sales", "Share"};
    std::map<std::string, std::string> familyOf;
    for (const MenuItem &m : menu)
        familyOf[m.id] = m.family.empty() ? "other" : m.family;
    struct Tally { std::int64_t qty = 0; Money sales; };
    std::map<std::string, Tally> families;
    Money total;
    std::int64_t qty = 0;
    for (const Check &c : closed) {
        for (const OrderLine &l : c.lines) {
            if (l.voided || l.isComment())
                continue;
            const auto it = familyOf.find(l.itemId);
            Tally &t = families[it == familyOf.end() ? "other" : it->second];
            t.qty += l.quantity;
            t.sales += l.total();
            total += l.total();
            qty += l.quantity;
        }
    }
    std::vector<std::pair<std::string, Tally>> sorted(families.begin(), families.end());
    std::ranges::sort(sorted, [](const auto &a, const auto &b) { return a.second.sales > b.second.sales; });
    for (const auto &[family, t] : sorted)
        r.line({capitalized(family), count(t.qty), ctx.money(t.sales), percent(t.sales, total)});
    if (sorted.empty())
        r.note("No sales yet.");
    else
        r.total({"All categories", count(qty), ctx.money(total), "100.0%"});
    return r;
}

Report auditReport(const std::vector<const Check *> &checks, const ReportContext &ctx)
{
    Report r;
    r.id = "audit";
    r.title = "Audit Trail";
    r.subtitle = ctx.period;
    r.columns = {"What", "Check", "Who", "Time"};
    struct Row { std::int64_t at; const Check *check; const CheckEvent *event; };
    std::vector<Row> rows;
    std::map<std::string, std::int64_t> counts;
    for (const Check *c : checks) {
        for (const CheckEvent &e : c->events) {
            rows.push_back({e.at, c, &e});
            counts[e.kind.empty() ? "other" : e.kind] += 1;
        }
    }
    std::ranges::sort(rows, {}, &Row::at);
    if (rows.empty()) {
        r.note("No voids, discounts, reopened, moved, transferred or merged checks.");
        return r;
    }
    r.section("Summary");
    const std::pair<const char *, const char *> kinds[] = {
        {"void", "Voids"}, {"discount", "Discounts"}, {"reopen", "Reopened checks"},
        {"transfer", "Transfers"}, {"move", "Table moves"}, {"merge", "Merges"}};
    for (const auto &[kind, label] : kinds) {
        if (const auto it = counts.find(kind); it != counts.end())
            r.line({label, "", "", count(it->second)});
    }
    r.section("Everything, in order");
    for (const Row &row : rows)
        r.line({row.event->what, row.check->label + " #" + std::to_string(row.check->id), row.event->who,
                ctx.clock(row.at)});
    return r;
}

Report serverSales(const std::vector<Check> &closed, const ReportContext &ctx)
{
    Report r;
    r.id = "servers";
    r.title = "Server Sales";
    r.subtitle = ctx.period;
    r.columns = {"Server", "Checks", "Guests", "Net sales"};

    struct Tally { std::int64_t checks = 0, guests = 0; Money net; };
    std::map<std::string, Tally> servers;
    for (const Check &c : closed) {
        Tally &t = servers[c.serverName.empty() ? "(unknown)" : c.serverName];
        ++t.checks;
        t.guests += c.guests;
        t.net += c.totals(ctx.settings.tax).subtotal;
    }
    Tally all;
    for (const auto &[name, t] : servers) {
        r.line({name, count(t.checks), count(t.guests), ctx.money(t.net)});
        all.checks += t.checks;
        all.guests += t.guests;
        all.net += t.net;
    }
    if (servers.empty())
        r.note("No sales yet.");
    else
        r.total({"All servers", count(all.checks), count(all.guests), ctx.money(all.net)});
    return r;
}

Report laborReport(const std::vector<TimePunch> &punches, const std::vector<Employee> &employees,
                   const ReportContext &ctx, const std::vector<TimePunch> &earlier, Money netSales)
{
    Report r;
    r.id = "labor";
    r.title = "Labor";
    r.subtitle = ctx.period;
    r.columns = {"Employee", "In", "Out", "Breaks", "Hours"};
    const PosSettings &s = ctx.settings;

    std::map<std::string, std::string> names;
    std::set<std::string> severalJobs;   // whose shifts say which job
    for (const Employee &e : employees) {
        names[e.id] = e.name;
        if (e.jobs().size() > 1)
            severalJobs.insert(e.id);
    }
    auto nameOf = [&](const std::string &id) {
        const auto it = names.find(id);
        return it == names.end() ? id : it->second;
    };

    std::int64_t totalMs = 0;
    std::vector<TimePunch> sorted = punches;
    std::ranges::sort(sorted, {}, &TimePunch::clockIn);
    for (const TimePunch &p : sorted) {
        const std::int64_t ms = p.workedMs(ctx.now, s.paidBreaks);
        totalMs += ms;
        const std::string out = !p.open() ? ctx.clock(p.clockOut) : p.onBreak() ? "on break" : "on clock";
        r.line({nameOf(p.employeeId) + (p.job.empty() || !severalJobs.contains(p.employeeId) ? "" : " (" + p.job + ")"),
                ctx.clock(p.clockIn), out,
                p.breaks.empty() ? "" : hours(p.breakMs(ctx.now)), hours(ms)});
    }
    if (sorted.empty()) {
        r.note("Nobody has clocked in.");
        return r;
    }
    r.total({"Total hours", "", "", "", hours(totalMs)});

    // Hours and overtime per person: daily rule per day worked, weekly rule
    // on the pay week so far; whichever gives more overtime counts.
    const std::int64_t hourMs = 3'600'000;
    struct Person {
        std::map<int, std::int64_t> byDay;
        std::int64_t today = 0;
        double payToday = 0;   // cents: today's shifts at their pay
        std::int64_t otTodayMs = 0;
    };
    std::map<std::string, Person> people;
    auto dayKey = [&](std::int64_t ms) { return ctx.dayOf ? ctx.dayOf(ms) : int(ms / (24 * hourMs)); };
    for (const TimePunch &p : sorted) {
        const std::int64_t ms = p.workedMs(ctx.now, s.paidBreaks);
        people[p.employeeId].today += ms;
        people[p.employeeId].byDay[dayKey(p.clockIn)] += ms;
        people[p.employeeId].payToday += double(ms) / hourMs * double(p.rate.cents());
    }
    for (const TimePunch &p : earlier) {
        if (p.clockIn >= ctx.weekStart && people.contains(p.employeeId))
            people[p.employeeId].byDay[dayKey(p.clockIn)] += p.workedMs(ctx.now, s.paidBreaks);
    }
    r.section("Hours and overtime");
    r.line({"", "Today", "This week", "Regular", "Overtime"});
    for (auto &[id, person] : people) {
        std::int64_t week = 0, dailyOt = 0;
        for (const auto &[day, ms] : person.byDay) {
            week += ms;
            if (s.overtimeDailyHours > 0)
                dailyOt += std::max<std::int64_t>(0, ms - s.overtimeDailyHours * hourMs);
        }
        const auto weeklyOver = [&](std::int64_t ms) {
            return s.overtimeWeeklyHours > 0 ? std::max<std::int64_t>(0, ms - s.overtimeWeeklyHours * hourMs) : 0;
        };
        const std::int64_t weeklyOt = weeklyOver(week);
        const std::int64_t ot = std::max(dailyOt, weeklyOt);
        r.line({nameOf(id), hours(person.today), hours(week), hours(week - ot), ot > 0 ? hours(ot) : "-"});
        // The overtime that falls on today: today's own daily overtime, or
        // what today added to the week's.
        const std::int64_t dailyToday = s.overtimeDailyHours > 0
                                            ? std::max<std::int64_t>(0, person.today - s.overtimeDailyHours * hourMs) : 0;
        person.otTodayMs = std::max(dailyToday, weeklyOt - weeklyOver(week - person.today));
    }
    if (s.overtimeDailyHours == 0 && s.overtimeWeeklyHours == 0)
        r.note("No overtime rule is set (Manager -> Settings).");

    // What today's labor costs: each shift at its pay, overtime at time and
    // a half (the extra half at the average pay of the day).
    double pay = 0, premium = 0;
    bool anyPay = false;
    for (const TimePunch &p : sorted)
        anyPay = anyPay || p.rate.cents() > 0;
    r.section("Labor cost today");
    if (!anyPay) {
        r.note("No pay rates are set (Manager -> Employees -> Pay rate).");
        return r;
    }
    r.line({"", "Hours", "Pay", "Overtime", "Cost"});
    for (const auto &[id, person] : people) {
        const double avg = person.today > 0 ? person.payToday / (double(person.today) / hourMs) : 0;
        const double extra = double(person.otTodayMs) / hourMs * avg * 0.5;
        pay += person.payToday;
        premium += extra;
        r.line({nameOf(id), hours(person.today), ctx.money(Money::fromCents(std::llround(person.payToday))),
                extra > 0 ? ctx.money(Money::fromCents(std::llround(extra))) : "-",
                ctx.money(Money::fromCents(std::llround(person.payToday + extra)))});
    }
    const Money cost = Money::fromCents(std::llround(pay + premium));
    r.total({"Labor cost", hours(totalMs), ctx.money(Money::fromCents(std::llround(pay))),
             premium > 0 ? ctx.money(Money::fromCents(std::llround(premium))) : "-", ctx.money(cost)});
    r.line({"Net sales", "", "", "", ctx.money(netSales)});
    if (netSales.cents() > 0) {
        char pct[16];
        std::snprintf(pct, sizeof pct, "%.1f%%", 100.0 * double(cost.cents()) / double(netSales.cents()));
        r.line({"Labor % of sales", "", "", "", pct});
    }
    return r;
}

Report drawerReport(const std::vector<DrawerSession> &drawers, const std::vector<Check> &closed,
                    const ReportContext &ctx)
{
    Report r;
    r.id = "drawer";
    r.title = "Drawers";
    r.subtitle = ctx.period;
    r.columns = {"", "Amount"};
    if (drawers.empty()) {
        r.note("No drawer has been opened.");
        return r;
    }
    const TaxRates &rates = ctx.settings.tax;
    for (const DrawerSession &d : drawers) {
        Money cash;
        std::int64_t checks = 0;
        for (const Check &c : closed) {
            if (c.drawerSession == d.id) {
                cash += cashIntoDrawer(c, rates);
                ++checks;
            }
        }
        r.section(d.name);
        r.line({"Opened by " + d.openedBy, ctx.clock(d.openedAt)});
        r.line({"Starting cash", ctx.money(d.startingCash)});
        r.line({"Cash sales (" + count(checks) + " checks)", ctx.money(cash)});
        for (const CashMovement &m : d.movements) {
            const std::string what = m.kind == CashMovement::Kind::PaidIn ? "Paid in"
                                     : m.kind == CashMovement::Kind::TipPayout ? "Tips paid out"
                                                                               : "Paid out";
            r.line({what + (m.reason.empty() ? "" : ": " + m.reason), ctx.money(m.effect())});
        }
        const Money expected = d.open() ? d.startingCash + cash + d.movementsTotal() : d.expected;
        r.total({"Expected in drawer", ctx.money(expected)});
        if (!d.open()) {
            r.line({"Counted by " + d.closedBy, ctx.money(d.counted)});
            const Money diff = d.overShort();
            r.total({diff.cents() < 0 ? "Short" : diff.cents() > 0 ? "Over" : "Balanced", ctx.money(diff)});
        }
    }
    return r;
}

Report tipsReport(const std::map<std::string, TipShare> &shares, const ReportContext &ctx)
{
    Report r;
    r.id = "tips";
    r.title = "Tips";
    r.subtitle = ctx.period;
    const bool pooling = !ctx.settings.tipOuts.empty();
    r.columns = pooling ? std::vector<std::string>{"Staff", "Card tips", "Gratuity", "Tipped out", "From pool", "Paid out", "Owed"}
                        : std::vector<std::string>{"Server", "Card tips", "Gratuity", "Paid out", "Owed"};
    TipShare all;
    for (const auto &[id, x] : shares) {
        if (x.earned.cents() == 0 && x.fromPool.cents() == 0 && x.paid.cents() == 0)
            continue;
        if (pooling)
            r.line({x.name, ctx.money(x.tips), ctx.money(x.gratuity), ctx.money(x.tipOut), ctx.money(x.fromPool),
                    ctx.money(x.paid), ctx.money(x.owed())});
        else
            r.line({x.name, ctx.money(x.tips), ctx.money(x.gratuity), ctx.money(x.paid), ctx.money(x.owed())});
        all.tips += x.tips;
        all.gratuity += x.gratuity;
        all.earned += x.earned;
        all.tipOut += x.tipOut;
        all.fromPool += x.fromPool;
        all.paid += x.paid;
    }
    if (all.earned.cents() == 0 && all.paid.cents() == 0) {
        r.note("No tips yet.");
        return r;
    }
    if (pooling)
        r.total({"All staff", ctx.money(all.tips), ctx.money(all.gratuity), ctx.money(all.tipOut),
                 ctx.money(all.fromPool), ctx.money(all.paid), ctx.money(all.owed())});
    else
        r.total({"All staff", ctx.money(all.tips), ctx.money(all.gratuity), ctx.money(all.paid), ctx.money(all.owed())});
    for (const PosSettings::TipOut &t : ctx.settings.tipOuts)
        r.note("Tip-out: " + std::to_string(t.percentBp / 100) + (t.percentBp % 100 ? "." + std::to_string(t.percentBp % 100) : "")
               + "% of " + (t.basis == "sales" ? "sales" : "tips") + " to " + t.role + "s, split by hours.");
    return r;
}

std::map<std::string, TipShare> tipShares(const std::vector<Check> &closed, const std::vector<DrawerSession> &drawers,
                                          const PosSettings &settings, const std::vector<Employee> &employees,
                                          const std::map<std::string, double> &hours)
{
    std::map<std::string, TipShare> out;
    std::map<std::string, Money> sales;
    for (const Check &c : closed) {
        const Totals t = c.totals(settings.tax);
        TipShare &s = out[c.serverId];
        s.name = c.serverName;
        s.tips += t.tips;
        s.gratuity += t.gratuity;
        s.earned += t.tips + t.gratuity;
        sales[c.serverId] += t.subtotal;
    }
    std::map<std::string, const Employee *> byId;
    for (const Employee &e : employees) {
        byId[e.id] = &e;
        if (out.contains(e.id))
            out[e.id].name = e.name;
    }
    const auto roleOf = [&](const std::string &id) { return byId.contains(id) ? byId[id]->role : std::string(); };

    for (const PosSettings::TipOut &rule : settings.tipOuts) {
        // Who shares this pool: that role, worked today.
        std::vector<std::pair<std::string, double>> crew;
        double crewHours = 0;
        for (const auto &[id, h] : hours) {
            if (h > 0 && roleOf(id) == rule.role) {
                crew.emplace_back(id, h);
                crewHours += h;
            }
        }
        if (crew.empty())
            continue;   // nobody to tip out to today
        Money pool;
        for (auto &[id, s] : out) {
            if (roleOf(id) == rule.role)
                continue;
            const Money base = rule.basis == "sales" ? sales[id] : s.earned;
            Money share = base.percent(rule.percentBp);
            const Money left = s.earned - s.tipOut;
            if (share > left)
                share = left;   // never more than they took in
            if (share.cents() <= 0)
                continue;
            s.tipOut += share;
            pool += share;
        }
        // Split by hours; the cents left over go to whoever worked longest.
        Money given;
        std::string longest;
        double most = -1;
        for (const auto &[id, h] : crew) {
            const Money part = Money::fromCents(std::int64_t(double(pool.cents()) * h / crewHours));
            TipShare &s = out[id];
            if (s.name.empty() && byId.contains(id))
                s.name = byId[id]->name;
            s.fromPool += part;
            given += part;
            if (h > most) {
                most = h;
                longest = id;
            }
        }
        out[longest].fromPool += pool - given;
    }
    for (const DrawerSession &d : drawers) {
        for (const CashMovement &m : d.movements) {
            if (m.kind == CashMovement::Kind::TipPayout) {
                TipShare &s = out[m.employeeId];
                if (s.name.empty())
                    s.name = byId.contains(m.employeeId) ? byId[m.employeeId]->name : m.employeeId;
                s.paid += m.amount;
            }
        }
    }
    return out;
}

Report accountsReport(const std::vector<GiftCard> &cards, const std::vector<CustomerRecord> &customers,
                      std::int64_t since, const ReportContext &ctx)
{
    Report r;
    r.id = "accounts";
    r.title = "Gift Cards & Accounts";
    r.subtitle = ctx.period;
    r.columns = {"", "Count", "Amount"};

    std::int64_t sold = 0, spent = 0, live = 0;
    Money soldAmount, spentAmount, outstanding;
    for (const GiftCard &g : cards) {
        for (const LedgerEntry &e : g.history) {
            if (e.at < since)
                continue;
            if (e.kind == "sale") {
                ++sold;
                soldAmount += e.amount;
            } else if (e.kind == "reopen") {   // a sale taken back
                --sold;
                soldAmount += e.amount;
            } else if (e.kind == "spend") {
                ++spent;
                spentAmount -= e.amount;
            } else if (e.kind == "refund") {   // a card payment removed
                --spent;
                spentAmount -= e.amount;
            }
        }
        if (g.balance.cents() > 0) {
            ++live;
            outstanding += g.balance;
        }
    }
    r.section("Gift cards");
    r.line({"Sold and reloaded", count(sold), ctx.money(soldAmount)});
    r.line({"Spent", count(spent), ctx.money(spentAmount)});
    r.total({"Still on cards (owed by the store)", count(live), ctx.money(outstanding)});

    r.section("House accounts");
    Money charged, paid, owed;
    std::vector<const CustomerRecord *> owing;
    for (const CustomerRecord &c : customers) {
        for (const LedgerEntry &e : c.account) {
            if (e.at < since)
                continue;
            if (e.kind == "payment")
                paid -= e.amount;
            else
                charged += e.amount;   // charges, less any taken back
        }
        if (c.houseAccount && c.accountBalance.cents() != 0) {
            owing.push_back(&c);
            owed += c.accountBalance;
        }
    }
    r.line({"Charged", "", ctx.money(charged)});
    r.line({"Paid", "", ctx.money(paid)});
    std::ranges::sort(owing, [](const CustomerRecord *a, const CustomerRecord *b) {
        return a->accountBalance > b->accountBalance;
    });
    for (const CustomerRecord *c : owing)
        r.line({(c->name.empty() ? c->phone : c->name) + (c->accountLimit.cents() > 0 && c->accountBalance >= c->accountLimit
                                                             ? "  (at the limit)" : ""),
                "", ctx.money(c->accountBalance)});
    r.total({"Owed to the store", count(std::int64_t(owing.size())), ctx.money(owed)});
    return r;
}

Report kitchenReport(const std::vector<const Check *> &checks, int lateMinutes, const ReportContext &ctx)
{
    Report r;
    r.id = "kitchen";
    r.title = "Kitchen Times";
    r.subtitle = ctx.period;
    r.columns = {"", "Tickets", "Average", "Longest", "Late"};

    // A ticket: one send from one check to one station.
    struct Ticket { const Check *check; std::int64_t sentAt; std::string station; std::int64_t madeAt = 0; bool done = true; };
    std::map<std::tuple<std::int64_t, std::int64_t, std::string>, Ticket> tickets;
    for (const Check *c : checks) {
        for (const OrderLine &l : c->lines) {
            if (!l.sent || l.voided || l.isComment() || l.isGiftCard())
                continue;
            const std::string station = l.printer.empty() ? "kitchen" : l.printer;
            Ticket &t = tickets.try_emplace({c->id, l.sentAt, station}, Ticket{c, l.sentAt, station}).first->second;
            if (l.made)
                t.madeAt = std::max(t.madeAt, l.madeAt);
            else
                t.done = false;
        }
    }
    const auto minutes = [](std::int64_t ms) {
        const std::int64_t s = ms / 1000;
        return std::to_string(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + std::to_string(s % 60);
    };
    struct Times { std::int64_t count = 0, total = 0, longest = 0, late = 0, waiting = 0; };
    std::map<std::string, Times> byStation;
    std::vector<const Ticket *> made;
    for (const auto &[key, t] : tickets) {
        Times &s = byStation[t.station];
        if (!t.done || t.madeAt == 0) {
            ++s.waiting;
            continue;
        }
        const std::int64_t took = t.madeAt - t.sentAt;
        ++s.count;
        s.total += took;
        s.longest = std::max(s.longest, took);
        if (took > std::int64_t(lateMinutes) * 60'000)
            ++s.late;
        made.push_back(&t);
    }
    if (byStation.empty()) {
        r.note("No orders have gone to the kitchen yet.");
        return r;
    }
    r.section("By station (sent to made)");
    Times all;
    for (const auto &[station, s] : byStation) {
        std::string name = station;
        if (!name.empty())
            name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
        r.line({name, count(s.count), s.count ? minutes(s.total / s.count) : "-", s.count ? minutes(s.longest) : "-",
                count(s.late)});
        all.count += s.count;
        all.total += s.total;
        all.longest = std::max(all.longest, s.longest);
        all.late += s.late;
        all.waiting += s.waiting;
    }
    r.total({"All", count(all.count), all.count ? minutes(all.total / all.count) : "-",
             all.count ? minutes(all.longest) : "-", count(all.late)});
    if (all.waiting)
        r.note(std::to_string(all.waiting) + (all.waiting == 1 ? " ticket is" : " tickets are") + " still being made.");
    r.note("Late: over " + std::to_string(lateMinutes) + " minutes (Store Settings).");

    std::ranges::sort(made, [](const Ticket *a, const Ticket *b) { return a->madeAt - a->sentAt > b->madeAt - b->sentAt; });
    if (!made.empty()) {
        r.section("Slowest tickets");
        for (std::size_t i = 0; i < std::min<std::size_t>(5, made.size()); ++i) {
            const Ticket *t = made[i];
            r.line({t->check->label + " #" + std::to_string(t->check->id) + " (" + t->station + ")", "",
                    ctx.clock(t->sentAt), minutes(t->madeAt - t->sentAt), t->check->rush ? "rush" : ""});
        }
    }
    return r;
}

namespace {
// "$1,234.50", "-$3.00", "12", "70.4%" -> the number; nullopt if none.
std::optional<double> numberIn(const std::string &text)
{
    std::string digits;
    bool any = false;
    for (char ch : text) {
        if ((ch >= '0' && ch <= '9') || ch == '.') {
            digits += ch;
            any = any || ch != '.';
        } else if (ch == '-' && digits.empty()) {
            digits += ch;
        }
    }
    if (!any)
        return std::nullopt;
    try {
        return std::stod(digits);
    } catch (...) {
        return std::nullopt;
    }
}
} // namespace

Report compareReports(const Report &now, const Report &before, const std::string &beforeLabel)
{
    Report r = now;
    if (r.columns.empty())
        r.columns = {"", "Amount"};
    r.columns.push_back(beforeLabel);
    r.columns.push_back("Change");
    for (ReportRow &row : r.rows) {
        if (row.kind != ReportRow::Kind::Line && row.kind != ReportRow::Kind::Total)
            continue;
        if (row.cells.size() < 2)
            row.cells.resize(2);
        std::string was = "-";
        for (const ReportRow &old : before.rows) {
            if (old.kind == row.kind && !old.cells.empty() && old.cells.front() == row.cells.front()) {
                was = old.cells.back();
                break;
            }
        }
        // Rows line up with the value columns: pad short ones.
        while (row.cells.size() < now.columns.size())
            row.cells.push_back("");
        std::string change;
        const auto a = numberIn(row.cells.back());
        const auto b = numberIn(was);
        if (a && b && *b != 0) {
            const double pct = (*a - *b) / std::abs(*b) * 100.0;
            char buf[32];
            std::snprintf(buf, sizeof buf, "%+.1f%%", pct);
            change = buf;
        } else if (a && b && *a == *b) {
            change = "0.0%";
        } else if (a && was != "-" && b && *b == 0) {
            change = "new";
        }
        row.cells.push_back(was);
        row.cells.push_back(change.empty() ? "-" : change);
    }
    r.subtitle = now.subtitle + " vs " + beforeLabel;
    return r;
}

Report tableTurns(const std::vector<Check> &closed, const ReportContext &ctx)
{
    Report r;
    r.id = "turns";
    r.title = "Table Turns";
    r.subtitle = ctx.period;
    r.columns = {"", "Checks", "Avg minutes", "Avg check", "Per guest"};
    struct Tally { std::int64_t checks = 0, minutes = 0, guests = 0; Money total; };
    std::map<int, Tally> bySize;   // 1-2, 3-4, 5-6, 7+ (by the first of each)
    std::map<std::string, Tally> byTable;
    Tally all;
    for (const Check &c : closed) {
        if (c.type != CheckType::DineIn || c.closedAt <= c.openedAt)
            continue;
        const std::int64_t minutes = (c.closedAt - c.openedAt) / 60'000;
        const Money total = c.totals(ctx.settings.tax).total;
        const int band = c.guests <= 2 ? 1 : c.guests <= 4 ? 3 : c.guests <= 6 ? 5 : 7;
        for (Tally *t : {&bySize[band], &byTable[c.label], &all}) {
            ++t->checks;
            t->minutes += minutes;
            t->guests += std::max(1, c.guests);
            t->total += total;
        }
    }
    if (all.checks == 0) {
        r.note("No dine-in checks closed yet.");
        return r;
    }
    const auto line = [&](const std::string &label, const Tally &t, bool total) {
        std::vector<std::string> cells{label, count(t.checks), std::to_string(t.minutes / t.checks),
                                       ctx.money(Money::fromCents(t.total.cents() / t.checks)),
                                       ctx.money(Money::fromCents(t.total.cents() / std::max<std::int64_t>(1, t.guests)))};
        if (total)
            r.total(cells);
        else
            r.line(cells);
    };
    r.section("By party size");
    for (const auto &[band, t] : bySize) {
        const std::string label = band == 1 ? "1 - 2 guests" : band == 3 ? "3 - 4 guests" : band == 5 ? "5 - 6 guests" : "7 or more";
        line(label, t, false);
    }
    r.section("By table");
    for (const auto &[table, t] : byTable)
        line(table, t, false);
    line("All tables", all, true);
    return r;
}

} // namespace vt::core
