#pragma once

#include "core/money.hh"
#include "core/tax.hh"

#include <string>

namespace vt::core {

// Something that can be sold. Modifiers ("Medium Rare", "Onion Rings") are
// menu items too; they attach to the order line they modify.
struct MenuItem {
    std::string id;
    std::string name;
    std::string family;      // reporting group: burgers, drinks, sides...
    Money price;
    TaxClass taxClass = TaxClass::Food;
    bool isModifier = false;
    std::string printer;     // kitchen routing target (used by printing, M4)
    bool available = true;   // false = "86'd", cannot be ordered

    bool operator==(const MenuItem &) const = default;
};

// No / Lite / Extra ... applied to the next item or modifier ordered.
enum class Qualifier { None, No, Lite, Extra, Side, Only, Double };

std::string toString(Qualifier q);
Qualifier qualifierFromString(const std::string &s);   // unknown -> None
// Display prefix, e.g. "No " for No. Empty for None.
std::string qualifierPrefix(Qualifier q);
// Price after the qualifier: No/Lite are free, Double charges twice.
Money qualifiedPrice(Money unit, Qualifier q);

} // namespace vt::core
