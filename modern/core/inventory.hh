#pragma once

#include "core/money.hh"

#include <string>

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

    bool low() const { return onHand <= lowAt; }
    bool operator==(const Ingredient &) const = default;
};

} // namespace vt::core
