#pragma once

#include "core/money.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace vt::core {

// One business day, from opening until End of Day. Closed checks belong to
// the day they were closed in (like the legacy archives).
struct BusinessDay {
    std::int64_t id = 0;
    std::int64_t openedAt = 0;   // epoch ms
    std::int64_t closedAt = 0;   // 0 while open

    bool open() const { return closedAt == 0; }
    bool operator==(const BusinessDay &) const = default;
};

// Cash in or out of a drawer that is not a sale.
struct CashMovement {
    enum class Kind { Payout, PaidIn, TipPayout };
    std::int64_t id = 0;
    Kind kind = Kind::Payout;
    Money amount;              // always positive; kind gives the direction
    std::string reason;
    std::string by;            // who did it
    std::string employeeId;    // TipPayout: whose tips
    std::int64_t at = 0;

    // Effect on the cash in the drawer.
    Money effect() const { return kind == Kind::PaidIn ? amount : -amount; }
    bool operator==(const CashMovement &) const = default;
};

std::string toString(CashMovement::Kind k);
CashMovement::Kind cashMovementKindFromString(const std::string &s);

// A cash drawer from opening (with a starting bank) until it is counted.
// Each terminal has its own drawer, named after the terminal.
struct DrawerSession {
    std::int64_t id = 0;
    std::string name = "Drawer 1";
    std::string terminal;      // the terminal it belongs to
    std::int64_t openedAt = 0;
    std::string openedBy;
    Money startingCash;
    std::int64_t closedAt = 0;
    std::string closedBy;
    Money expected;   // starting cash + net cash sales + movements, fixed when counted
    Money counted;
    std::vector<CashMovement> movements;
    std::int64_t nextMovementId = 1;

    Money movementsTotal() const
    {
        Money m;
        for (const CashMovement &c : movements)
            m += c.effect();
        return m;
    }

    bool open() const { return closedAt == 0; }
    Money overShort() const { return counted - expected; }
    bool operator==(const DrawerSession &) const = default;
};

} // namespace vt::core
