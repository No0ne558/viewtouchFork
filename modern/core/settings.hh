#pragma once

#include "core/check.hh"
#include "core/tax.hh"

#include <string>
#include <vector>

namespace vt::core {

// Store-wide POS settings (M4 adds an admin screen for them).
struct PosSettings {
    std::string storeName = "ViewTouch";
    std::string currencySymbol = "$";
    TaxRates tax;
    std::vector<Tender> tenders;

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
