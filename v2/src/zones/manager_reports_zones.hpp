/**
 * @file manager_reports_zones.hpp
 * @brief Manager report zones for various business reports
 */

#pragma once

#include "ui/zone.hpp"
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QDateEdit>
#include <QLineEdit>
#include <QComboBox>

namespace vt2 {

// ============================================================================
// Menu Item Performance Zone
// ============================================================================
class MenuPerformanceZone : public Zone {
    Q_OBJECT
public:
    explicit MenuPerformanceZone(QWidget* parent = nullptr);
signals:
    void backRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void setupUI();
    void updateSizes();
    void loadSampleData();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QTableWidget* reportTable_{nullptr};
    QPushButton* backBtn_{nullptr};
};

// ============================================================================
// Today's Revenue Zone
// ============================================================================
class TodaysRevenueZone : public Zone {
    Q_OBJECT
public:
    explicit TodaysRevenueZone(QWidget* parent = nullptr);
    void refresh();
signals:
    void backRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void setupUI();
    void updateSizes();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLabel* salesLabel_{nullptr};
    QLabel* checksLabel_{nullptr};
    QLabel* avgCheckLabel_{nullptr};
    QLabel* tipsLabel_{nullptr};
    QLabel* laborLabel_{nullptr};
    QTableWidget* hourlyTable_{nullptr};
    QPushButton* refreshBtn_{nullptr};
    QPushButton* backBtn_{nullptr};
};

// ============================================================================
// Exceptional Transactions Zone
// ============================================================================
class ExceptionalTransactionsZone : public Zone {
    Q_OBJECT
public:
    explicit ExceptionalTransactionsZone(QWidget* parent = nullptr);
signals:
    void backRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void setupUI();
    void updateSizes();
    void loadSampleData();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QTableWidget* voidsTable_{nullptr};
    QTableWidget* compsTable_{nullptr};
    QTableWidget* discountsTable_{nullptr};
    QPushButton* backBtn_{nullptr};
};

// ============================================================================
// Franchise Traffic Zone
// ============================================================================
class FranchiseTrafficZone : public Zone {
    Q_OBJECT
public:
    explicit FranchiseTrafficZone(QWidget* parent = nullptr);
signals:
    void backRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void setupUI();
    void updateSizes();
    void loadSampleData();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QTableWidget* trafficTable_{nullptr};
    QLabel* summaryLabel_{nullptr};
    QPushButton* backBtn_{nullptr};
};

// ============================================================================
// Receipts Balance Zone
// ============================================================================
class ReceiptsBalanceZone : public Zone {
    Q_OBJECT
public:
    explicit ReceiptsBalanceZone(QWidget* parent = nullptr);
signals:
    void backRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void setupUI();
    void updateSizes();
    void loadSampleData();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QTableWidget* receiptsTable_{nullptr};
    QLabel* cashSummary_{nullptr};
    QLabel* cardSummary_{nullptr};
    QPushButton* recordDepositBtn_{nullptr};
    QPushButton* backBtn_{nullptr};
};

// ============================================================================
// Closed Check Summary Zone
// ============================================================================
class ClosedCheckSummaryZone : public Zone {
    Q_OBJECT
public:
    explicit ClosedCheckSummaryZone(QWidget* parent = nullptr);
signals:
    void backRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void setupUI();
    void updateSizes();
    void loadSampleData();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QDateEdit* dateSelect_{nullptr};
    QTableWidget* checksTable_{nullptr};
    QLabel* summaryLabel_{nullptr};
    QPushButton* backBtn_{nullptr};
};

// ============================================================================
// Review Guest Checks Zone
// ============================================================================
class ReviewGuestChecksZone : public Zone {
    Q_OBJECT
public:
    explicit ReviewGuestChecksZone(QWidget* parent = nullptr);
signals:
    void backRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private slots:
    void onSearchChanged(const QString& text);
    void onCheckSelected(int row, int col);
private:
    void setupUI();
    void updateSizes();
    void loadSampleData();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLineEdit* searchEdit_{nullptr};
    QTableWidget* checksTable_{nullptr};
    QLabel* detailLabel_{nullptr};
    QPushButton* reprintBtn_{nullptr};
    QPushButton* backBtn_{nullptr};
};

// ============================================================================
// Expenses View Zone
// ============================================================================
class ExpensesViewZone : public Zone {
    Q_OBJECT
public:
    explicit ExpensesViewZone(QWidget* parent = nullptr);
signals:
    void backRequested();
    void addExpenseRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void setupUI();
    void updateSizes();
    void loadSampleData();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QTableWidget* expensesTable_{nullptr};
    QLabel* totalLabel_{nullptr};
    QPushButton* addBtn_{nullptr};
    QPushButton* backBtn_{nullptr};
};

// ============================================================================
// Edit Menu Item Properties Zone
// ============================================================================
class EditMenuItemZone : public Zone {
    Q_OBJECT
public:
    explicit EditMenuItemZone(QWidget* parent = nullptr);
signals:
    void backRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private slots:
    void onItemSelected(int row, int col);
    void onSaveChanges();
private:
    void setupUI();
    void updateSizes();
    void loadSampleData();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QTableWidget* menuTable_{nullptr};
    QLineEdit* nameEdit_{nullptr};
    QLineEdit* priceEdit_{nullptr};
    QLineEdit* categoryEdit_{nullptr};
    QPushButton* saveBtn_{nullptr};
    QPushButton* backBtn_{nullptr};
    int selectedRow_{-1};
};

// ============================================================================
// Pay Captured Tips Zone
// ============================================================================
class PayCapturedTipsZone : public Zone {
    Q_OBJECT
public:
    explicit PayCapturedTipsZone(QWidget* parent = nullptr);
signals:
    void backRequested();
protected:
    void resizeEvent(QResizeEvent* event) override;
private slots:
    void onPaySelected();
    void onPayAll();
private:
    void setupUI();
    void updateSizes();
    void loadSampleData();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QTableWidget* tipsTable_{nullptr};
    QLabel* totalLabel_{nullptr};
    QPushButton* paySelectedBtn_{nullptr};
    QPushButton* payAllBtn_{nullptr};
    QPushButton* backBtn_{nullptr};
};

// ============================================================================
// Record Expense Zone
// ============================================================================
class RecordExpenseZone : public Zone {
    Q_OBJECT
public:
    explicit RecordExpenseZone(QWidget* parent = nullptr);
signals:
    void backRequested();
    void expenseRecorded();
protected:
    void resizeEvent(QResizeEvent* event) override;
private slots:
    void onSaveExpense();
private:
    void setupUI();
    void updateSizes();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLineEdit* descriptionEdit_{nullptr};
    QLineEdit* amountEdit_{nullptr};
    QComboBox* categoryCombo_{nullptr};
    QLineEdit* vendorEdit_{nullptr};
    QComboBox* paymentMethodCombo_{nullptr};
    QPushButton* saveBtn_{nullptr};
    QPushButton* cancelBtn_{nullptr};
};

// ============================================================================
// End Day Zone
// ============================================================================
class EndDayZone : public Zone {
    Q_OBJECT
public:
    explicit EndDayZone(QWidget* parent = nullptr);
signals:
    void backRequested();
    void endDayConfirmed();
protected:
    void resizeEvent(QResizeEvent* event) override;
private slots:
    void onConfirmEndDay();
private:
    void setupUI();
    void updateSizes();
    void loadDaySummary();
    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLabel* salesSummary_{nullptr};
    QLabel* paymentSummary_{nullptr};
    QLabel* laborSummary_{nullptr};
    QLabel* warningsLabel_{nullptr};
    QPushButton* confirmBtn_{nullptr};
    QPushButton* cancelBtn_{nullptr};
};

} // namespace vt2
