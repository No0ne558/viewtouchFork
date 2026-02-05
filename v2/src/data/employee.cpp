/**
 * @file employee.cpp
 */

#include "data/employee.hpp"

namespace vt2 {

bool Employee::hasPermission(Permission perm) const {
    // Admins have all permissions
    if (role_ == EmployeeRole::Admin) {
        return true;
    }
    
    // Managers have most permissions
    if (role_ == EmployeeRole::Manager) {
        if (perm != Permission::SystemSettings) {
            return true;
        }
    }
    
    return permissions_.contains(perm);
}

void Employee::grantPermission(Permission perm) {
    permissions_.insert(perm);
}

void Employee::revokePermission(Permission perm) {
    permissions_.erase(perm);
}

} // namespace vt2
