/**
 * @file manager_zone.hpp
 * @brief Manager zone with management and reporting functions
 */

#pragma once

#include "ui/zone.hpp"
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QGridLayout>
#include <QVBoxLayout>
#include <vector>

namespace vt2 {

/**
 * @brief Manager zone - access to management and reporting functions
 * 
 * Provides access to:
 * - User Management
 * - Balance Tills
 * - Audit / Sales Reports
 * - Menu Item Performance
 * - Revenue & Productivity
 * - And more...
 */
class ManagerZone : public Zone {
    Q_OBJECT

public:
    explicit ManagerZone(QWidget* parent = nullptr);
    ~ManagerZone() override;

signals:
    // User Management
    void userManagerRequested();
    
    // Till Management
    void balanceTillsRequested();
    
    // Reports & Audit
    void auditRequested();
    void menuItemPerformanceRequested();
    void todaysRevenueRequested();
    void exceptionalTransactionsRequested();
    void franchiseTrafficRequested();
    void receiptsBalanceRequested();
    void closedCheckSummaryRequested();
    void reviewGuestChecksRequested();
    void expensesRequested();
    
    // Menu Management
    void editMenuItemPropertiesRequested();
    
    // End of Day Operations
    void payCapturedTipsRequested();
    void recordExpensesRequested();
    void endDayRequested();
    
    // Navigation
    void backRequested();

protected:
    void drawContent(QPainter& painter) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void updateSizes();
    QPushButton* createManagerButton(const QString& text, const QColor& color);

    QLabel* m_titleLabel{nullptr};
    QScrollArea* m_scrollArea{nullptr};
    QWidget* m_buttonContainer{nullptr};
    QGridLayout* m_buttonLayout{nullptr};
    QPushButton* m_backBtn{nullptr};
    QVBoxLayout* m_mainLayout{nullptr};
    std::vector<QPushButton*> m_allButtons;
    
    // Store buttons for specific styling
    QPushButton* m_userManagerBtn{nullptr};
    QPushButton* m_balanceTillsBtn{nullptr};
    QPushButton* m_auditBtn{nullptr};
    QPushButton* m_endDayBtn{nullptr};
};

} // namespace vt2
