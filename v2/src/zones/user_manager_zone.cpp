/**
 * @file user_manager_zone.cpp
 * @brief User Manager zone implementation
 */

#include "user_manager_zone.hpp"
#include "data/employee_store.hpp"
#include "core/logger.hpp"
#include <QHeaderView>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QResizeEvent>

namespace vt2 {

UserManagerZone::UserManagerZone(EmployeeStore* store, QWidget* parent)
    : Zone(ZoneType::Manager, parent)
    , employeeStore_(store) {
    setZoneName("User Manager");
    setupUI();
    refreshEmployeeList();
}

void UserManagerZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    // Title
    titleLabel_ = new QLabel("👥 User Manager - Manage Employees");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    // Employee table
    employeeTable_ = new QTableWidget();
    employeeTable_->setColumnCount(5);
    employeeTable_->setHorizontalHeaderLabels({"ID", "Name", "PIN", "Role", "Active"});
    employeeTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    employeeTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    employeeTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    employeeTable_->horizontalHeader()->setStretchLastSection(true);
    employeeTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(employeeTable_, &QTableWidget::cellClicked, this, &UserManagerZone::onEmployeeSelected);
    mainLayout_->addWidget(employeeTable_, 1);

    // Button row
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(15);

    addBtn_ = new QPushButton("➕ Add Employee");
    connect(addBtn_, &QPushButton::clicked, this, &UserManagerZone::onAddEmployee);
    buttonLayout->addWidget(addBtn_);

    editBtn_ = new QPushButton("✏️ Edit Selected");
    editBtn_->setEnabled(false);
    connect(editBtn_, &QPushButton::clicked, this, &UserManagerZone::onEditEmployee);
    buttonLayout->addWidget(editBtn_);

    deleteBtn_ = new QPushButton("🗑️ Delete Selected");
    deleteBtn_->setEnabled(false);
    connect(deleteBtn_, &QPushButton::clicked, this, &UserManagerZone::onDeleteEmployee);
    buttonLayout->addWidget(deleteBtn_);

    buttonLayout->addStretch();

    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &UserManagerZone::backRequested);
    buttonLayout->addWidget(backBtn_);

    mainLayout_->addLayout(buttonLayout);

    updateSizes();
}

void UserManagerZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void UserManagerZone::updateSizes() {
    int w = width();
    int h = height();
    int baseFontSize = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #2c3e50; padding: %2px;")
        .arg(baseFontSize * 2).arg(baseFontSize / 2));
    
    employeeTable_->setStyleSheet(QString(R"(
        QTableWidget {
            background-color: white;
            border: 2px solid #bdc3c7;
            border-radius: 8px;
            font-size: %1px;
        }
        QTableWidget::item { padding: 8px; }
        QTableWidget::item:selected { background-color: #3498db; color: white; }
        QHeaderView::section {
            background-color: #34495e;
            color: white;
            padding: %2px;
            font-weight: bold;
            font-size: %1px;
            border: none;
        }
    )").arg(baseFontSize).arg(baseFontSize));

    QString btnStyle = QString(R"(
        QPushButton {
            border: none;
            border-radius: 8px;
            padding: %1px %2px;
            font-size: %3px;
            font-weight: bold;
            color: white;
        }
    )").arg(baseFontSize).arg(baseFontSize * 2).arg(baseFontSize);

    addBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #27ae60; } QPushButton:hover { background-color: #2ecc71; }");
    editBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #3498db; } QPushButton:hover { background-color: #5dade2; } QPushButton:disabled { background-color: #95a5a6; }");
    deleteBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #e74c3c; } QPushButton:hover { background-color: #ec7063; } QPushButton:disabled { background-color: #95a5a6; }");
    backBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #7f8c8d; } QPushButton:hover { background-color: #95a5a6; }");
}

void UserManagerZone::refreshEmployeeList() {
    employeeTable_->setRowCount(0);
    
    if (!employeeStore_) return;
    
    auto employees = employeeStore_->getAllEmployees(true);  // Include inactive
    
    for (const auto& emp : employees) {
        int row = employeeTable_->rowCount();
        employeeTable_->insertRow(row);
        
        employeeTable_->setItem(row, 0, new QTableWidgetItem(QString::number(emp.id().value)));
        employeeTable_->setItem(row, 1, new QTableWidgetItem(emp.fullName()));
        employeeTable_->setItem(row, 2, new QTableWidgetItem(emp.pin()));
        
        QString roleStr;
        switch (emp.role()) {
            case EmployeeRole::Server: roleStr = "Server"; break;
            case EmployeeRole::Bartender: roleStr = "Bartender"; break;
            case EmployeeRole::Cashier: roleStr = "Cashier"; break;
            case EmployeeRole::Host: roleStr = "Host"; break;
            case EmployeeRole::Manager: roleStr = "Manager"; break;
            case EmployeeRole::Admin: roleStr = "Admin"; break;
            default: roleStr = "Unknown"; break;
        }
        employeeTable_->setItem(row, 3, new QTableWidgetItem(roleStr));
        employeeTable_->setItem(row, 4, new QTableWidgetItem(emp.active() ? "Yes" : "No"));
        
        // Color inactive rows
        if (!emp.active()) {
            for (int col = 0; col < 5; ++col) {
                employeeTable_->item(row, col)->setBackground(QColor(240, 240, 240));
                employeeTable_->item(row, col)->setForeground(QColor(150, 150, 150));
            }
        }
    }
    
    selectedRow_ = -1;
    editBtn_->setEnabled(false);
    deleteBtn_->setEnabled(false);
}

void UserManagerZone::onEmployeeSelected(int row, int column) {
    Q_UNUSED(column);
    selectedRow_ = row;
    editBtn_->setEnabled(true);
    deleteBtn_->setEnabled(true);
}

bool UserManagerZone::showEmployeeDialog(Employee& emp, bool isNew) {
    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? "Add Employee" : "Edit Employee");
    dialog.setMinimumSize(400, 400);
    dialog.setStyleSheet("QDialog { background-color: #ecf0f1; }");

    auto* layout = new QFormLayout(&dialog);
    layout->setSpacing(15);
    layout->setContentsMargins(20, 20, 20, 20);

    auto* firstNameEdit = new QLineEdit(emp.firstName());
    firstNameEdit->setStyleSheet("padding: 10px; font-size: 16px; border: 2px solid #bdc3c7; border-radius: 5px;");
    layout->addRow("First Name:", firstNameEdit);

    auto* lastNameEdit = new QLineEdit(emp.lastName());
    lastNameEdit->setStyleSheet("padding: 10px; font-size: 16px; border: 2px solid #bdc3c7; border-radius: 5px;");
    layout->addRow("Last Name:", lastNameEdit);

    auto* pinEdit = new QLineEdit(emp.pin());
    pinEdit->setStyleSheet("padding: 10px; font-size: 16px; border: 2px solid #bdc3c7; border-radius: 5px;");
    pinEdit->setMaxLength(6);
    layout->addRow("PIN:", pinEdit);

    auto* roleCombo = new QComboBox();
    roleCombo->addItem("Server", static_cast<int>(EmployeeRole::Server));
    roleCombo->addItem("Bartender", static_cast<int>(EmployeeRole::Bartender));
    roleCombo->addItem("Cashier", static_cast<int>(EmployeeRole::Cashier));
    roleCombo->addItem("Host", static_cast<int>(EmployeeRole::Host));
    roleCombo->addItem("Manager", static_cast<int>(EmployeeRole::Manager));
    roleCombo->addItem("Admin", static_cast<int>(EmployeeRole::Admin));
    roleCombo->setCurrentIndex(static_cast<int>(emp.role()));
    roleCombo->setStyleSheet("padding: 10px; font-size: 16px;");
    layout->addRow("Role:", roleCombo);

    auto* activeCombo = new QComboBox();
    activeCombo->addItem("Active", true);
    activeCombo->addItem("Inactive", false);
    activeCombo->setCurrentIndex(emp.active() ? 0 : 1);
    activeCombo->setStyleSheet("padding: 10px; font-size: 16px;");
    layout->addRow("Status:", activeCombo);

    auto* btnLayout = new QHBoxLayout();
    auto* saveBtn = new QPushButton("💾 Save");
    saveBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #27ae60; color: white; border: none;
            border-radius: 8px; padding: 12px 25px; font-size: 16px; font-weight: bold;
        }
        QPushButton:hover { background-color: #2ecc71; }
    )");

    auto* cancelBtn = new QPushButton("❌ Cancel");
    cancelBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #e74c3c; color: white; border: none;
            border-radius: 8px; padding: 12px 25px; font-size: 16px; font-weight: bold;
        }
        QPushButton:hover { background-color: #ec7063; }
    )");

    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addRow(btnLayout);

    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        if (firstNameEdit->text().isEmpty() || pinEdit->text().isEmpty()) {
            QMessageBox::warning(this, "Error", "First name and PIN are required!");
            return false;
        }
        
        // Check for duplicate PIN
        if (employeeStore_->isPinInUse(pinEdit->text(), emp.id())) {
            QMessageBox::warning(this, "Error", "PIN is already in use!");
            return false;
        }
        
        emp.setFirstName(firstNameEdit->text());
        emp.setLastName(lastNameEdit->text());
        emp.setPin(pinEdit->text());
        emp.setRole(static_cast<EmployeeRole>(roleCombo->currentData().toInt()));
        emp.setActive(activeCombo->currentData().toBool());
        return true;
    }
    return false;
}

void UserManagerZone::onAddEmployee() {
    VT_INFO("Adding new employee");
    
    Employee newEmp;
    newEmp.setRole(EmployeeRole::Server);
    newEmp.setActive(true);
    
    if (showEmployeeDialog(newEmp, true)) {
        EmployeeId newId = employeeStore_->addEmployee(newEmp);
        refreshEmployeeList();
        VT_INFO("Added employee: {} (ID: {})", newEmp.fullName().toStdString(), newId.value);
    }
}

void UserManagerZone::onEditEmployee() {
    if (selectedRow_ < 0) return;
    
    EmployeeId empId{static_cast<uint32_t>(employeeTable_->item(selectedRow_, 0)->text().toUInt())};
    auto empOpt = employeeStore_->findById(empId);
    
    if (empOpt) {
        Employee editEmp = *empOpt;
        if (showEmployeeDialog(editEmp, false)) {
            employeeStore_->updateEmployee(editEmp);
            refreshEmployeeList();
            VT_INFO("Updated employee: {}", editEmp.fullName().toStdString());
        }
    }
}

void UserManagerZone::onDeleteEmployee() {
    if (selectedRow_ < 0) return;
    
    QString name = employeeTable_->item(selectedRow_, 1)->text();
    EmployeeId empId{static_cast<uint32_t>(employeeTable_->item(selectedRow_, 0)->text().toUInt())};
    
    auto reply = QMessageBox::question(this, "Confirm Delete",
        QString("Are you sure you want to delete employee '%1'?\n\n"
                "This will permanently remove this employee from the system.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        employeeStore_->removeEmployee(empId);
        refreshEmployeeList();
        VT_INFO("Deleted employee: {}", name.toStdString());
    }
}

} // namespace vt2
