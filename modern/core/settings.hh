#pragma once

#include "core/check.hh"
#include "core/tax.hh"

#include <string>
#include <vector>

namespace vt::core {

// A ticket printer. Menu items name the printer their kitchen tickets go to
// ("kitchen", "bar"); receipts go to "receipt".
struct PrinterConfig {
    std::string id;
    std::string name;
    std::string type = "none";   // network | file | cups | none
    std::string host;            // network
    int port = 9100;             // network (raw / JetDirect)
    std::string path;            // file path, or CUPS queue name
    std::string format;          // escpos | text; empty = escpos for network, else text
    int width = 42;              // characters per line
    bool cutter = true;
    bool drawerKick = false;     // cash drawer is wired to this printer

    std::string effectiveFormat() const { return !format.empty() ? format : type == "network" ? "escpos" : "text"; }
    bool operator==(const PrinterConfig &) const = default;
};

// Per-terminal setup: which printer takes its receipts (and opens its
// drawer). Terminals not listed use the "receipt" printer.
struct TerminalConfig {
    std::string name;
    std::string receiptPrinter;

    bool operator==(const TerminalConfig &) const = default;
};

// Store-wide POS settings, edited on the manager's admin screens.
struct PosSettings {
    std::string storeName = "ViewTouch";
    std::string currencySymbol = "$";
    TaxRates tax;
    std::vector<Tender> tenders;
    std::vector<PrinterConfig> printers;
    std::string receiptHeader;   // lines under the store name
    std::string receiptFooter;
    // Auto-gratuity: added to dine-in checks with at least this many guests.
    std::int64_t gratuityBp = 0;   // 0 = off; 1800 = 18%
    int gratuityMinGuests = 6;
    std::vector<TerminalConfig> terminals;

    std::string receiptPrinterFor(const std::string &terminal) const
    {
        for (const TerminalConfig &t : terminals) {
            if (t.name == terminal && !t.receiptPrinter.empty())
                return t.receiptPrinter;
        }
        return "receipt";
    }

    const PrinterConfig *printer(const std::string &id) const
    {
        for (const PrinterConfig &p : printers) {
            if (p.id == id)
                return &p;
        }
        return nullptr;
    }

    const Tender *tender(const std::string &id) const
    {
        for (const Tender &t : tenders) {
            if (t.id == id)
                return &t;
        }
        return nullptr;
    }

    bool operator==(const PosSettings &) const = default;
};

} // namespace vt::core
