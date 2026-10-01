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
    std::string drawer;   // "yes" / "no": has a cash drawer; empty = the store's setting
    // A paired device (tablet, remote terminal): the id it connects as and
    // its secret key (base64). Empty for terminals set up by name only.
    std::string id;
    std::string key;
    std::int64_t pairedAt = 0;
    // Pages to show: "phone" (phone versions), "standard", or empty to
    // decide from the screen size.
    std::string screen;

    bool operator==(const TerminalConfig &) const = default;
};

// A part of the day. Index pages whose mealPeriod is this id are the ones the
// "index" jump opens while it lasts. A period runs from its start until the
// next one starts; before the first start of the day the last one continues
// (a dinner that runs past midnight).
struct MealPeriod {
    std::string id;
    std::string name;
    int start = 0;   // minutes after midnight

    bool operator==(const MealPeriod &) const = default;
};

inline std::vector<MealPeriod> defaultMealPeriods()
{
    return {{"breakfast", "Breakfast", 4 * 60}, {"lunch", "Lunch", 11 * 60}, {"dinner", "Dinner", 16 * 60}};
}

// The id of the period running at `minute` (0-1439); empty if there are none.
inline std::string mealPeriodAt(const std::vector<MealPeriod> &periods, int minute)
{
    const MealPeriod *current = nullptr;
    const MealPeriod *latest = nullptr;
    for (const MealPeriod &p : periods) {
        if (!latest || p.start > latest->start)
            latest = &p;
        if (p.start <= minute && (!current || p.start > current->start))
            current = &p;
    }
    return current ? current->id : latest ? latest->id : std::string();
}

// Where cash goes. With a drawer per terminal, cash sales go in the drawer
// of the terminal that closes the check. With server banks, whoever takes
// the cash keeps it in their own bank (no drawer) and turns it in when they
// check out, so any terminal can be used by anyone.
enum class CashMode { TerminalDrawer, ServerBank };

inline std::string toString(CashMode m) { return m == CashMode::ServerBank ? "serverBank" : "drawer"; }
inline CashMode cashModeFromString(const std::string &s)
{
    return s == "serverBank" ? CashMode::ServerBank : CashMode::TerminalDrawer;
}

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
    // This store's server, as terminals know it (found again after an
    // address change). Made once, on first start.
    std::string serverId;
    std::vector<MealPeriod> mealPeriods = defaultMealPeriods();
    std::vector<ModifierGroup> modifierGroups;

    const ModifierGroup *modifierGroup(const std::string &id) const
    {
        for (const ModifierGroup &g : modifierGroups) {
            if (g.id == id)
                return &g;
        }
        return nullptr;
    }
    CashMode cashMode = CashMode::TerminalDrawer;
    // Whether terminals have a cash drawer, unless set per terminal.
    bool terminalsHaveDrawer = true;
    // Time and overtime: paid breaks count as worked time; overtime after
    // this many hours in a day / in the pay week (0: no such rule); the pay
    // week starts on this weekday (0 Sunday ... 6 Saturday).
    bool paidBreaks = false;
    int overtimeDailyHours = 0;
    int overtimeWeeklyHours = 40;
    int weekStartsOn = 0;
    // Whether people must close (or hand over) their checks before they
    // check out their bank, unless set per employee.
    bool checkoutNeedsClosedChecks = true;
    // A second folder every backup is also copied to (a USB drive, a
    // network share), so one dead disk can't take the backups with it.
    std::string backupCopyDir;
    // Waitlist: quoted minutes for each party ahead, and the text sent when
    // a table is ready ({name}, {store}). textWebhook: where texts are sent
    // (a JSON POST of {to, message}; empty: no texting).
    int waitMinutesPerParty = 10;
    // Tip choices offered to the guest (percent of the check before gratuity).
    std::vector<int> tipPercents{15, 18, 20, 25};
    std::string tableReadyText = "Hi {name}, your table at {store} is ready! Please come to the host stand.";
    std::string textWebhook;

    bool hasDrawer(const std::string &terminal) const
    {
        for (const TerminalConfig &t : terminals) {
            if (t.name == terminal && !t.drawer.empty())
                return t.drawer == "yes";
        }
        return terminalsHaveDrawer;
    }

    std::string receiptPrinterFor(const std::string &terminal) const
    {
        for (const TerminalConfig &t : terminals) {
            if (t.name == terminal && !t.receiptPrinter.empty())
                return t.receiptPrinter;
        }
        return "receipt";
    }

    const TerminalConfig *pairedTerminal(const std::string &id) const
    {
        for (const TerminalConfig &t : terminals) {
            if (!id.empty() && t.id == id && !t.key.empty())
                return &t;
        }
        return nullptr;
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
