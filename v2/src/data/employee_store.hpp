/**
 * @file employee_store.hpp
 * @brief In-memory employee data storage
 */

#pragma once

#include "data/employee.hpp"
#include <QObject>
#include <vector>
#include <optional>
#include <unordered_map>

namespace vt2 {

/**
 * @brief Manages employee data storage
 * 
 * Currently stores employees in-memory. Can be extended to
 * persist to database or file storage.
 */
class EmployeeStore : public QObject {
    Q_OBJECT
    
public:
    explicit EmployeeStore(QObject* parent = nullptr);
    ~EmployeeStore() override = default;
    
    /**
     * @brief Add a new employee
     * @return The assigned employee ID
     */
    EmployeeId addEmployee(const Employee& employee);
    
    /**
     * @brief Update an existing employee
     * @return true if employee was found and updated
     */
    bool updateEmployee(const Employee& employee);
    
    /**
     * @brief Remove an employee
     * @return true if employee was found and removed
     */
    bool removeEmployee(EmployeeId id);
    
    /**
     * @brief Find employee by ID
     */
    std::optional<Employee> findById(EmployeeId id) const;
    
    /**
     * @brief Find employee by PIN
     * @note Does NOT return the superuser - that's handled separately
     */
    std::optional<Employee> findByPin(const QString& pin) const;
    
    /**
     * @brief Get all employees (for admin screens)
     * @param includeInactive Whether to include inactive employees
     * @note Does NOT include the hidden superuser
     */
    std::vector<Employee> getAllEmployees(bool includeInactive = false) const;
    
    /**
     * @brief Get count of active employees
     */
    size_t activeCount() const;
    
    /**
     * @brief Check if a PIN is already in use
     * @note Does NOT check the superuser PIN
     */
    bool isPinInUse(const QString& pin, EmployeeId excludeId = EmployeeId{0}) const;
    
    /**
     * @brief Load demo/test employees
     */
    void loadDemoData();
    
    /**
     * @brief Clear all employees
     */
    void clear();
    
signals:
    void employeeAdded(EmployeeId id);
    void employeeUpdated(EmployeeId id);
    void employeeRemoved(EmployeeId id);
    
private:
    uint32_t nextId_{100};  // Start at 100, 0 is reserved for superuser
    std::unordered_map<EmployeeId, Employee> employees_;
    std::unordered_map<QString, EmployeeId> pinIndex_;  // Quick PIN lookup
    
    void rebuildPinIndex();
};

} // namespace vt2
