#include "core/report.hh"

#include <algorithm>
#include <cmath>
#include <map>
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
                   const ReportContext &ctx)
{
    Report r;
    r.id = "labor";
    r.title = "Labor";
    r.subtitle = ctx.period;
    r.columns = {"Employee", "In", "Out", "Hours"};

    std::map<std::string, std::string> names;
    for (const Employee &e : employees)
        names[e.id] = e.name;

    std::int64_t totalMs = 0;
    std::vector<TimePunch> sorted = punches;
    std::ranges::sort(sorted, {}, &TimePunch::clockIn);
    for (const TimePunch &p : sorted) {
        const std::int64_t end = p.open() ? ctx.now : p.clockOut;
        const std::int64_t ms = std::max<std::int64_t>(0, end - p.clockIn);
        totalMs += ms;
        const auto it = names.find(p.employeeId);
        r.line({it == names.end() ? p.employeeId : it->second, ctx.clock(p.clockIn),
                p.open() ? "on clock" : ctx.clock(p.clockOut), hours(ms)});
    }
    if (sorted.empty())
        r.note("Nobody has clocked in.");
    else
        r.total({"Total hours", "", "", hours(totalMs)});
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

Report tipsReport(const std::vector<Check> &closed, const std::vector<DrawerSession> &drawers,
                  const ReportContext &ctx)
{
    Report r;
    r.id = "tips";
    r.title = "Tips";
    r.subtitle = ctx.period;
    r.columns = {"Server", "Card tips", "Gratuity", "Paid out", "Owed"};

    struct Tally { std::string name; Money tips, gratuity, paid; };
    std::map<std::string, Tally> byServer;
    for (const Check &c : closed) {
        const Totals t = c.totals(ctx.settings.tax);
        if (t.tips.cents() == 0 && t.gratuity.cents() == 0)
            continue;
        Tally &x = byServer[c.serverId];
        x.name = c.serverName;
        x.tips += t.tips;
        x.gratuity += t.gratuity;
    }
    for (const DrawerSession &d : drawers) {
        for (const CashMovement &m : d.movements) {
            if (m.kind == CashMovement::Kind::TipPayout) {
                Tally &x = byServer[m.employeeId];
                if (x.name.empty())
                    x.name = m.employeeId;
                x.paid += m.amount;
            }
        }
    }
    Tally all;
    for (const auto &[id, x] : byServer) {
        r.line({x.name, ctx.money(x.tips), ctx.money(x.gratuity), ctx.money(x.paid),
                ctx.money(x.tips + x.gratuity - x.paid)});
        all.tips += x.tips;
        all.gratuity += x.gratuity;
        all.paid += x.paid;
    }
    if (byServer.empty())
        r.note("No tips yet.");
    else
        r.total({"All staff", ctx.money(all.tips), ctx.money(all.gratuity), ctx.money(all.paid),
                 ctx.money(all.tips + all.gratuity - all.paid)});
    return r;
}

} // namespace vt::core
