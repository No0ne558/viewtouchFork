#pragma once

#include <cstdint>
#include <set>
#include <algorithm>
#include <string>
#include <vector>

namespace vt::core {

// Permissions replace the legacy "employee id 1 is superuser, 2 is editor".
namespace perm {
inline constexpr const char *Order = "order";            // take orders
inline constexpr const char *Settle = "check.settle";    // take payments, close checks
inline constexpr const char *Void = "order.void";        // void items already sent
inline constexpr const char *Manager = "manager";        // manager pages
inline constexpr const char *EditLayout = "layout.edit"; // edit pages
inline constexpr const char *Discount = "check.discount"; // discounts and comps
} // namespace perm

// Every permission, for the per-person settings.
inline constexpr const char *AllPermissions[] = {perm::Order, perm::Settle, perm::Discount, perm::Void,
                                                 perm::Manager, perm::EditLayout};

// Built-in roles. M4 makes these editable.
std::set<std::string> permissionsForRole(const std::string &role);

struct Employee {
    std::string id;
    std::string name;
    std::string role = "server";   // server | bartender | cashier | host | busser | manager | admin
    // PINs are stored hashed (see app::hashPin); never the digits.
    std::string pinSalt;
    std::string pinHash;
    bool active = true;
    // Practice only: their checks never reach the kitchen, sales or stock.
    bool training = false;
    // Cash handling for this person: "serverBank" (own bank), "drawer" (the
    // terminal's drawer), or empty for the store's setting.
    std::string language;          // "en", "es"...; empty: the store's
    std::string cashMode;
    // Checking out with checks still open: "closeChecks" (not allowed),
    // "anyTime" (allowed), or empty for the store's setting.
    std::string checkout;

    // Per-person changes to the role's permissions.
    std::set<std::string> allow;
    std::set<std::string> deny;

    std::set<std::string> permissions() const
    {
        std::set<std::string> out = permissionsForRole(role);
        out.insert(allow.begin(), allow.end());
        for (const std::string &p : deny)
            out.erase(p);
        return out;
    }
    bool can(const std::string &permission) const { return permissions().contains(permission); }
    bool operator==(const Employee &) const = default;
};

// One shift on the clock. clockOut == 0 means still clocked in.
struct TimePunch {
    std::int64_t id = 0;
    std::string employeeId;
    std::int64_t clockIn = 0;    // epoch ms
    std::int64_t clockOut = 0;
    struct Break {
        std::int64_t start = 0;
        std::int64_t end = 0;    // 0: still on it
        bool operator==(const Break &) const = default;
    };
    std::vector<Break> breaks;

    bool open() const { return clockOut == 0; }
    bool onBreak() const { return !breaks.empty() && breaks.back().end == 0; }
    // Time on the clock / on breaks, up to `now` for what is still running.
    std::int64_t spanMs(std::int64_t now) const { return std::max<std::int64_t>(0, (open() ? now : clockOut) - clockIn); }
    std::int64_t breakMs(std::int64_t now) const
    {
        std::int64_t ms = 0;
        for (const Break &b : breaks)
            ms += std::max<std::int64_t>(0, (b.end == 0 ? (open() ? now : clockOut) : b.end) - b.start);
        return ms;
    }
    // Paid time: breaks count only if they are paid.
    std::int64_t workedMs(std::int64_t now, bool paidBreaks) const
    {
        return paidBreaks ? spanMs(now) : std::max<std::int64_t>(0, spanMs(now) - breakMs(now));
    }
    bool operator==(const TimePunch &) const = default;
};

// A scheduled shift. With PosSettings::scheduleRequired, staff clock in
// only from a little before a shift until it ends.
struct Shift {
    std::int64_t id = 0;
    std::string employeeId;
    std::int64_t start = 0;   // epoch ms
    std::int64_t end = 0;
    std::string note;         // "patio", "close"...

    double hours() const { return double(end - start) / 3'600'000.0; }
    bool operator==(const Shift &) const = default;
};

} // namespace vt::core
