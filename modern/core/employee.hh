#pragma once

#include <cstdint>
#include <set>
#include <string>

namespace vt::core {

// Permissions replace the legacy "employee id 1 is superuser, 2 is editor".
namespace perm {
inline constexpr const char *Order = "order";            // take orders
inline constexpr const char *Settle = "check.settle";    // take payments, close checks
inline constexpr const char *Void = "order.void";        // void items already sent
inline constexpr const char *Manager = "manager";        // manager pages
inline constexpr const char *EditLayout = "layout.edit"; // edit pages
} // namespace perm

// Built-in roles. M4 makes these editable.
std::set<std::string> permissionsForRole(const std::string &role);

struct Employee {
    std::string id;
    std::string name;
    std::string role = "server";   // server | cashier | manager | admin
    // PINs are stored hashed (see app::hashPin); never the digits.
    std::string pinSalt;
    std::string pinHash;
    bool active = true;
    // Cash handling for this person: "serverBank" (own bank), "drawer" (the
    // terminal's drawer), or empty for the store's setting.
    std::string cashMode;

    bool can(const std::string &permission) const { return permissionsForRole(role).contains(permission); }
    bool operator==(const Employee &) const = default;
};

// One shift on the clock. clockOut == 0 means still clocked in.
struct TimePunch {
    std::int64_t id = 0;
    std::string employeeId;
    std::int64_t clockIn = 0;    // epoch ms
    std::int64_t clockOut = 0;

    bool open() const { return clockOut == 0; }
    bool operator==(const TimePunch &) const = default;
};

} // namespace vt::core
