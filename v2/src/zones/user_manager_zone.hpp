/**
 * @file user_manager_zone.hpp
 * @brief User Manager zone for managing employees
 */

#pragma once

#include "ui/zone.hpp"
#include "data/employee.hpp"
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>

namespace vt2 {

class EmployeeStore;

class UserManagerZone : public Zone {
    Q_OBJECT

public:
    explicit UserManagerZone(EmployeeStore* store, QWidget* parent = nullptr);
    void refreshEmployeeList();

signals:
    void backRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onAddEmployee();
    void onEditEmployee();
    void onDeleteEmployee();
    void onEmployeeSelected(int row, int column);

private:
    void setupUI();
    void updateSizes();
    bool showEmployeeDialog(Employee& emp, bool isNew);

    EmployeeStore* employeeStore_{nullptr};
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QTableWidget* employeeTable_{nullptr};
    QPushButton* addBtn_{nullptr};
    QPushButton* editBtn_{nullptr};
    QPushButton* deleteBtn_{nullptr};
    QPushButton* backBtn_{nullptr};
    int selectedRow_{-1};
};

} // namespace vt2
