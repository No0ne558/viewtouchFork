#pragma once

#include "core/money.hh"
#include "core/tax.hh"

#include <map>
#include <string>
#include <vector>

namespace vt::core {

// What one of an item uses up (see Ingredient).
struct RecipeLine {
    std::string ingredientId;
    double quantity = 1;
    bool operator==(const RecipeLine &) const = default;
};

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
    // Choices asked for when it is ordered (ModifierGroup ids, in order).
    std::vector<std::string> modifierGroups;
    // A different price during a meal period (meal period id -> price),
    // e.g. dinner portions, or a Happy Hour period.
    std::map<std::string, Money> periodPrices;
    // What one uses up; when the stock runs short it is sold out by itself
    // (autoSoldOut) until restocked.
    std::vector<RecipeLine> recipe;
    bool autoSoldOut = false;
    // On the kitchen screen and kitchen tickets: a shorter name, a highlight
    // color, or nothing at all.
    std::string kitchenName;
    std::string kitchenColor;
    bool kitchenHide = false;

    Money priceDuring(const std::string &mealPeriod) const
    {
        const auto it = periodPrices.find(mealPeriod);
        return it == periodPrices.end() ? price : it->second;
    }
    bool operator==(const MenuItem &) const = default;
};

// One choice in a modifier group: "Medium Rare", "Onion Rings +1.50".
struct ModifierOption {
    std::string name;
    Money price;
    std::string kitchenName;   // what the kitchen sees instead
    bool kitchenHide = false;  // "No dressing": nothing for the kitchen

    bool operator==(const ModifierOption &) const = default;
};

// Choices asked for when an item is ordered: "Temperature" (exactly one),
// "Toppings" (up to three)... `min` choices are required; `max` is the most
// allowed (0 = any number; 1 = choosing another replaces the choice).
struct ModifierGroup {
    std::string id;
    std::string name;
    int min = 0;
    int max = 1;
    std::vector<ModifierOption> options;

    bool operator==(const ModifierGroup &) const = default;
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
