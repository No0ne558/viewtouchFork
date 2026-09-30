#pragma once

#include "app/pos_service.hh"
#include "print/spooler.hh"
#include "print/tickets.hh"

#include <QString>

namespace vt::print {

// PosPrinter that formats tickets and hands them to the spooler.
//
// Routing: kitchen lines go to the printer named by each menu item
// ("kitchen", "bar"...), falling back to "kitchen"; receipts, reports and
// drawer kicks to the terminal's receipt printer (falling back to "receipt",
// and for kicks to any printer with a drawer attached).
// Relative file paths are resolved against `outputDir`.
class TicketPrinter : public app::PosPrinter {
public:
    TicketPrinter(PrintSpooler &spooler, QString outputDir);

    void printKitchen(const core::PosSettings &settings, const core::Check &check,
                      const std::vector<core::OrderLine> &lines, bool voids) override;
    void printReceipt(const core::PosSettings &settings, const core::Check &check,
                      const std::string &printerId) override;
    void printReport(const core::PosSettings &settings, const core::Report &report,
                     const std::string &printerId) override;
    void openDrawer(const core::PosSettings &settings, const std::string &printerId) override;

    // For tests: the clock used on tickets.
    void setClock(std::function<std::int64_t()> now) { now_ = std::move(now); }

private:
    void send(const core::PosSettings &settings, const core::PrinterConfig &printer, const Document &doc,
              const QString &description);
    TicketContext context(const core::PosSettings &settings) const;

    PrintSpooler &spooler_;
    QString outputDir_;
    std::function<std::int64_t()> now_;
};

} // namespace vt::print
