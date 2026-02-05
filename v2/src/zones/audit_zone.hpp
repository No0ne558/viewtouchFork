/**
 * @file audit_zone.hpp
 * @brief Audit zone for sales reports and history
 */

#pragma once

#include "ui/zone.hpp"
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QDateEdit>
#include <QComboBox>
#include <QVBoxLayout>

namespace vt2 {

class AuditZone : public Zone {
    Q_OBJECT

public:
    explicit AuditZone(QWidget* parent = nullptr);
    void refreshReport();

signals:
    void backRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onGenerateReport();
    void onPrintReport();
    void onExportReport();

private:
    void setupUI();
    void updateSizes();
    void generateSampleData();

    QVBoxLayout* mainLayout_{nullptr};
    QLabel* titleLabel_{nullptr};
    QDateEdit* startDate_{nullptr};
    QDateEdit* endDate_{nullptr};
    QComboBox* reportType_{nullptr};
    QTableWidget* reportTable_{nullptr};
    QLabel* summaryLabel_{nullptr};
    QPushButton* generateBtn_{nullptr};
    QPushButton* printBtn_{nullptr};
    QPushButton* exportBtn_{nullptr};
    QPushButton* backBtn_{nullptr};
};

} // namespace vt2
