/**
 * @file balance_tills_zone.cpp
 * @brief Balance Tills zone implementation
 */

#include "balance_tills_zone.hpp"
#include "data/employee_store.hpp"
#include "core/logger.hpp"
#include <QHeaderView>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QDateTime>
#include <random>

namespace vt2 {

BalanceTillsZone::BalanceTillsZone(EmployeeStore* store, QWidget* parent)
    : Zone(ZoneType::Manager, parent)
    , employeeStore_(store) {
    setZoneName("Balance Tills");
    setupUI();
    refreshTillsList();
}

void BalanceTillsZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    // Title
    titleLabel_ = new QLabel("💵 Balance Server Tills");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    // Tills table
    tillsTable_ = new QTableWidget();
    tillsTable_->setColumnCount(6);
    tillsTable_->setHorizontalHeaderLabels({
        "Server", "Cash Sales", "Card Sales", "Cash Due", "Tips Due", "Status"
    });
    tillsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tillsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    tillsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tillsTable_->horizontalHeader()->setStretchLastSection(true);
    tillsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(tillsTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        selectedRow_ = row;
        balanceBtn_->setEnabled(true);
    });
    mainLayout_->addWidget(tillsTable_, 1);

    // Summary
    auto* summaryLabel = new QLabel();
    summaryLabel->setAlignment(Qt::AlignCenter);
    summaryLabel->setObjectName("summaryLabel");
    mainLayout_->addWidget(summaryLabel);

    // Button row
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(15);

    balanceBtn_ = new QPushButton("✅ Balance Selected");
    balanceBtn_->setEnabled(false);
    connect(balanceBtn_, &QPushButton::clicked, this, &BalanceTillsZone::onBalanceSelected);
    buttonLayout->addWidget(balanceBtn_);

    printBtn_ = new QPushButton("🖨️ Print Report");
    connect(printBtn_, &QPushButton::clicked, this, &BalanceTillsZone::onPrintReport);
    buttonLayout->addWidget(printBtn_);

    buttonLayout->addStretch();

    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &BalanceTillsZone::backRequested);
    buttonLayout->addWidget(backBtn_);

    mainLayout_->addLayout(buttonLayout);

    updateSizes();
}

void BalanceTillsZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void BalanceTillsZone::updateSizes() {
    int h = height();
    int baseFontSize = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #27ae60; padding: %2px;")
        .arg(baseFontSize * 2).arg(baseFontSize / 2));
    
    tillsTable_->setStyleSheet(QString(R"(
        QTableWidget {
            background-color: white;
            border: 2px solid #bdc3c7;
            border-radius: 8px;
            font-size: %1px;
        }
        QTableWidget::item { padding: 8px; }
        QTableWidget::item:selected { background-color: #27ae60; color: white; }
        QHeaderView::section {
            background-color: #27ae60;
            color: white;
            padding: %2px;
            font-weight: bold;
            font-size: %1px;
            border: none;
        }
    )").arg(baseFontSize).arg(baseFontSize));

    if (auto* summary = findChild<QLabel*>("summaryLabel")) {
        summary->setStyleSheet(QString("font-size: %1px; color: #2c3e50; padding: 10px;").arg(baseFontSize));
    }

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

    balanceBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #27ae60; } QPushButton:hover { background-color: #2ecc71; } QPushButton:disabled { background-color: #95a5a6; }");
    printBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #3498db; } QPushButton:hover { background-color: #5dade2; }");
    backBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #7f8c8d; } QPushButton:hover { background-color: #95a5a6; }");
}

void BalanceTillsZone::refreshTillsList() {
    tillsTable_->setRowCount(0);
    
    if (!employeeStore_) return;
    
    // Get servers/bartenders who handle cash
    auto employees = employeeStore_->getAllEmployees(false);
    
    // Generate demo till data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> cashDist(50.0, 500.0);
    std::uniform_real_distribution<> cardDist(100.0, 800.0);
    std::uniform_real_distribution<> tipsDist(20.0, 150.0);
    
    double totalCashDue = 0;
    double totalTipsDue = 0;
    int unbalanced = 0;
    
    for (const auto& emp : employees) {
        if (emp.role() != EmployeeRole::Server && 
            emp.role() != EmployeeRole::Bartender &&
            emp.role() != EmployeeRole::Cashier) {
            continue;
        }
        
        int row = tillsTable_->rowCount();
        tillsTable_->insertRow(row);
        
        double cashSales = cashDist(gen);
        double cardSales = cardDist(gen);
        double cashDue = cashSales;
        double tipsDue = tipsDist(gen);
        bool balanced = (row % 3 == 0);  // Some already balanced for demo
        
        tillsTable_->setItem(row, 0, new QTableWidgetItem(emp.fullName()));
        tillsTable_->setItem(row, 1, new QTableWidgetItem(QString("$%1").arg(cashSales, 0, 'f', 2)));
        tillsTable_->setItem(row, 2, new QTableWidgetItem(QString("$%1").arg(cardSales, 0, 'f', 2)));
        tillsTable_->setItem(row, 3, new QTableWidgetItem(QString("$%1").arg(cashDue, 0, 'f', 2)));
        tillsTable_->setItem(row, 4, new QTableWidgetItem(QString("$%1").arg(tipsDue, 0, 'f', 2)));
        
        QString status = balanced ? "✅ Balanced" : "⏳ Pending";
        auto* statusItem = new QTableWidgetItem(status);
        if (balanced) {
            statusItem->setForeground(QColor("#27ae60"));
        } else {
            statusItem->setForeground(QColor("#e67e22"));
            totalCashDue += cashDue;
            totalTipsDue += tipsDue;
            unbalanced++;
        }
        tillsTable_->setItem(row, 5, statusItem);
    }
    
    // Update summary
    if (auto* summary = findChild<QLabel*>("summaryLabel")) {
        summary->setText(QString("📊 %1 servers pending | Total Cash Due: $%2 | Total Tips Due: $%3")
            .arg(unbalanced)
            .arg(totalCashDue, 0, 'f', 2)
            .arg(totalTipsDue, 0, 'f', 2));
    }
    
    selectedRow_ = -1;
    balanceBtn_->setEnabled(false);
}

void BalanceTillsZone::onBalanceSelected() {
    if (selectedRow_ < 0) return;
    
    QString serverName = tillsTable_->item(selectedRow_, 0)->text();
    QString cashDue = tillsTable_->item(selectedRow_, 3)->text();
    QString tipsDue = tillsTable_->item(selectedRow_, 4)->text();
    
    QDialog dialog(this);
    dialog.setWindowTitle("Balance Till - " + serverName);
    dialog.setMinimumSize(400, 350);
    dialog.setStyleSheet("QDialog { background-color: #ecf0f1; }");

    auto* layout = new QFormLayout(&dialog);
    layout->setSpacing(15);
    layout->setContentsMargins(20, 20, 20, 20);

    auto* infoLabel = new QLabel(QString("Server: %1\nCash Due: %2\nTips Due: %3")
        .arg(serverName).arg(cashDue).arg(tipsDue));
    infoLabel->setStyleSheet("font-size: 16px; padding: 10px; background-color: #fff; border-radius: 5px;");
    layout->addRow(infoLabel);

    auto* cashReceived = new QLineEdit(cashDue.mid(1));  // Remove $
    cashReceived->setStyleSheet("padding: 10px; font-size: 18px; border: 2px solid #bdc3c7; border-radius: 5px;");
    layout->addRow("Cash Received:", cashReceived);

    auto* tipsAmount = new QLineEdit(tipsDue.mid(1));
    tipsAmount->setStyleSheet("padding: 10px; font-size: 18px; border: 2px solid #bdc3c7; border-radius: 5px;");
    layout->addRow("Tips Paid:", tipsAmount);

    auto* notesEdit = new QLineEdit();
    notesEdit->setPlaceholderText("Optional notes...");
    notesEdit->setStyleSheet("padding: 10px; font-size: 14px; border: 2px solid #bdc3c7; border-radius: 5px;");
    layout->addRow("Notes:", notesEdit);

    auto* btnLayout = new QHBoxLayout();
    auto* confirmBtn = new QPushButton("✅ Confirm Balance");
    confirmBtn->setStyleSheet(R"(
        QPushButton { background-color: #27ae60; color: white; border: none;
        border-radius: 8px; padding: 12px 25px; font-size: 16px; font-weight: bold; }
        QPushButton:hover { background-color: #2ecc71; }
    )");

    auto* cancelBtn = new QPushButton("❌ Cancel");
    cancelBtn->setStyleSheet(R"(
        QPushButton { background-color: #e74c3c; color: white; border: none;
        border-radius: 8px; padding: 12px 25px; font-size: 16px; font-weight: bold; }
        QPushButton:hover { background-color: #ec7063; }
    )");

    btnLayout->addWidget(confirmBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addRow(btnLayout);

    connect(confirmBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        // Update the row to show balanced
        tillsTable_->item(selectedRow_, 5)->setText("✅ Balanced");
        tillsTable_->item(selectedRow_, 5)->setForeground(QColor("#27ae60"));
        
        VT_INFO("Till balanced for {}: Cash={}, Tips={}", 
            serverName.toStdString(), cashReceived->text().toStdString(), tipsAmount->text().toStdString());
        
        QMessageBox::information(this, "Till Balanced",
            QString("Till balanced successfully for %1.\n\nCash Received: $%2\nTips Paid: $%3")
                .arg(serverName)
                .arg(cashReceived->text())
                .arg(tipsAmount->text()));
        
        refreshTillsList();
    }
}

void BalanceTillsZone::onPrintReport() {
    VT_INFO("Printing till balance report");
    QMessageBox::information(this, "Print Report",
        "Till Balance Report\n\n"
        "Date: " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") + "\n\n"
        "Report would be sent to printer...");
}

} // namespace vt2
