#pragma once

#include "core/check.hh"
#include "core/customer.hh"
#include "core/day.hh"
#include "core/employee.hh"
#include "core/menu.hh"
#include "core/settings.hh"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vt::core {

// A report as plain rows, rendered by the reportView widget on screen and by
// the ticket formatter on paper (legacy Report / ReportEntry).
struct ReportRow {
    enum class Kind { Line, Section, Total, Note };
    Kind kind = Kind::Line;
    std::vector<std::string> cells;   // first cell left-aligned, the rest right

    bool operator==(const ReportRow &) const = default;
};

struct Report {
    std::string id;
    std::string title;
    std::string subtitle;
    std::vector<std::string> columns;
    std::vector<ReportRow> rows;

    void section(const std::string &title) { rows.push_back({ReportRow::Kind::Section, {title}}); }
    void line(std::vector<std::string> cells) { rows.push_back({ReportRow::Kind::Line, std::move(cells)}); }
    void total(std::vector<std::string> cells) { rows.push_back({ReportRow::Kind::Total, std::move(cells)}); }
    void note(const std::string &text) { rows.push_back({ReportRow::Kind::Note, {text}}); }
    // First line whose first cell is `label`; empty if none. For tests.
    std::vector<std::string> find(const std::string &label) const;
};

struct ReportContext {
    const PosSettings &settings;
    std::string period;                                // "Today", "Tue Sep 29"...
    std::function<std::string(std::int64_t)> clock;   // epoch ms -> "10:31 AM"
    std::int64_t now = 0;
    std::function<int(std::int64_t)> hourOf;          // epoch ms -> local hour 0-23
    std::function<int(std::int64_t)> dayOf;           // epoch ms -> local day (any number per date)
    std::int64_t weekStart = 0;                       // start of the current pay week

    std::string money(Money m) const;
};

// Net cash a closed check left in its drawer.
Money cashIntoDrawer(const Check &check, const TaxRates &rates);
// Starting bank + net cash of the checks closed into this drawer + pay-ins
// - pay-outs.
Money expectedCash(const DrawerSession &drawer, const std::vector<Check> &closed, const TaxRates &rates);

// Card tips and gratuity an employee earned on these checks, less the tips
// already paid out to them from any drawer.
Money tipsOwed(const std::string &employeeId, const std::vector<Check> &closed,
               const std::vector<DrawerSession> &drawers, const TaxRates &rates);

// Everyone's tips after tip-outs: what they earned on their checks, what
// they tipped out (PosSettings::tipOuts), their share of the pools (by
// hours worked, `hours`: employee id -> hours today), what was paid out.
struct TipShare {
    std::string name;
    Money tips, gratuity;   // card tips and party gratuity on their checks
    Money earned, tipOut, fromPool, paid;
    Money owed() const { return earned - tipOut + fromPool - paid; }
};
std::map<std::string, TipShare> tipShares(const std::vector<Check> &closed, const std::vector<DrawerSession> &drawers,
                                          const PosSettings &settings, const std::vector<Employee> &employees,
                                          const std::map<std::string, double> &hours);

Report salesSummary(const std::vector<Check> &closed, const ReportContext &ctx);
Report itemSales(const std::vector<Check> &closed, const std::vector<MenuItem> &menu, const ReportContext &ctx);
Report serverSales(const std::vector<Check> &closed, const ReportContext &ctx);
// Today's punches with breaks and worked hours, then each person's hours
// today and this pay week with overtime (`earlier`: this week's punches
// from earlier days), then what today's labor costs (each shift at the pay
// it was clocked in at, overtime at time and a half) against `netSales`.
Report laborReport(const std::vector<TimePunch> &punches, const std::vector<Employee> &employees,
                   const ReportContext &ctx, const std::vector<TimePunch> &earlier = {}, Money netSales = {});
Report drawerReport(const std::vector<DrawerSession> &drawers, const std::vector<Check> &closed,
                    const ReportContext &ctx);
// Net sales by the hour checks closed in.
Report hourlySales(const std::vector<Check> &closed, const ReportContext &ctx);
// Net sales per family (category), with its share of the day.
Report categorySales(const std::vector<Check> &closed, const std::vector<MenuItem> &menu, const ReportContext &ctx);
// Every void, discount, reopen, transfer, move and merge, with who and when.
Report auditReport(const std::vector<const Check *> &checks, const ReportContext &ctx);
// Gift cards sold and spent since `since`, what is still on cards (owed by
// the store), and house account charges, payments and balances.
Report accountsReport(const std::vector<GiftCard> &cards, const std::vector<CustomerRecord> &customers,
                      std::int64_t since, const ReportContext &ctx);
// Kitchen ticket times by station: from sent to made (bumped) - how many,
// the average, the longest, how many were late - and the slowest tickets.
Report kitchenReport(const std::vector<const Check *> &checks, int lateMinutes, const ReportContext &ctx);
Report tipsReport(const std::map<std::string, TipShare> &shares, const ReportContext &ctx);
// Dine-in table turns: how long checks were open (seated to paid), by
// party size and by table, with the average check and per guest.
Report tableTurns(const std::vector<Check> &closed, const ReportContext &ctx);
// `now` with two more columns: the same row's last value in `before`, and
// the change in percent (rows matched by kind and first cell).
Report compareReports(const Report &now, const Report &before, const std::string &beforeLabel);

} // namespace vt::core
