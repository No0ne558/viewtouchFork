#pragma once

#include "core/money.hh"

#include <cstdint>
#include <string>

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

// A cash drawer from opening (with a starting bank) until it is counted.
struct DrawerSession {
    std::int64_t id = 0;
    std::string name = "Drawer 1";
    std::int64_t openedAt = 0;
    std::string openedBy;
    Money startingCash;
    std::int64_t closedAt = 0;
    std::string closedBy;
    Money expected;   // starting cash + net cash sales, fixed when counted
    Money counted;

    bool open() const { return closedAt == 0; }
    Money overShort() const { return counted - expected; }
    bool operator==(const DrawerSession &) const = default;
};

} // namespace vt::core
