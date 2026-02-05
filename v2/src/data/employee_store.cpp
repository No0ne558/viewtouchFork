/**
 * @file employee_store.cpp
 * @brief Employee data storage implementation
 */

#include "data/employee_store.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace vt2 {

EmployeeStore::EmployeeStore(QObject* parent)
    : QObject(parent)
{
    spdlog::debug("EmployeeStore initialized");
}

EmployeeId EmployeeStore::addEmployee(const Employee& employee) {
    Employee emp = employee;
    
    // Assign ID if not set
    if (emp.id().value == 0) {
        emp.setId(EmployeeId{nextId_++});
    } else if (emp.id().value >= nextId_) {
        nextId_ = emp.id().value + 1;
    }
    
    // Validate PIN isn't in use
    if (isPinInUse(emp.pin())) {
        spdlog::error("Cannot add employee: PIN already in use");
        return EmployeeId{0};
    }
    
    EmployeeId id = emp.id();
    employees_[id] = std::move(emp);
    
    // Update PIN index
    if (!employees_[id].pin().isEmpty()) {
        pinIndex_[employees_[id].pin()] = id;
    }
    
    spdlog::info("Added employee ID {} ({})", id.value, 
        employees_[id].fullName().toStdString());
    emit employeeAdded(id);
    
    return id;
}

bool EmployeeStore::updateEmployee(const Employee& employee) {
    auto it = employees_.find(employee.id());
    if (it == employees_.end()) {
        spdlog::warn("Cannot update employee {}: not found", employee.id().value);
        return false;
    }
    
    // Check PIN uniqueness if changed
    if (it->second.pin() != employee.pin() && 
        isPinInUse(employee.pin(), employee.id())) {
        spdlog::error("Cannot update employee: PIN already in use");
        return false;
    }
    
    // Remove old PIN from index
    if (!it->second.pin().isEmpty()) {
        pinIndex_.erase(it->second.pin());
    }
    
    // Update employee
    it->second = employee;
    
    // Add new PIN to index
    if (!employee.pin().isEmpty()) {
        pinIndex_[employee.pin()] = employee.id();
    }
    
    spdlog::info("Updated employee ID {}", employee.id().value);
    emit employeeUpdated(employee.id());
    
    return true;
}

bool EmployeeStore::removeEmployee(EmployeeId id) {
    auto it = employees_.find(id);
    if (it == employees_.end()) {
        return false;
    }
    
    // Remove from PIN index
    if (!it->second.pin().isEmpty()) {
        pinIndex_.erase(it->second.pin());
    }
    
    employees_.erase(it);
    spdlog::info("Removed employee ID {}", id.value);
    emit employeeRemoved(id);
    
    return true;
}

std::optional<Employee> EmployeeStore::findById(EmployeeId id) const {
    auto it = employees_.find(id);
    if (it != employees_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<Employee> EmployeeStore::findByPin(const QString& pin) const {
    auto it = pinIndex_.find(pin);
    if (it != pinIndex_.end()) {
        auto empIt = employees_.find(it->second);
        if (empIt != employees_.end()) {
            return empIt->second;
        }
    }
    return std::nullopt;
}

std::vector<Employee> EmployeeStore::getAllEmployees(bool includeInactive) const {
    std::vector<Employee> result;
    result.reserve(employees_.size());
    
    for (const auto& [id, emp] : employees_) {
        if (includeInactive || emp.active()) {
            result.push_back(emp);
        }
    }
    
    // Sort by name
    std::sort(result.begin(), result.end(), 
        [](const Employee& a, const Employee& b) {
            return a.lastName() < b.lastName() || 
                   (a.lastName() == b.lastName() && a.firstName() < b.firstName());
        });
    
    return result;
}

size_t EmployeeStore::activeCount() const {
    return std::count_if(employees_.begin(), employees_.end(),
        [](const auto& pair) { return pair.second.active(); });
}

bool EmployeeStore::isPinInUse(const QString& pin, EmployeeId excludeId) const {
    auto it = pinIndex_.find(pin);
    if (it == pinIndex_.end()) {
        return false;
    }
    return it->second != excludeId;
}

void EmployeeStore::loadDemoData() {
    clear();
    
    spdlog::info("Loading demo employee data");
    
    // Demo employees with simple PINs for testing
    Employee mgr;
    mgr.setFirstName("John");
    mgr.setLastName("Manager");
    mgr.setPin("1111");
    mgr.setRole(EmployeeRole::Manager);
    addEmployee(mgr);
    
    Employee server1;
    server1.setFirstName("Alice");
    server1.setLastName("Smith");
    server1.setPin("2222");
    server1.setRole(EmployeeRole::Server);
    addEmployee(server1);
    
    Employee server2;
    server2.setFirstName("Bob");
    server2.setLastName("Jones");
    server2.setPin("3333");
    server2.setRole(EmployeeRole::Server);
    addEmployee(server2);
    
    Employee bartender;
    bartender.setFirstName("Carol");
    bartender.setLastName("Davis");
    bartender.setPin("4444");
    bartender.setRole(EmployeeRole::Bartender);
    addEmployee(bartender);
    
    Employee cashier;
    cashier.setFirstName("Dave");
    cashier.setLastName("Wilson");
    cashier.setPin("5555");
    cashier.setRole(EmployeeRole::Cashier);
    addEmployee(cashier);
    
    Employee host;
    host.setFirstName("Eve");
    host.setLastName("Brown");
    host.setPin("6666");
    host.setRole(EmployeeRole::Host);
    addEmployee(host);
    
    // An inactive employee
    Employee inactive;
    inactive.setFirstName("Frank");
    inactive.setLastName("Old");
    inactive.setPin("9999");
    inactive.setRole(EmployeeRole::Server);
    inactive.setActive(false);
    addEmployee(inactive);
    
    spdlog::info("Loaded {} demo employees ({} active)", 
        employees_.size(), activeCount());
}

void EmployeeStore::clear() {
    employees_.clear();
    pinIndex_.clear();
    nextId_ = 100;
    spdlog::debug("EmployeeStore cleared");
}

void EmployeeStore::rebuildPinIndex() {
    pinIndex_.clear();
    for (const auto& [id, emp] : employees_) {
        if (!emp.pin().isEmpty()) {
            pinIndex_[emp.pin()] = id;
        }
    }
}

} // namespace vt2
