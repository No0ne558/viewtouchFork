/**
 * @file employee.hpp
 * @brief Employee data structure
 */

#pragma once

#include "core/types.hpp"
#include <QString>
#include <set>

namespace vt2 {

/**
 * @brief An employee who uses the POS
 */
class Employee {
public:
    Employee() = default;
    
    EmployeeId id() const { return id_; }
    void setId(EmployeeId id) { id_ = id; }
    
    QString firstName() const { return firstName_; }
    void setFirstName(const QString& name) { firstName_ = name; }
    
    QString lastName() const { return lastName_; }
    void setLastName(const QString& name) { lastName_ = name; }
    
    QString fullName() const { return firstName_ + " " + lastName_; }
    
    QString pin() const { return pin_; }
    void setPin(const QString& pin) { pin_ = pin; }
    
    EmployeeRole role() const { return role_; }
    void setRole(EmployeeRole role) { role_ = role; }
    
    bool hasPermission(Permission perm) const;
    void grantPermission(Permission perm);
    void revokePermission(Permission perm);
    
    bool active() const { return active_; }
    void setActive(bool active) { active_ = active; }
    
private:
    EmployeeId id_{0};
    QString firstName_;
    QString lastName_;
    QString pin_;
    EmployeeRole role_ = EmployeeRole::Server;
    std::set<Permission> permissions_;
    bool active_ = true;
};

} // namespace vt2
