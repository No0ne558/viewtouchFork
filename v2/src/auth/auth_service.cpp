/**
 * @file auth_service.cpp
 * @brief Authentication service implementation
 */

#include "auth/auth_service.hpp"
#include <spdlog/spdlog.h>
#include <QRegularExpression>

namespace vt2 {

AuthService::AuthService(QObject* parent)
    : QObject(parent)
{
    spdlog::debug("AuthService initialized");
}

AuthResult AuthService::authenticate(const QString& pin) {
    AuthResult result;
    
    // Validate PIN format
    if (!isValidPinFormat(pin)) {
        result.message = "Invalid PIN format. PIN must be 1-5 digits.";
        spdlog::warn("Authentication failed: invalid PIN format");
        emit authenticationFailed(result.message);
        return result;
    }
    
    // Check for superuser first (hidden, not in any employee list)
    if (isSuperuserPin(pin)) {
        spdlog::info("Superuser authenticated");
        currentEmployee_ = createSuperuser();
        isSuperuser_ = true;
        result.success = true;
        result.isSuperuser = true;
        result.employeeId = SUPERUSER_ID;
        result.message = "Welcome, Superuser";
        emit userLoggedIn(currentEmployee_.get(), true);
        return result;
    }
    
    // Look up employee by PIN
    if (employeeLookup_) {
        auto employee = employeeLookup_(pin);
        if (employee.has_value()) {
            if (!employee->active()) {
                result.message = "Employee account is inactive.";
                spdlog::warn("Authentication failed: inactive employee {}", 
                    employee->id().value);
                emit authenticationFailed(result.message);
                return result;
            }
            
            spdlog::info("Employee {} authenticated", employee->id().value);
            currentEmployee_ = std::make_unique<Employee>(std::move(*employee));
            isSuperuser_ = false;
            result.success = true;
            result.employeeId = currentEmployee_->id();
            result.message = QString("Welcome, %1").arg(currentEmployee_->fullName());
            emit userLoggedIn(currentEmployee_.get(), false);
            return result;
        }
    }
    
    // No match found
    result.message = "Invalid PIN.";
    spdlog::warn("Authentication failed: no matching PIN");
    emit authenticationFailed(result.message);
    return result;
}

void AuthService::logout() {
    if (currentEmployee_) {
        spdlog::info("User {} logged out", 
            isSuperuser_ ? "Superuser" : std::to_string(currentEmployee_->id().value));
        currentEmployee_.reset();
        isSuperuser_ = false;
        emit userLoggedOut();
    }
}

bool AuthService::hasPermission(Permission perm) const {
    // Superuser has all permissions
    if (isSuperuser_) {
        return true;
    }
    
    // Check current employee's permissions
    if (currentEmployee_) {
        return currentEmployee_->hasPermission(perm);
    }
    
    return false;
}

bool AuthService::isValidPinFormat(const QString& pin) {
    if (pin.isEmpty()) {
        return false;
    }
    
    if (pin.length() < MIN_PIN_LENGTH || pin.length() > MAX_PIN_LENGTH) {
        return false;
    }
    
    // Must be all digits
    static QRegularExpression digitsOnly("^\\d+$");
    return digitsOnly.match(pin).hasMatch();
}

bool AuthService::isSuperuserPin(const QString& pin) const {
    return pin == SUPERUSER_PIN;
}

std::unique_ptr<Employee> AuthService::createSuperuser() {
    auto superuser = std::make_unique<Employee>();
    superuser->setId(SUPERUSER_ID);
    superuser->setFirstName("Super");
    superuser->setLastName("User");
    superuser->setPin(SUPERUSER_PIN);
    superuser->setRole(EmployeeRole::Admin);  // Highest built-in role
    superuser->setActive(true);
    
    // Grant all permissions explicitly (though Admin already has them)
    superuser->grantPermission(Permission::VoidItem);
    superuser->grantPermission(Permission::VoidCheck);
    superuser->grantPermission(Permission::Discount);
    superuser->grantPermission(Permission::Comps);
    superuser->grantPermission(Permission::OpenDrawer);
    superuser->grantPermission(Permission::CloseDay);
    superuser->grantPermission(Permission::EditMenu);
    superuser->grantPermission(Permission::EditEmployees);
    superuser->grantPermission(Permission::ViewReports);
    superuser->grantPermission(Permission::SystemSettings);
    
    return superuser;
}

} // namespace vt2
