/**
 * @file audit_zone.cpp
 * @brief Audit zone implementation
 */

#include "audit_zone.hpp"
#include "core/logger.hpp"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QMessageBox>
#include <QDateTime>
#include <random>

namespace vt2 {

AuditZone::AuditZone(QWidget* parent)
    : Zone(ZoneType::Report, parent) {
    setZoneName("Audit");
    setupUI();
}

void AuditZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    // Title
    titleLabel_ = new QLabel("📊 Audit - Sales Reports & History");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    // Date range and report type controls
    auto* controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(20);

    auto* startLabel = new QLabel("From:");
    startDate_ = new QDateEdit(QDate::currentDate().addDays(-7));
    startDate_->setCalendarPopup(true);
    startDate_->setDisplayFormat("yyyy-MM-dd");
    controlsLayout->addWidget(startLabel);
    controlsLayout->addWidget(startDate_);

    auto* endLabel = new QLabel("To:");
    endDate_ = new QDateEdit(QDate::currentDate());
    endDate_->setCalendarPopup(true);
    endDate_->setDisplayFormat("yyyy-MM-dd");
    controlsLayout->addWidget(endLabel);
    controlsLayout->addWidget(endDate_);

    auto* typeLabel = new QLabel("Report:");
    reportType_ = new QComboBox();
    reportType_->addItem("Sales Summary");
    reportType_->addItem("By Server");
    reportType_->addItem("By Category");
    reportType_->addItem("By Hour");
    reportType_->addItem("Payment Methods");
    controlsLayout->addWidget(typeLabel);
    controlsLayout->addWidget(reportType_);

    generateBtn_ = new QPushButton("📈 Generate");
    connect(generateBtn_, &QPushButton::clicked, this, &AuditZone::onGenerateReport);
    controlsLayout->addWidget(generateBtn_);

    controlsLayout->addStretch();
    mainLayout_->addLayout(controlsLayout);

    // Report table
    reportTable_ = new QTableWidget();
    reportTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    reportTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    reportTable_->horizontalHeader()->setStretchLastSection(true);
    reportTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(reportTable_, 1);

    // Summary
    summaryLabel_ = new QLabel("Select date range and click Generate to view report");
    summaryLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(summaryLabel_);

    // Button row
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(15);

    printBtn_ = new QPushButton("🖨️ Print Report");
    connect(printBtn_, &QPushButton::clicked, this, &AuditZone::onPrintReport);
    buttonLayout->addWidget(printBtn_);

    exportBtn_ = new QPushButton("💾 Export CSV");
    connect(exportBtn_, &QPushButton::clicked, this, &AuditZone::onExportReport);
    buttonLayout->addWidget(exportBtn_);

    buttonLayout->addStretch();

    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &AuditZone::backRequested);
    buttonLayout->addWidget(backBtn_);

    mainLayout_->addLayout(buttonLayout);

    updateSizes();
}

void AuditZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void AuditZone::updateSizes() {
    int h = height();
    int baseFontSize = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #8e44ad; padding: %2px;")
        .arg(baseFontSize * 2).arg(baseFontSize / 2));
    
    QString inputStyle = QString("padding: %1px; font-size: %2px;").arg(baseFontSize / 2).arg(baseFontSize);
    startDate_->setStyleSheet(inputStyle);
    endDate_->setStyleSheet(inputStyle);
    reportType_->setStyleSheet(inputStyle);

    reportTable_->setStyleSheet(QString(R"(
        QTableWidget {
            background-color: white;
            border: 2px solid #bdc3c7;
            border-radius: 8px;
            font-size: %1px;
        }
        QTableWidget::item { padding: 8px; }
        QTableWidget::item:selected { background-color: #8e44ad; color: white; }
        QHeaderView::section {
            background-color: #8e44ad;
            color: white;
            padding: %2px;
            font-weight: bold;
            font-size: %1px;
            border: none;
        }
    )").arg(baseFontSize).arg(baseFontSize));

    summaryLabel_->setStyleSheet(QString("font-size: %1px; color: #2c3e50; padding: 10px; background-color: #ecf0f1; border-radius: 5px;").arg(baseFontSize));

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

    generateBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #8e44ad; } QPushButton:hover { background-color: #9b59b6; }");
    printBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #3498db; } QPushButton:hover { background-color: #5dade2; }");
    exportBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #27ae60; } QPushButton:hover { background-color: #2ecc71; }");
    backBtn_->setStyleSheet(btnStyle + "QPushButton { background-color: #7f8c8d; } QPushButton:hover { background-color: #95a5a6; }");
}

void AuditZone::onGenerateReport() {
    VT_INFO("Generating {} report from {} to {}", 
        reportType_->currentText().toStdString(),
        startDate_->date().toString("yyyy-MM-dd").toStdString(),
        endDate_->date().toString("yyyy-MM-dd").toStdString());
    
    generateSampleData();
}

void AuditZone::generateSampleData() {
    reportTable_->clear();
    
    QString type = reportType_->currentText();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    if (type == "Sales Summary") {
        reportTable_->setColumnCount(4);
        reportTable_->setHorizontalHeaderLabels({"Date", "Gross Sales", "Discounts", "Net Sales"});
        
        std::uniform_real_distribution<> salesDist(1500.0, 4500.0);
        std::uniform_real_distribution<> discDist(50.0, 200.0);
        
        double totalGross = 0, totalDisc = 0;
        QDate date = startDate_->date();
        while (date <= endDate_->date()) {
            int row = reportTable_->rowCount();
            reportTable_->insertRow(row);
            
            double gross = salesDist(gen);
            double disc = discDist(gen);
            totalGross += gross;
            totalDisc += disc;
            
            reportTable_->setItem(row, 0, new QTableWidgetItem(date.toString("yyyy-MM-dd")));
            reportTable_->setItem(row, 1, new QTableWidgetItem(QString("$%1").arg(gross, 0, 'f', 2)));
            reportTable_->setItem(row, 2, new QTableWidgetItem(QString("$%1").arg(disc, 0, 'f', 2)));
            reportTable_->setItem(row, 3, new QTableWidgetItem(QString("$%1").arg(gross - disc, 0, 'f', 2)));
            
            date = date.addDays(1);
        }
        
        summaryLabel_->setText(QString("📊 Total: Gross $%1 | Discounts $%2 | Net $%3")
            .arg(totalGross, 0, 'f', 2)
            .arg(totalDisc, 0, 'f', 2)
            .arg(totalGross - totalDisc, 0, 'f', 2));
            
    } else if (type == "By Server") {
        reportTable_->setColumnCount(5);
        reportTable_->setHorizontalHeaderLabels({"Server", "Checks", "Sales", "Avg Check", "Tips"});
        
        QStringList servers = {"John Manager", "Jane Server", "Bob Bartender", "Alice Cashier"};
        std::uniform_int_distribution<> checksDist(20, 80);
        std::uniform_real_distribution<> avgDist(25.0, 55.0);
        std::uniform_real_distribution<> tipPct(0.15, 0.22);
        
        double totalSales = 0;
        int totalChecks = 0;
        
        for (const auto& server : servers) {
            int row = reportTable_->rowCount();
            reportTable_->insertRow(row);
            
            int checks = checksDist(gen);
            double avg = avgDist(gen);
            double sales = checks * avg;
            double tips = sales * tipPct(gen);
            totalChecks += checks;
            totalSales += sales;
            
            reportTable_->setItem(row, 0, new QTableWidgetItem(server));
            reportTable_->setItem(row, 1, new QTableWidgetItem(QString::number(checks)));
            reportTable_->setItem(row, 2, new QTableWidgetItem(QString("$%1").arg(sales, 0, 'f', 2)));
            reportTable_->setItem(row, 3, new QTableWidgetItem(QString("$%1").arg(avg, 0, 'f', 2)));
            reportTable_->setItem(row, 4, new QTableWidgetItem(QString("$%1").arg(tips, 0, 'f', 2)));
        }
        
        summaryLabel_->setText(QString("📊 %1 servers | %2 checks | Total Sales: $%3")
            .arg(servers.size()).arg(totalChecks).arg(totalSales, 0, 'f', 2));
            
    } else if (type == "By Category") {
        reportTable_->setColumnCount(4);
        reportTable_->setHorizontalHeaderLabels({"Category", "Items Sold", "Revenue", "% of Sales"});
        
        QStringList categories = {"Appetizers", "Entrees", "Sides", "Beverages", "Desserts", "Alcohol"};
        std::vector<double> weights = {0.10, 0.35, 0.10, 0.15, 0.08, 0.22};
        std::uniform_real_distribution<> totalDist(8000.0, 15000.0);
        
        double totalSales = totalDist(gen);
        
        for (int i = 0; i < categories.size(); ++i) {
            int row = reportTable_->rowCount();
            reportTable_->insertRow(row);
            
            double revenue = totalSales * weights[i];
            int items = static_cast<int>(revenue / (15.0 + i * 3));
            
            reportTable_->setItem(row, 0, new QTableWidgetItem(categories[i]));
            reportTable_->setItem(row, 1, new QTableWidgetItem(QString::number(items)));
            reportTable_->setItem(row, 2, new QTableWidgetItem(QString("$%1").arg(revenue, 0, 'f', 2)));
            reportTable_->setItem(row, 3, new QTableWidgetItem(QString("%1%").arg(weights[i] * 100, 0, 'f', 1)));
        }
        
        summaryLabel_->setText(QString("📊 Total Revenue: $%1").arg(totalSales, 0, 'f', 2));
        
    } else if (type == "By Hour") {
        reportTable_->setColumnCount(4);
        reportTable_->setHorizontalHeaderLabels({"Hour", "Checks", "Sales", "Avg Check"});
        
        // Restaurant hours 11am - 10pm
        std::vector<double> hourWeights = {0.05, 0.12, 0.10, 0.06, 0.04, 0.08, 0.15, 0.18, 0.12, 0.07, 0.03};
        std::uniform_real_distribution<> totalDist(5000.0, 10000.0);
        double totalSales = totalDist(gen);
        
        for (int h = 0; h < 11; ++h) {
            int row = reportTable_->rowCount();
            reportTable_->insertRow(row);
            
            double sales = totalSales * hourWeights[h];
            int checks = static_cast<int>(sales / 35.0);
            double avg = checks > 0 ? sales / checks : 0;
            
            QString hourStr = QString("%1:00 - %2:00").arg(11 + h).arg(12 + h);
            
            reportTable_->setItem(row, 0, new QTableWidgetItem(hourStr));
            reportTable_->setItem(row, 1, new QTableWidgetItem(QString::number(checks)));
            reportTable_->setItem(row, 2, new QTableWidgetItem(QString("$%1").arg(sales, 0, 'f', 2)));
            reportTable_->setItem(row, 3, new QTableWidgetItem(QString("$%1").arg(avg, 0, 'f', 2)));
        }
        
        summaryLabel_->setText(QString("📊 Peak hours: 6pm-8pm | Total: $%1").arg(totalSales, 0, 'f', 2));
        
    } else if (type == "Payment Methods") {
        reportTable_->setColumnCount(4);
        reportTable_->setHorizontalHeaderLabels({"Payment Type", "Transactions", "Amount", "% of Total"});
        
        QStringList methods = {"Cash", "Visa", "MasterCard", "Amex", "Discover", "Gift Card"};
        std::vector<double> weights = {0.25, 0.35, 0.20, 0.10, 0.05, 0.05};
        std::uniform_real_distribution<> totalDist(8000.0, 12000.0);
        double totalAmount = totalDist(gen);
        
        for (int i = 0; i < methods.size(); ++i) {
            int row = reportTable_->rowCount();
            reportTable_->insertRow(row);
            
            double amount = totalAmount * weights[i];
            int txns = static_cast<int>(amount / 40.0);
            
            reportTable_->setItem(row, 0, new QTableWidgetItem(methods[i]));
            reportTable_->setItem(row, 1, new QTableWidgetItem(QString::number(txns)));
            reportTable_->setItem(row, 2, new QTableWidgetItem(QString("$%1").arg(amount, 0, 'f', 2)));
            reportTable_->setItem(row, 3, new QTableWidgetItem(QString("%1%").arg(weights[i] * 100, 0, 'f', 1)));
        }
        
        summaryLabel_->setText(QString("📊 Total Payments: $%1 | Cash: 25%% | Card: 70%% | Gift: 5%%").arg(totalAmount, 0, 'f', 2));
    }
}

void AuditZone::refreshReport() {
    if (reportTable_->rowCount() > 0) {
        onGenerateReport();
    }
}

void AuditZone::onPrintReport() {
    VT_INFO("Printing audit report");
    QMessageBox::information(this, "Print Report",
        QString("Audit Report: %1\n\n"
                "Date Range: %2 to %3\n\n"
                "Report would be sent to printer...")
            .arg(reportType_->currentText())
            .arg(startDate_->date().toString("yyyy-MM-dd"))
            .arg(endDate_->date().toString("yyyy-MM-dd")));
}

void AuditZone::onExportReport() {
    VT_INFO("Exporting audit report to CSV");
    QMessageBox::information(this, "Export Report",
        "Report exported to:\n\naudit_report_" + 
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".csv");
}

} // namespace vt2
