/**
 * @file balance_tills_zone.hpp
 * @brief Balance Tills zone for server cash reconciliation
 */

#pragma once

#include "ui/zone.hpp"
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>

namespace vt2 {

class EmployeeStore;

class BalanceTillsZone : public Zone {
    Q_OBJECT

public:
    explicit BalanceTillsZone(EmployeeStore* store, QWidget* parent = nullptr);
    void refreshTillsList();

signals:
    void backRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onBalanceSelected();
    void onPrintReport();

private:
    void setupUI();
    void updateSizes();

    EmployeeStore* employeeStore_{nullptr};
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QTableWidget* tillsTable_{nullptr};
    QPushButton* balanceBtn_{nullptr};
    QPushButton* printBtn_{nullptr};
    QPushButton* backBtn_{nullptr};
    int selectedRow_{-1};
};

} // namespace vt2
