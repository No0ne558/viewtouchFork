#pragma once

#include "core/money.hh"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace vt::core {

// Something the kitchen uses up: buns, patties, oz of coffee... Stock goes
// down as recipes are sent to the kitchen and back up on a void; a manager
// counts or receives it in Manager -> Inventory.
struct Ingredient {
    std::string id;
    std::string name;
    std::string unit = "each";   // each, oz, lb, slice...
    double onHand = 0;
    double lowAt = 0;            // low-stock warning at or below this
    Money cost;                  // per unit, for food cost
    std::string vendor;          // who it usually comes from (a Vendor id)

    bool low() const { return onHand <= lowAt; }
    bool operator==(const Ingredient &) const = default;
};

// Who the store buys from (Manager -> Vendors).
struct Vendor {
    std::string id;
    std::string name;
    std::string phone;
    std::string account;   // the store's account number with them
    std::string note;      // delivery days, the rep...
    bool operator==(const Vendor &) const = default;
};

// A delivery received (Manager -> Inventory -> Receive a Delivery): what came,
// at what cost. Receiving adds it to stock and updates each ingredient's cost.
struct Delivery {
    std::int64_t id = 0;
    std::int64_t at = 0;
    std::string vendorId;
    std::string vendorName;
    std::string invoice;   // their invoice number
    std::string by;
    struct Line {
        std::string ingredientId;
        std::string name;
        std::string unit;
        double qty = 0;
        Money unitCost;
        Money total() const { return Money::fromCents(std::llround(double(unitCost.cents()) * qty)); }
        bool operator==(const Line &) const = default;
    };
    std::vector<Line> lines;

    Money total() const
    {
        Money t;
        for (const Line &l : lines)
            t += l.total();
        return t;
    }
    bool operator==(const Delivery &) const = default;
};

} // namespace vt::core
