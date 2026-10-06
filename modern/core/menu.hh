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
    // The kitchen station that makes it (a PosSettings::stations id: grill,
    // fryer...); empty = its printer's screen.
    std::string station;
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
    // On the self-order kiosk: a line about it and a photo (an image file on
    // the computer showing the kiosk); or not shown there at all.
    std::string description;
    std::string image;
    // Its number for ringing it in by number (a PLU: "104"); empty: none.
    std::string number;
    bool kioskHide = false;
    // Prices by order type (0: the regular price), and who may not discount it.
    Money takeoutPrice;
    Money deliveryPrice;
    bool noDiscount = false;        // no discounts or comps
    bool noStaffDiscount = false;   // no staff meal discount (alcohol, say)
    // Priced by weight (the original's "Priced By Weight"): `price` is per
    // weightUnit, and ordering it asks for the weight.
    bool byWeight = false;
    std::string weightUnit = "lb";
    // The original's "Menu Item + Substitute": ordered on its own at `price`,
    // or with Sub in place of part of another item at substitutePrice
    // (a house salad instead of fries, + $3.00).
    bool substitute = false;
    Money substitutePrice;
    // The original's "Event Admission": tickets to an event with this many
    // seats (0 = not an event), on this date (epoch ms, 0 = none given).
    // ticketsSoldBefore: sold on earlier business days (End of Day adds them).
    int ticketCapacity = 0;
    std::int64_t eventAt = 0;
    int ticketsSoldBefore = 0;

    Money priceDuring(const std::string &mealPeriod) const
    {
        const auto it = periodPrices.find(mealPeriod);
        return it == periodPrices.end() ? price : it->second;
    }
    // Takeout and delivery have their own price when set (delivery falls
    // back on the takeout price); otherwise the meal period's.
    Money priceFor(const std::string &mealPeriod, bool takeout, bool delivery) const
    {
        if (delivery && deliveryPrice.cents() > 0)
            return deliveryPrice;
        if ((takeout || delivery) && takeoutPrice.cents() > 0)
            return takeoutPrice;
        return priceDuring(mealPeriod);
    }
    bool operator==(const MenuItem &) const = default;
};

// One choice in a modifier group: "Medium Rare", "Onion Rings +1.50".
struct ModifierOption {
    std::string name;
    Money price;
    std::string kitchenName;   // what the kitchen sees instead
    bool kitchenHide = false;  // "No dressing": nothing for the kitchen
    // The menu item it is (ModifierGroup::menuItems): it uses up the item's
    // stock and can't be chosen while it's sold out.
    std::string itemId;

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
    // Each option is the menu item of that name. That's how a combo works:
    // "Burger Combo" asks for a side and a drink from the menu.
    bool menuItems = false;

    bool operator==(const ModifierGroup &) const = default;
};

// No / Lite / Extra ... applied to the next item or modifier ordered.
// Sub: the next item replaces part of the one before it, at its
// substitute price (MenuItem::substitute).
enum class Qualifier { None, No, Lite, Extra, Side, Only, Double, Sub };

std::string toString(Qualifier q);
Qualifier qualifierFromString(const std::string &s);   // unknown -> None
// Display prefix, e.g. "No " for No. Empty for None.
std::string qualifierPrefix(Qualifier q);
// Price after the qualifier: No/Lite are free, Double charges twice.
Money qualifiedPrice(Money unit, Qualifier q);

} // namespace vt::core
