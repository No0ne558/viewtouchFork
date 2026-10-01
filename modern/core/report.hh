#pragma once

#include "core/check.hh"
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

Report salesSummary(const std::vector<Check> &closed, const ReportContext &ctx);
Report itemSales(const std::vector<Check> &closed, const std::vector<MenuItem> &menu, const ReportContext &ctx);
Report serverSales(const std::vector<Check> &closed, const ReportContext &ctx);
// Today's punches with breaks and worked hours, then each person's hours
// today and this pay week with overtime (`earlier`: this week's punches
// from earlier days).
Report laborReport(const std::vector<TimePunch> &punches, const std::vector<Employee> &employees,
                   const ReportContext &ctx, const std::vector<TimePunch> &earlier = {});
Report drawerReport(const std::vector<DrawerSession> &drawers, const std::vector<Check> &closed,
                    const ReportContext &ctx);
// Net sales by the hour checks closed in.
Report hourlySales(const std::vector<Check> &closed, const ReportContext &ctx);
// Net sales per family (category), with its share of the day.
Report categorySales(const std::vector<Check> &closed, const std::vector<MenuItem> &menu, const ReportContext &ctx);
// Every void, discount, reopen, transfer, move and merge, with who and when.
Report auditReport(const std::vector<const Check *> &checks, const ReportContext &ctx);
Report tipsReport(const std::vector<Check> &closed, const std::vector<DrawerSession> &drawers,
                  const ReportContext &ctx);

} // namespace vt::core
