#pragma once

#include "core/check.hh"
#include "core/report.hh"
#include "core/settings.hh"
#include "print/document.hh"

#include <functional>
#include <string>
#include <vector>

namespace vt::print {

struct TicketContext {
    const core::PosSettings &settings;
    std::function<std::string(std::int64_t)> dateTime;   // epoch ms -> "Sep 29, 2026 10:31 AM"
    std::function<std::string(std::int64_t)> time;       // epoch ms -> "10:31 AM"
    std::int64_t now = 0;

    std::string money(Money m) const;
};

// Customer receipt (legacy SubCheck::PrintReceipt).
Document receipt(const core::Check &check, const TicketContext &ctx);

// Kitchen / bar ticket for the given lines (legacy Check::PrintWorkOrder).
// `voids` prints the lines as cancelled.
Document kitchenTicket(const core::Check &check, const std::vector<core::OrderLine> &lines,
                       const std::string &station, bool voids, const TicketContext &ctx);

// Any report, e.g. the end-of-day summary.
Document reportTicket(const core::Report &report, const TicketContext &ctx);

// Just open the cash drawer ("No Sale").
Document drawerKick();

} // namespace vt::print
