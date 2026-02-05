/**
 * @file auth_service.hpp
 * @brief Authentication service for PIN-based login
 */

#pragma once

#include "core/types.hpp"
#include "data/employee.hpp"
#include <QString>
#include <QObject>
#include <optional>
#include <memory>
#include <vector>
#include <functional>

namespace vt2 {

/**
 * @brief Result of an authentication attempt
 */
struct AuthResult {
    bool success{false};
    QString message;
    std::optional<EmployeeId> employeeId;
    bool isSuperuser{false};
};

/**
 * @brief Authentication service handling PIN-based login
 * 
 * Features:
 * - PIN validation (1-5 digits)
 * - Hidden superuser with hardcoded PIN
 * - Employee lookup by PIN
 * - Session management
 */
class AuthService : public QObject {
    Q_OBJECT
    
public:
    // PIN constraints
    static constexpr int MIN_PIN_LENGTH = 1;
    static constexpr int MAX_PIN_LENGTH = 5;
    
    explicit AuthService(QObject* parent = nullptr);
    ~AuthService() override = default;
    
    /**
     * @brief Authenticate with a PIN
     * @param pin The entered PIN (1-5 digits)
     * @return AuthResult with success/failure and employee info
     */
    AuthResult authenticate(const QString& pin);
    
    /**
     * @brief Logout the current user
     */
    void logout();
    
    /**
     * @brief Check if someone is currently logged in
     */
    bool isLoggedIn() const { return currentEmployee_ != nullptr; }
    
    /**
     * @brief Get the currently logged in employee
     * @return Pointer to current employee or nullptr
     */
    const Employee* currentEmployee() const { return currentEmployee_.get(); }
    
    /**
     * @brief Check if current user is the superuser
     */
    bool isSuperuser() const { return isSuperuser_; }
    
    /**
     * @brief Check if current user has a specific permission
     */
    bool hasPermission(Permission perm) const;
    
    /**
     * @brief Validate PIN format (1-5 digits, numeric only)
     */
    static bool isValidPinFormat(const QString& pin);
    
    /**
     * @brief Set the employee lookup function
     * 
     * This allows the auth service to look up employees without
     * directly depending on the data storage layer.
     */
    using EmployeeLookup = std::function<std::optional<Employee>(const QString& pin)>;
    void setEmployeeLookup(EmployeeLookup lookup) { employeeLookup_ = std::move(lookup); }
    
signals:
    /**
     * @brief Emitted when a user logs in
     */
    void userLoggedIn(const Employee* employee, bool isSuperuser);
    
    /**
     * @brief Emitted when a user logs out
     */
    void userLoggedOut();
    
    /**
     * @brief Emitted when authentication fails
     */
    void authenticationFailed(const QString& reason);

private:
    // Superuser configuration - HARDCODED and HIDDEN
    static constexpr const char* SUPERUSER_PIN = "13524";
    static constexpr EmployeeId SUPERUSER_ID{0};  // Special ID
    
    /**
     * @brief Create the hidden superuser employee object
     */
    static std::unique_ptr<Employee> createSuperuser();
    
    /**
     * @brief Check if PIN matches the superuser
     */
    bool isSuperuserPin(const QString& pin) const;
    
    std::unique_ptr<Employee> currentEmployee_;
    bool isSuperuser_{false};
    EmployeeLookup employeeLookup_;
};

} // namespace vt2
