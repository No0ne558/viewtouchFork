/**
 * @file manager_reports_zones.cpp
 * @brief Manager report zones implementations
 */

#include "manager_reports_zones.hpp"
#include "core/logger.hpp"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QMessageBox>
#include <QDateTime>
#include <QComboBox>
#include <QScrollArea>
#include <QGroupBox>
#include <QFormLayout>
#include <random>

namespace vt2 {

// Helper for random data generation
static std::mt19937& getRng() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}

// Common button style helper
static QString getButtonStyle(int fontSize, const QString& bgColor, const QString& hoverColor) {
    return QString(R"(
        QPushButton {
            border: none; border-radius: 8px;
            padding: %1px %2px; font-size: %3px;
            font-weight: bold; color: white;
            background-color: %4;
        }
        QPushButton:hover { background-color: %5; }
        QPushButton:disabled { background-color: #95a5a6; }
    )").arg(fontSize).arg(fontSize * 2).arg(fontSize).arg(bgColor).arg(hoverColor);
}

// ============================================================================
// Menu Performance Zone
// ============================================================================
MenuPerformanceZone::MenuPerformanceZone(QWidget* parent)
    : Zone(ZoneType::Report, parent) {
    setZoneName("Menu Performance");
    setupUI();
    loadSampleData();
}

void MenuPerformanceZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    titleLabel_ = new QLabel("📈 Menu Item Performance");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    reportTable_ = new QTableWidget();
    reportTable_->setColumnCount(6);
    reportTable_->setHorizontalHeaderLabels({"Item", "Category", "Sold", "Revenue", "Avg/Day", "Trend"});
    reportTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    reportTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    reportTable_->horizontalHeader()->setStretchLastSection(true);
    reportTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(reportTable_, 1);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &MenuPerformanceZone::backRequested);
    btnLayout->addWidget(backBtn_);
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void MenuPerformanceZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void MenuPerformanceZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #e67e22; padding: 10px;").arg(fs * 2));
    reportTable_->setStyleSheet(QString(R"(
        QTableWidget { background-color: white; border: 2px solid #bdc3c7; border-radius: 8px; font-size: %1px; }
        QTableWidget::item { padding: 8px; }
        QTableWidget::item:selected { background-color: #e67e22; color: white; }
        QHeaderView::section { background-color: #e67e22; color: white; padding: %1px; font-weight: bold; border: none; }
    )").arg(fs));
    backBtn_->setStyleSheet(getButtonStyle(fs, "#7f8c8d", "#95a5a6"));
}

void MenuPerformanceZone::loadSampleData() {
    reportTable_->setRowCount(0);
    
    struct MenuItem { QString name; QString category; };
    std::vector<MenuItem> items = {
        {"Classic Burger", "Entrees"}, {"Caesar Salad", "Appetizers"},
        {"Fish & Chips", "Entrees"}, {"Margherita Pizza", "Entrees"},
        {"Buffalo Wings", "Appetizers"}, {"Grilled Salmon", "Entrees"},
        {"French Fries", "Sides"}, {"Onion Rings", "Sides"},
        {"Chocolate Cake", "Desserts"}, {"Craft Beer", "Beverages"},
        {"House Wine", "Beverages"}, {"Soft Drinks", "Beverages"}
    };
    
    std::uniform_int_distribution<> soldDist(50, 300);
    std::uniform_real_distribution<> priceDist(8.0, 28.0);
    QStringList trends = {"📈 +15%", "📈 +8%", "➡️ 0%", "📉 -5%", "📈 +22%"};
    
    for (const auto& item : items) {
        int row = reportTable_->rowCount();
        reportTable_->insertRow(row);
        
        int sold = soldDist(getRng());
        double price = priceDist(getRng());
        double revenue = sold * price;
        
        reportTable_->setItem(row, 0, new QTableWidgetItem(item.name));
        reportTable_->setItem(row, 1, new QTableWidgetItem(item.category));
        reportTable_->setItem(row, 2, new QTableWidgetItem(QString::number(sold)));
        reportTable_->setItem(row, 3, new QTableWidgetItem(QString("$%1").arg(revenue, 0, 'f', 2)));
        reportTable_->setItem(row, 4, new QTableWidgetItem(QString::number(sold / 7)));
        reportTable_->setItem(row, 5, new QTableWidgetItem(trends[row % trends.size()]));
    }
}

// ============================================================================
// Today's Revenue Zone
// ============================================================================
TodaysRevenueZone::TodaysRevenueZone(QWidget* parent)
    : Zone(ZoneType::Report, parent) {
    setZoneName("Today's Revenue");
    setupUI();
    refresh();
}

void TodaysRevenueZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    titleLabel_ = new QLabel("💰 Today's Revenue & Productivity");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    // Stats grid
    auto* statsLayout = new QHBoxLayout();
    
    salesLabel_ = new QLabel();
    salesLabel_->setAlignment(Qt::AlignCenter);
    salesLabel_->setObjectName("salesLabel");
    statsLayout->addWidget(salesLabel_);

    checksLabel_ = new QLabel();
    checksLabel_->setAlignment(Qt::AlignCenter);
    checksLabel_->setObjectName("checksLabel");
    statsLayout->addWidget(checksLabel_);

    avgCheckLabel_ = new QLabel();
    avgCheckLabel_->setAlignment(Qt::AlignCenter);
    avgCheckLabel_->setObjectName("avgCheckLabel");
    statsLayout->addWidget(avgCheckLabel_);

    tipsLabel_ = new QLabel();
    tipsLabel_->setAlignment(Qt::AlignCenter);
    tipsLabel_->setObjectName("tipsLabel");
    statsLayout->addWidget(tipsLabel_);

    laborLabel_ = new QLabel();
    laborLabel_->setAlignment(Qt::AlignCenter);
    laborLabel_->setObjectName("laborLabel");
    statsLayout->addWidget(laborLabel_);

    mainLayout_->addLayout(statsLayout);

    // Hourly breakdown
    hourlyTable_ = new QTableWidget();
    hourlyTable_->setColumnCount(4);
    hourlyTable_->setHorizontalHeaderLabels({"Hour", "Sales", "Checks", "Avg"});
    hourlyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    hourlyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(hourlyTable_, 1);

    auto* btnLayout = new QHBoxLayout();
    refreshBtn_ = new QPushButton("🔄 Refresh");
    connect(refreshBtn_, &QPushButton::clicked, this, &TodaysRevenueZone::refresh);
    btnLayout->addWidget(refreshBtn_);
    btnLayout->addStretch();
    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &TodaysRevenueZone::backRequested);
    btnLayout->addWidget(backBtn_);
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void TodaysRevenueZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void TodaysRevenueZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #27ae60; padding: 10px;").arg(fs * 2));
    
    QString statStyle = QString(R"(
        QLabel { background-color: #ecf0f1; border-radius: 10px; padding: 15px;
                 font-size: %1px; min-height: 80px; }
    )").arg(fs);
    salesLabel_->setStyleSheet(statStyle);
    checksLabel_->setStyleSheet(statStyle);
    avgCheckLabel_->setStyleSheet(statStyle);
    tipsLabel_->setStyleSheet(statStyle);
    laborLabel_->setStyleSheet(statStyle);

    hourlyTable_->setStyleSheet(QString(R"(
        QTableWidget { background-color: white; border: 2px solid #bdc3c7; border-radius: 8px; font-size: %1px; }
        QHeaderView::section { background-color: #27ae60; color: white; padding: %1px; font-weight: bold; border: none; }
    )").arg(fs));

    refreshBtn_->setStyleSheet(getButtonStyle(fs, "#3498db", "#5dade2"));
    backBtn_->setStyleSheet(getButtonStyle(fs, "#7f8c8d", "#95a5a6"));
}

void TodaysRevenueZone::refresh() {
    std::uniform_real_distribution<> salesDist(2500.0, 6000.0);
    std::uniform_int_distribution<> checksDist(80, 200);
    std::uniform_real_distribution<> tipsDist(400.0, 900.0);
    
    double sales = salesDist(getRng());
    int checks = checksDist(getRng());
    double tips = tipsDist(getRng());
    double labor = sales * 0.28;
    
    salesLabel_->setText(QString("💵 Sales\n$%1").arg(sales, 0, 'f', 2));
    checksLabel_->setText(QString("🧾 Checks\n%1").arg(checks));
    avgCheckLabel_->setText(QString("📊 Avg Check\n$%1").arg(sales/checks, 0, 'f', 2));
    tipsLabel_->setText(QString("💳 Tips\n$%1").arg(tips, 0, 'f', 2));
    laborLabel_->setText(QString("👷 Labor\n$%1").arg(labor, 0, 'f', 2));

    // Hourly data
    hourlyTable_->setRowCount(0);
    int currentHour = QTime::currentTime().hour();
    for (int h = 11; h <= qMin(currentHour, 22); ++h) {
        int row = hourlyTable_->rowCount();
        hourlyTable_->insertRow(row);
        
        double hourSales = sales / 12.0 * (0.5 + getRng()() % 100 / 100.0);
        int hourChecks = checks / 12 + getRng()() % 5;
        
        hourlyTable_->setItem(row, 0, new QTableWidgetItem(QString("%1:00").arg(h)));
        hourlyTable_->setItem(row, 1, new QTableWidgetItem(QString("$%1").arg(hourSales, 0, 'f', 2)));
        hourlyTable_->setItem(row, 2, new QTableWidgetItem(QString::number(hourChecks)));
        hourlyTable_->setItem(row, 3, new QTableWidgetItem(QString("$%1").arg(hourChecks > 0 ? hourSales/hourChecks : 0, 0, 'f', 2)));
    }
}

// ============================================================================
// Exceptional Transactions Zone
// ============================================================================
ExceptionalTransactionsZone::ExceptionalTransactionsZone(QWidget* parent)
    : Zone(ZoneType::Report, parent) {
    setZoneName("Exceptional Transactions");
    setupUI();
    loadSampleData();
}

void ExceptionalTransactionsZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(10);

    titleLabel_ = new QLabel("⚠️ Exceptional Transactions - Voids, Comps & Discounts");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    // Voids
    auto* voidsLabel = new QLabel("🚫 Voids");
    mainLayout_->addWidget(voidsLabel);
    voidsTable_ = new QTableWidget();
    voidsTable_->setColumnCount(5);
    voidsTable_->setHorizontalHeaderLabels({"Time", "Server", "Item", "Amount", "Reason"});
    voidsTable_->setMaximumHeight(150);
    voidsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(voidsTable_);

    // Comps
    auto* compsLabel = new QLabel("🎁 Comps");
    mainLayout_->addWidget(compsLabel);
    compsTable_ = new QTableWidget();
    compsTable_->setColumnCount(5);
    compsTable_->setHorizontalHeaderLabels({"Time", "Server", "Item", "Amount", "Manager"});
    compsTable_->setMaximumHeight(150);
    compsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(compsTable_);

    // Discounts
    auto* discountsLabel = new QLabel("💸 Discounts");
    mainLayout_->addWidget(discountsLabel);
    discountsTable_ = new QTableWidget();
    discountsTable_->setColumnCount(5);
    discountsTable_->setHorizontalHeaderLabels({"Time", "Server", "Type", "Amount", "Check #"});
    discountsTable_->setMaximumHeight(150);
    discountsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(discountsTable_);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &ExceptionalTransactionsZone::backRequested);
    btnLayout->addWidget(backBtn_);
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void ExceptionalTransactionsZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void ExceptionalTransactionsZone::updateSizes() {
    int h = height();
    int fs = qMax(12, h / 60);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #c0392b; padding: 10px;").arg(fs * 2));
    
    QString tableStyle = QString(R"(
        QTableWidget { background-color: white; border: 1px solid #bdc3c7; font-size: %1px; }
        QHeaderView::section { background-color: #c0392b; color: white; padding: 5px; font-size: %1px; border: none; }
    )").arg(fs);
    voidsTable_->setStyleSheet(tableStyle);
    compsTable_->setStyleSheet(tableStyle);
    discountsTable_->setStyleSheet(tableStyle);
    
    backBtn_->setStyleSheet(getButtonStyle(fs, "#7f8c8d", "#95a5a6"));
}

void ExceptionalTransactionsZone::loadSampleData() {
    QStringList servers = {"Jane S.", "Bob B.", "Alice C."};
    QStringList voidReasons = {"Customer changed mind", "Wrong item", "Kitchen error"};
    QStringList discTypes = {"Senior 10%", "Military 15%", "Happy Hour", "Manager"};
    
    // Voids
    voidsTable_->setRowCount(0);
    for (int i = 0; i < 3; ++i) {
        voidsTable_->insertRow(i);
        voidsTable_->setItem(i, 0, new QTableWidgetItem(QString("%1:%2").arg(12+i).arg(30+i*10)));
        voidsTable_->setItem(i, 1, new QTableWidgetItem(servers[i]));
        voidsTable_->setItem(i, 2, new QTableWidgetItem(i == 0 ? "Burger" : i == 1 ? "Salad" : "Pizza"));
        voidsTable_->setItem(i, 3, new QTableWidgetItem(QString("$%1").arg(12.50 + i*5)));
        voidsTable_->setItem(i, 4, new QTableWidgetItem(voidReasons[i]));
    }

    // Comps
    compsTable_->setRowCount(0);
    for (int i = 0; i < 2; ++i) {
        compsTable_->insertRow(i);
        compsTable_->setItem(i, 0, new QTableWidgetItem(QString("%1:15").arg(14+i*2)));
        compsTable_->setItem(i, 1, new QTableWidgetItem(servers[i]));
        compsTable_->setItem(i, 2, new QTableWidgetItem(i == 0 ? "Dessert" : "Appetizer"));
        compsTable_->setItem(i, 3, new QTableWidgetItem(QString("$%1").arg(8.00 + i*4)));
        compsTable_->setItem(i, 4, new QTableWidgetItem("John M."));
    }

    // Discounts  
    discountsTable_->setRowCount(0);
    for (int i = 0; i < 4; ++i) {
        discountsTable_->insertRow(i);
        discountsTable_->setItem(i, 0, new QTableWidgetItem(QString("%1:%2").arg(11+i*2).arg(i*15)));
        discountsTable_->setItem(i, 1, new QTableWidgetItem(servers[i % 3]));
        discountsTable_->setItem(i, 2, new QTableWidgetItem(discTypes[i]));
        discountsTable_->setItem(i, 3, new QTableWidgetItem(QString("$%1").arg(5.00 + i*3)));
        discountsTable_->setItem(i, 4, new QTableWidgetItem(QString("#%1").arg(1001 + i)));
    }
}

// ============================================================================
// Franchise Traffic Zone
// ============================================================================
FranchiseTrafficZone::FranchiseTrafficZone(QWidget* parent)
    : Zone(ZoneType::Report, parent) {
    setZoneName("Franchise Traffic");
    setupUI();
    loadSampleData();
}

void FranchiseTrafficZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    titleLabel_ = new QLabel("🚗 Franchise Traffic Analysis");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    trafficTable_ = new QTableWidget();
    trafficTable_->setColumnCount(5);
    trafficTable_->setHorizontalHeaderLabels({"Hour", "Guests", "Tables", "Avg Party", "Wait Time"});
    trafficTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    trafficTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(trafficTable_, 1);

    summaryLabel_ = new QLabel();
    summaryLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(summaryLabel_);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &FranchiseTrafficZone::backRequested);
    btnLayout->addWidget(backBtn_);
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void FranchiseTrafficZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void FranchiseTrafficZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #16a085; padding: 10px;").arg(fs * 2));
    trafficTable_->setStyleSheet(QString(R"(
        QTableWidget { background-color: white; border: 2px solid #bdc3c7; border-radius: 8px; font-size: %1px; }
        QHeaderView::section { background-color: #16a085; color: white; padding: %1px; font-weight: bold; border: none; }
    )").arg(fs));
    summaryLabel_->setStyleSheet(QString("font-size: %1px; color: #2c3e50; padding: 15px; background: #ecf0f1; border-radius: 8px;").arg(fs));
    backBtn_->setStyleSheet(getButtonStyle(fs, "#7f8c8d", "#95a5a6"));
}

void FranchiseTrafficZone::loadSampleData() {
    trafficTable_->setRowCount(0);
    
    int totalGuests = 0;
    std::uniform_int_distribution<> guestDist(10, 45);
    
    for (int h = 11; h <= 21; ++h) {
        int row = trafficTable_->rowCount();
        trafficTable_->insertRow(row);
        
        int guests = guestDist(getRng());
        if (h >= 12 && h <= 14) guests += 15;  // Lunch rush
        if (h >= 18 && h <= 20) guests += 25;  // Dinner rush
        totalGuests += guests;
        
        int tables = guests / 3;
        double avgParty = 3.0 + (getRng()() % 20) / 10.0;
        int waitTime = (h >= 18 && h <= 20) ? 10 + getRng()() % 15 : 0;
        
        trafficTable_->setItem(row, 0, new QTableWidgetItem(QString("%1:00").arg(h)));
        trafficTable_->setItem(row, 1, new QTableWidgetItem(QString::number(guests)));
        trafficTable_->setItem(row, 2, new QTableWidgetItem(QString::number(tables)));
        trafficTable_->setItem(row, 3, new QTableWidgetItem(QString::number(avgParty, 'f', 1)));
        trafficTable_->setItem(row, 4, new QTableWidgetItem(waitTime > 0 ? QString("%1 min").arg(waitTime) : "-"));
    }
    
    summaryLabel_->setText(QString("📊 Total Guests Today: %1 | Peak Hours: 12-2pm, 6-8pm | Avg Party Size: 3.2").arg(totalGuests));
}

// ============================================================================
// Receipts Balance Zone
// ============================================================================
ReceiptsBalanceZone::ReceiptsBalanceZone(QWidget* parent)
    : Zone(ZoneType::Report, parent) {
    setZoneName("Receipts Balance");
    setupUI();
    loadSampleData();
}

void ReceiptsBalanceZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    titleLabel_ = new QLabel("🧾 Receipts Balance & Cash Deposits");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    // Summary cards
    auto* summaryLayout = new QHBoxLayout();
    cashSummary_ = new QLabel();
    cashSummary_->setAlignment(Qt::AlignCenter);
    summaryLayout->addWidget(cashSummary_);
    cardSummary_ = new QLabel();
    cardSummary_->setAlignment(Qt::AlignCenter);
    summaryLayout->addWidget(cardSummary_);
    mainLayout_->addLayout(summaryLayout);

    receiptsTable_ = new QTableWidget();
    receiptsTable_->setColumnCount(5);
    receiptsTable_->setHorizontalHeaderLabels({"Time", "Type", "Amount", "Ref #", "Status"});
    receiptsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    receiptsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(receiptsTable_, 1);

    auto* btnLayout = new QHBoxLayout();
    recordDepositBtn_ = new QPushButton("💵 Record Cash Deposit");
    connect(recordDepositBtn_, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, "Record Deposit", "Cash deposit recording dialog would open here.");
    });
    btnLayout->addWidget(recordDepositBtn_);
    btnLayout->addStretch();
    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &ReceiptsBalanceZone::backRequested);
    btnLayout->addWidget(backBtn_);
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void ReceiptsBalanceZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void ReceiptsBalanceZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #2980b9; padding: 10px;").arg(fs * 2));
    
    QString summaryStyle = QString("font-size: %1px; padding: 20px; background: #ecf0f1; border-radius: 10px; min-width: 200px;").arg(fs * 1.2);
    cashSummary_->setStyleSheet(summaryStyle);
    cardSummary_->setStyleSheet(summaryStyle);

    receiptsTable_->setStyleSheet(QString(R"(
        QTableWidget { background-color: white; border: 2px solid #bdc3c7; border-radius: 8px; font-size: %1px; }
        QHeaderView::section { background-color: #2980b9; color: white; padding: %1px; font-weight: bold; border: none; }
    )").arg(fs));

    recordDepositBtn_->setStyleSheet(getButtonStyle(fs, "#27ae60", "#2ecc71"));
    backBtn_->setStyleSheet(getButtonStyle(fs, "#7f8c8d", "#95a5a6"));
}

void ReceiptsBalanceZone::loadSampleData() {
    std::uniform_real_distribution<> amtDist(15.0, 150.0);
    double totalCash = 0, totalCard = 0;
    
    receiptsTable_->setRowCount(0);
    QStringList types = {"Cash", "Visa", "MC", "Amex", "Cash", "Visa"};
    
    for (int i = 0; i < 20; ++i) {
        int row = receiptsTable_->rowCount();
        receiptsTable_->insertRow(row);
        
        QString type = types[i % types.size()];
        double amt = amtDist(getRng());
        if (type == "Cash") totalCash += amt; else totalCard += amt;
        
        receiptsTable_->setItem(row, 0, new QTableWidgetItem(QString("%1:%2").arg(11 + i/3).arg((i*17) % 60, 2, 10, QChar('0'))));
        receiptsTable_->setItem(row, 1, new QTableWidgetItem(type));
        receiptsTable_->setItem(row, 2, new QTableWidgetItem(QString("$%1").arg(amt, 0, 'f', 2)));
        receiptsTable_->setItem(row, 3, new QTableWidgetItem(type == "Cash" ? "-" : QString("****%1").arg(1000 + i)));
        receiptsTable_->setItem(row, 4, new QTableWidgetItem("✅ Posted"));
    }
    
    cashSummary_->setText(QString("💵 Cash\n$%1\n(to deposit)").arg(totalCash, 0, 'f', 2));
    cardSummary_->setText(QString("💳 Cards\n$%1\n(settled)").arg(totalCard, 0, 'f', 2));
}

// ============================================================================
// Closed Check Summary Zone
// ============================================================================
ClosedCheckSummaryZone::ClosedCheckSummaryZone(QWidget* parent)
    : Zone(ZoneType::Report, parent) {
    setZoneName("Closed Check Summary");
    setupUI();
    loadSampleData();
}

void ClosedCheckSummaryZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    titleLabel_ = new QLabel("📅 Closed Check Summary by Calendar Day");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    auto* dateLayout = new QHBoxLayout();
    dateLayout->addWidget(new QLabel("Select Date:"));
    dateSelect_ = new QDateEdit(QDate::currentDate());
    dateSelect_->setCalendarPopup(true);
    connect(dateSelect_, &QDateEdit::dateChanged, this, [this]() { loadSampleData(); });
    dateLayout->addWidget(dateSelect_);
    dateLayout->addStretch();
    mainLayout_->addLayout(dateLayout);

    checksTable_ = new QTableWidget();
    checksTable_->setColumnCount(6);
    checksTable_->setHorizontalHeaderLabels({"Check #", "Server", "Table", "Subtotal", "Tax", "Total"});
    checksTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    checksTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(checksTable_, 1);

    summaryLabel_ = new QLabel();
    summaryLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(summaryLabel_);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &ClosedCheckSummaryZone::backRequested);
    btnLayout->addWidget(backBtn_);
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void ClosedCheckSummaryZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void ClosedCheckSummaryZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #8e44ad; padding: 10px;").arg(fs * 2));
    dateSelect_->setStyleSheet(QString("padding: %1px; font-size: %2px;").arg(fs/2).arg(fs));
    checksTable_->setStyleSheet(QString(R"(
        QTableWidget { background-color: white; border: 2px solid #bdc3c7; border-radius: 8px; font-size: %1px; }
        QHeaderView::section { background-color: #8e44ad; color: white; padding: %1px; font-weight: bold; border: none; }
    )").arg(fs));
    summaryLabel_->setStyleSheet(QString("font-size: %1px; padding: 15px; background: #ecf0f1; border-radius: 8px;").arg(fs));
    backBtn_->setStyleSheet(getButtonStyle(fs, "#7f8c8d", "#95a5a6"));
}

void ClosedCheckSummaryZone::loadSampleData() {
    checksTable_->setRowCount(0);
    
    QStringList servers = {"John M.", "Jane S.", "Bob B.", "Alice C."};
    std::uniform_real_distribution<> subtotalDist(20.0, 120.0);
    double totalSales = 0;
    
    int numChecks = 25 + getRng()() % 30;
    for (int i = 0; i < numChecks; ++i) {
        int row = checksTable_->rowCount();
        checksTable_->insertRow(row);
        
        double subtotal = subtotalDist(getRng());
        double tax = subtotal * 0.0825;
        double total = subtotal + tax;
        totalSales += total;
        
        checksTable_->setItem(row, 0, new QTableWidgetItem(QString("#%1").arg(1001 + i)));
        checksTable_->setItem(row, 1, new QTableWidgetItem(servers[i % servers.size()]));
        checksTable_->setItem(row, 2, new QTableWidgetItem(QString::number(1 + i % 20)));
        checksTable_->setItem(row, 3, new QTableWidgetItem(QString("$%1").arg(subtotal, 0, 'f', 2)));
        checksTable_->setItem(row, 4, new QTableWidgetItem(QString("$%1").arg(tax, 0, 'f', 2)));
        checksTable_->setItem(row, 5, new QTableWidgetItem(QString("$%1").arg(total, 0, 'f', 2)));
    }
    
    summaryLabel_->setText(QString("📊 %1 Checks | Total Sales: $%2 | Avg Check: $%3")
        .arg(numChecks).arg(totalSales, 0, 'f', 2).arg(totalSales/numChecks, 0, 'f', 2));
}

// ============================================================================
// Review Guest Checks Zone
// ============================================================================
ReviewGuestChecksZone::ReviewGuestChecksZone(QWidget* parent)
    : Zone(ZoneType::Report, parent) {
    setZoneName("Review Guest Checks");
    setupUI();
    loadSampleData();
}

void ReviewGuestChecksZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    titleLabel_ = new QLabel("🔍 Review Guest Checks");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    auto* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:"));
    searchEdit_ = new QLineEdit();
    searchEdit_->setPlaceholderText("Check #, server name, or table...");
    connect(searchEdit_, &QLineEdit::textChanged, this, &ReviewGuestChecksZone::onSearchChanged);
    searchLayout->addWidget(searchEdit_, 1);
    mainLayout_->addLayout(searchLayout);

    checksTable_ = new QTableWidget();
    checksTable_->setColumnCount(7);
    checksTable_->setHorizontalHeaderLabels({"Check #", "Time", "Server", "Table", "Items", "Total", "Payment"});
    checksTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    checksTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    checksTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(checksTable_, &QTableWidget::cellClicked, this, &ReviewGuestChecksZone::onCheckSelected);
    mainLayout_->addWidget(checksTable_, 1);

    detailLabel_ = new QLabel("Select a check to view details");
    detailLabel_->setAlignment(Qt::AlignCenter);
    detailLabel_->setMinimumHeight(100);
    mainLayout_->addWidget(detailLabel_);

    auto* btnLayout = new QHBoxLayout();
    reprintBtn_ = new QPushButton("🖨️ Reprint Check");
    reprintBtn_->setEnabled(false);
    connect(reprintBtn_, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(this, "Reprint", "Check would be sent to printer...");
    });
    btnLayout->addWidget(reprintBtn_);
    btnLayout->addStretch();
    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &ReviewGuestChecksZone::backRequested);
    btnLayout->addWidget(backBtn_);
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void ReviewGuestChecksZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void ReviewGuestChecksZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #2c3e50; padding: 10px;").arg(fs * 2));
    searchEdit_->setStyleSheet(QString("padding: %1px; font-size: %2px; border: 2px solid #bdc3c7; border-radius: 5px;").arg(fs).arg(fs));
    checksTable_->setStyleSheet(QString(R"(
        QTableWidget { background-color: white; border: 2px solid #bdc3c7; border-radius: 8px; font-size: %1px; }
        QTableWidget::item:selected { background-color: #3498db; color: white; }
        QHeaderView::section { background-color: #34495e; color: white; padding: %1px; font-weight: bold; border: none; }
    )").arg(fs));
    detailLabel_->setStyleSheet(QString("font-size: %1px; padding: 15px; background: #ecf0f1; border-radius: 8px;").arg(fs));
    reprintBtn_->setStyleSheet(getButtonStyle(fs, "#3498db", "#5dade2"));
    backBtn_->setStyleSheet(getButtonStyle(fs, "#7f8c8d", "#95a5a6"));
}

void ReviewGuestChecksZone::loadSampleData() {
    checksTable_->setRowCount(0);
    
    QStringList servers = {"John M.", "Jane S.", "Bob B.", "Alice C."};
    QStringList payments = {"Cash", "Visa ****1234", "MC ****5678", "Amex ****9012"};
    std::uniform_real_distribution<> totalDist(25.0, 150.0);
    
    for (int i = 0; i < 30; ++i) {
        int row = checksTable_->rowCount();
        checksTable_->insertRow(row);
        
        double total = totalDist(getRng());
        int items = 2 + getRng()() % 6;
        
        checksTable_->setItem(row, 0, new QTableWidgetItem(QString("#%1").arg(1001 + i)));
        checksTable_->setItem(row, 1, new QTableWidgetItem(QString("%1:%2").arg(11 + i/4).arg((i*13) % 60, 2, 10, QChar('0'))));
        checksTable_->setItem(row, 2, new QTableWidgetItem(servers[i % servers.size()]));
        checksTable_->setItem(row, 3, new QTableWidgetItem(QString::number(1 + i % 20)));
        checksTable_->setItem(row, 4, new QTableWidgetItem(QString::number(items)));
        checksTable_->setItem(row, 5, new QTableWidgetItem(QString("$%1").arg(total, 0, 'f', 2)));
        checksTable_->setItem(row, 6, new QTableWidgetItem(payments[i % payments.size()]));
    }
}

void ReviewGuestChecksZone::onSearchChanged(const QString& text) {
    for (int row = 0; row < checksTable_->rowCount(); ++row) {
        bool match = text.isEmpty();
        if (!match) {
            for (int col = 0; col < checksTable_->columnCount(); ++col) {
                if (checksTable_->item(row, col)->text().contains(text, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
        }
        checksTable_->setRowHidden(row, !match);
    }
}

void ReviewGuestChecksZone::onCheckSelected(int row, int col) {
    Q_UNUSED(col);
    reprintBtn_->setEnabled(true);
    
    QString checkNum = checksTable_->item(row, 0)->text();
    QString server = checksTable_->item(row, 2)->text();
    QString total = checksTable_->item(row, 5)->text();
    QString payment = checksTable_->item(row, 6)->text();
    
    detailLabel_->setText(QString("📋 Check %1\nServer: %2\nTotal: %3\nPayment: %4\n\n(Full item details would display here)")
        .arg(checkNum).arg(server).arg(total).arg(payment));
}

// ============================================================================
// Expenses View Zone
// ============================================================================
ExpensesViewZone::ExpensesViewZone(QWidget* parent)
    : Zone(ZoneType::Report, parent) {
    setZoneName("Expenses");
    setupUI();
    loadSampleData();
}

void ExpensesViewZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    titleLabel_ = new QLabel("💸 Expenses");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    expensesTable_ = new QTableWidget();
    expensesTable_->setColumnCount(5);
    expensesTable_->setHorizontalHeaderLabels({"Date", "Description", "Category", "Amount", "Vendor"});
    expensesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    expensesTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(expensesTable_, 1);

    totalLabel_ = new QLabel();
    totalLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(totalLabel_);

    auto* btnLayout = new QHBoxLayout();
    addBtn_ = new QPushButton("➕ Add Expense");
    connect(addBtn_, &QPushButton::clicked, this, &ExpensesViewZone::addExpenseRequested);
    btnLayout->addWidget(addBtn_);
    btnLayout->addStretch();
    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &ExpensesViewZone::backRequested);
    btnLayout->addWidget(backBtn_);
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void ExpensesViewZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void ExpensesViewZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #c0392b; padding: 10px;").arg(fs * 2));
    expensesTable_->setStyleSheet(QString(R"(
        QTableWidget { background-color: white; border: 2px solid #bdc3c7; border-radius: 8px; font-size: %1px; }
        QHeaderView::section { background-color: #c0392b; color: white; padding: %1px; font-weight: bold; border: none; }
    )").arg(fs));
    totalLabel_->setStyleSheet(QString("font-size: %1px; padding: 15px; background: #ecf0f1; border-radius: 8px;").arg(fs));
    addBtn_->setStyleSheet(getButtonStyle(fs, "#27ae60", "#2ecc71"));
    backBtn_->setStyleSheet(getButtonStyle(fs, "#7f8c8d", "#95a5a6"));
}

void ExpensesViewZone::loadSampleData() {
    expensesTable_->setRowCount(0);
    
    struct Expense { QString desc; QString cat; double amt; QString vendor; };
    std::vector<Expense> expenses = {
        {"Food supplies", "Inventory", 523.45, "Sysco"},
        {"Paper goods", "Supplies", 89.99, "Restaurant Depot"},
        {"Equipment repair", "Maintenance", 250.00, "Joe's Repair"},
        {"Cleaning supplies", "Supplies", 67.50, "Costco"},
        {"Beverage order", "Inventory", 412.00, "ABC Distributors"}
    };
    
    double total = 0;
    QDate today = QDate::currentDate();
    
    for (size_t i = 0; i < expenses.size(); ++i) {
        int row = expensesTable_->rowCount();
        expensesTable_->insertRow(row);
        
        total += expenses[i].amt;
        
        expensesTable_->setItem(row, 0, new QTableWidgetItem(today.addDays(-static_cast<int>(i)).toString("yyyy-MM-dd")));
        expensesTable_->setItem(row, 1, new QTableWidgetItem(expenses[i].desc));
        expensesTable_->setItem(row, 2, new QTableWidgetItem(expenses[i].cat));
        expensesTable_->setItem(row, 3, new QTableWidgetItem(QString("$%1").arg(expenses[i].amt, 0, 'f', 2)));
        expensesTable_->setItem(row, 4, new QTableWidgetItem(expenses[i].vendor));
    }
    
    totalLabel_->setText(QString("📊 Total Expenses (This Week): $%1").arg(total, 0, 'f', 2));
}

// ============================================================================
// Edit Menu Item Zone
// ============================================================================
EditMenuItemZone::EditMenuItemZone(QWidget* parent)
    : Zone(ZoneType::Settings, parent) {
    setZoneName("Edit Menu Items");
    setupUI();
    loadSampleData();
}

void EditMenuItemZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    titleLabel_ = new QLabel("✏️ Edit Menu Item Properties");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    // Split: list on left, edit on right
    auto* contentLayout = new QHBoxLayout();
    
    menuTable_ = new QTableWidget();
    menuTable_->setColumnCount(4);
    menuTable_->setHorizontalHeaderLabels({"Item", "Category", "Price", "Active"});
    menuTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    menuTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    menuTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(menuTable_, &QTableWidget::cellClicked, this, &EditMenuItemZone::onItemSelected);
    contentLayout->addWidget(menuTable_, 2);

    // Edit panel
    auto* editPanel = new QWidget();
    auto* editLayout = new QFormLayout(editPanel);
    editLayout->setSpacing(15);
    
    nameEdit_ = new QLineEdit();
    nameEdit_->setPlaceholderText("Item name");
    editLayout->addRow("Name:", nameEdit_);
    
    priceEdit_ = new QLineEdit();
    priceEdit_->setPlaceholderText("0.00");
    editLayout->addRow("Price:", priceEdit_);
    
    categoryEdit_ = new QLineEdit();
    categoryEdit_->setPlaceholderText("Category");
    editLayout->addRow("Category:", categoryEdit_);
    
    saveBtn_ = new QPushButton("💾 Save Changes");
    saveBtn_->setEnabled(false);
    connect(saveBtn_, &QPushButton::clicked, this, &EditMenuItemZone::onSaveChanges);
    editLayout->addRow(saveBtn_);
    
    contentLayout->addWidget(editPanel, 1);
    mainLayout_->addLayout(contentLayout, 1);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &EditMenuItemZone::backRequested);
    btnLayout->addWidget(backBtn_);
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void EditMenuItemZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void EditMenuItemZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #e67e22; padding: 10px;").arg(fs * 2));
    menuTable_->setStyleSheet(QString(R"(
        QTableWidget { background-color: white; border: 2px solid #bdc3c7; border-radius: 8px; font-size: %1px; }
        QTableWidget::item:selected { background-color: #e67e22; color: white; }
        QHeaderView::section { background-color: #e67e22; color: white; padding: %1px; font-weight: bold; border: none; }
    )").arg(fs));
    
    QString inputStyle = QString("padding: %1px; font-size: %2px; border: 2px solid #bdc3c7; border-radius: 5px;").arg(fs).arg(fs);
    nameEdit_->setStyleSheet(inputStyle);
    priceEdit_->setStyleSheet(inputStyle);
    categoryEdit_->setStyleSheet(inputStyle);
    
    saveBtn_->setStyleSheet(getButtonStyle(fs, "#27ae60", "#2ecc71"));
    backBtn_->setStyleSheet(getButtonStyle(fs, "#7f8c8d", "#95a5a6"));
}

void EditMenuItemZone::loadSampleData() {
    menuTable_->setRowCount(0);
    
    struct MenuItem { QString name; QString cat; double price; bool active; };
    std::vector<MenuItem> items = {
        {"Classic Burger", "Entrees", 14.99, true},
        {"Caesar Salad", "Appetizers", 9.99, true},
        {"Fish & Chips", "Entrees", 16.99, true},
        {"Buffalo Wings", "Appetizers", 12.99, true},
        {"Chocolate Cake", "Desserts", 7.99, true},
        {"House Wine", "Beverages", 8.00, true},
        {"Draft Beer", "Beverages", 6.00, true},
        {"Seasonal Special", "Entrees", 18.99, false}
    };
    
    for (const auto& item : items) {
        int row = menuTable_->rowCount();
        menuTable_->insertRow(row);
        
        menuTable_->setItem(row, 0, new QTableWidgetItem(item.name));
        menuTable_->setItem(row, 1, new QTableWidgetItem(item.cat));
        menuTable_->setItem(row, 2, new QTableWidgetItem(QString("$%1").arg(item.price, 0, 'f', 2)));
        menuTable_->setItem(row, 3, new QTableWidgetItem(item.active ? "✅" : "❌"));
    }
}

void EditMenuItemZone::onItemSelected(int row, int col) {
    Q_UNUSED(col);
    selectedRow_ = row;
    saveBtn_->setEnabled(true);
    
    nameEdit_->setText(menuTable_->item(row, 0)->text());
    categoryEdit_->setText(menuTable_->item(row, 1)->text());
    priceEdit_->setText(menuTable_->item(row, 2)->text().mid(1));  // Remove $
}

void EditMenuItemZone::onSaveChanges() {
    if (selectedRow_ < 0) return;
    
    menuTable_->item(selectedRow_, 0)->setText(nameEdit_->text());
    menuTable_->item(selectedRow_, 1)->setText(categoryEdit_->text());
    menuTable_->item(selectedRow_, 2)->setText(QString("$%1").arg(priceEdit_->text().toDouble(), 0, 'f', 2));
    
    VT_INFO("Updated menu item: {}", nameEdit_->text().toStdString());
    QMessageBox::information(this, "Saved", QString("Menu item '%1' updated successfully.").arg(nameEdit_->text()));
}

// ============================================================================
// Pay Captured Tips Zone
// ============================================================================
PayCapturedTipsZone::PayCapturedTipsZone(QWidget* parent)
    : Zone(ZoneType::Manager, parent) {
    setZoneName("Pay Captured Tips");
    setupUI();
    loadSampleData();
}

void PayCapturedTipsZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(20, 20, 20, 20);
    mainLayout_->setSpacing(15);

    titleLabel_ = new QLabel("💳 Pay Captured Credit Card Tips");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    tipsTable_ = new QTableWidget();
    tipsTable_->setColumnCount(5);
    tipsTable_->setHorizontalHeaderLabels({"Server", "Shifts", "Card Tips", "Cash Tips", "Total Due"});
    tipsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tipsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tipsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mainLayout_->addWidget(tipsTable_, 1);

    totalLabel_ = new QLabel();
    totalLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(totalLabel_);

    auto* btnLayout = new QHBoxLayout();
    paySelectedBtn_ = new QPushButton("💵 Pay Selected");
    connect(paySelectedBtn_, &QPushButton::clicked, this, &PayCapturedTipsZone::onPaySelected);
    btnLayout->addWidget(paySelectedBtn_);
    
    payAllBtn_ = new QPushButton("💰 Pay All");
    connect(payAllBtn_, &QPushButton::clicked, this, &PayCapturedTipsZone::onPayAll);
    btnLayout->addWidget(payAllBtn_);
    
    btnLayout->addStretch();
    backBtn_ = new QPushButton("⬅️ Back to Manager");
    connect(backBtn_, &QPushButton::clicked, this, &PayCapturedTipsZone::backRequested);
    btnLayout->addWidget(backBtn_);
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void PayCapturedTipsZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void PayCapturedTipsZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #27ae60; padding: 10px;").arg(fs * 2));
    tipsTable_->setStyleSheet(QString(R"(
        QTableWidget { background-color: white; border: 2px solid #bdc3c7; border-radius: 8px; font-size: %1px; }
        QTableWidget::item:selected { background-color: #27ae60; color: white; }
        QHeaderView::section { background-color: #27ae60; color: white; padding: %1px; font-weight: bold; border: none; }
    )").arg(fs));
    totalLabel_->setStyleSheet(QString("font-size: %1px; padding: 15px; background: #ecf0f1; border-radius: 8px;").arg(fs));
    paySelectedBtn_->setStyleSheet(getButtonStyle(fs, "#3498db", "#5dade2"));
    payAllBtn_->setStyleSheet(getButtonStyle(fs, "#27ae60", "#2ecc71"));
    backBtn_->setStyleSheet(getButtonStyle(fs, "#7f8c8d", "#95a5a6"));
}

void PayCapturedTipsZone::loadSampleData() {
    tipsTable_->setRowCount(0);
    
    QStringList servers = {"John Manager", "Jane Server", "Bob Bartender", "Alice Cashier"};
    std::uniform_real_distribution<> cardTipDist(50.0, 200.0);
    std::uniform_real_distribution<> cashTipDist(20.0, 80.0);
    
    double totalDue = 0;
    
    for (const auto& server : servers) {
        int row = tipsTable_->rowCount();
        tipsTable_->insertRow(row);
        
        int shifts = 3 + getRng()() % 4;
        double cardTips = cardTipDist(getRng());
        double cashTips = cashTipDist(getRng());
        double due = cardTips;  // Only card tips need to be paid out
        totalDue += due;
        
        tipsTable_->setItem(row, 0, new QTableWidgetItem(server));
        tipsTable_->setItem(row, 1, new QTableWidgetItem(QString::number(shifts)));
        tipsTable_->setItem(row, 2, new QTableWidgetItem(QString("$%1").arg(cardTips, 0, 'f', 2)));
        tipsTable_->setItem(row, 3, new QTableWidgetItem(QString("$%1").arg(cashTips, 0, 'f', 2)));
        tipsTable_->setItem(row, 4, new QTableWidgetItem(QString("$%1").arg(due, 0, 'f', 2)));
    }
    
    totalLabel_->setText(QString("💰 Total Card Tips to Pay: $%1").arg(totalDue, 0, 'f', 2));
}

void PayCapturedTipsZone::onPaySelected() {
    auto selected = tipsTable_->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, "No Selection", "Please select a server to pay.");
        return;
    }
    
    int row = selected.first()->row();
    QString server = tipsTable_->item(row, 0)->text();
    QString amount = tipsTable_->item(row, 4)->text();
    
    auto reply = QMessageBox::question(this, "Confirm Payment",
        QString("Pay %1 to %2?").arg(amount).arg(server),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        tipsTable_->removeRow(row);
        VT_INFO("Paid {} to {}", amount.toStdString(), server.toStdString());
        loadSampleData();  // Refresh
    }
}

void PayCapturedTipsZone::onPayAll() {
    auto reply = QMessageBox::question(this, "Confirm Payment",
        "Pay all captured tips to all servers?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        VT_INFO("Paid all captured tips");
        QMessageBox::information(this, "Tips Paid", "All captured tips have been paid out.");
        tipsTable_->setRowCount(0);
        totalLabel_->setText("💰 Total Card Tips to Pay: $0.00");
    }
}

// ============================================================================
// Record Expense Zone
// ============================================================================
RecordExpenseZone::RecordExpenseZone(QWidget* parent)
    : Zone(ZoneType::Manager, parent) {
    setZoneName("Record Expense");
    setupUI();
}

void RecordExpenseZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(40, 40, 40, 40);
    mainLayout_->setSpacing(20);

    titleLabel_ = new QLabel("📝 Record New Expense");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    auto* formLayout = new QFormLayout();
    formLayout->setSpacing(15);

    descriptionEdit_ = new QLineEdit();
    descriptionEdit_->setPlaceholderText("What was purchased?");
    formLayout->addRow("Description:", descriptionEdit_);

    amountEdit_ = new QLineEdit();
    amountEdit_->setPlaceholderText("0.00");
    formLayout->addRow("Amount ($):", amountEdit_);

    categoryCombo_ = new QComboBox();
    categoryCombo_->addItems({"Inventory", "Supplies", "Maintenance", "Utilities", "Marketing", "Other"});
    formLayout->addRow("Category:", categoryCombo_);

    vendorEdit_ = new QLineEdit();
    vendorEdit_->setPlaceholderText("Vendor name");
    formLayout->addRow("Vendor:", vendorEdit_);

    paymentMethodCombo_ = new QComboBox();
    paymentMethodCombo_->addItems({"Cash", "Company Card", "Check", "Transfer"});
    formLayout->addRow("Payment:", paymentMethodCombo_);

    mainLayout_->addLayout(formLayout);
    mainLayout_->addStretch();

    auto* btnLayout = new QHBoxLayout();
    saveBtn_ = new QPushButton("💾 Save Expense");
    connect(saveBtn_, &QPushButton::clicked, this, &RecordExpenseZone::onSaveExpense);
    btnLayout->addWidget(saveBtn_);
    
    cancelBtn_ = new QPushButton("❌ Cancel");
    connect(cancelBtn_, &QPushButton::clicked, this, &RecordExpenseZone::backRequested);
    btnLayout->addWidget(cancelBtn_);
    
    btnLayout->addStretch();
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void RecordExpenseZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void RecordExpenseZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #c0392b; padding: 10px;").arg(fs * 2));
    
    QString inputStyle = QString("padding: %1px; font-size: %2px; border: 2px solid #bdc3c7; border-radius: 5px; min-width: 300px;").arg(fs).arg(fs);
    descriptionEdit_->setStyleSheet(inputStyle);
    amountEdit_->setStyleSheet(inputStyle);
    vendorEdit_->setStyleSheet(inputStyle);
    categoryCombo_->setStyleSheet(QString("padding: %1px; font-size: %2px;").arg(fs).arg(fs));
    paymentMethodCombo_->setStyleSheet(QString("padding: %1px; font-size: %2px;").arg(fs).arg(fs));
    
    saveBtn_->setStyleSheet(getButtonStyle(fs, "#27ae60", "#2ecc71"));
    cancelBtn_->setStyleSheet(getButtonStyle(fs, "#e74c3c", "#ec7063"));
}

void RecordExpenseZone::onSaveExpense() {
    if (descriptionEdit_->text().isEmpty() || amountEdit_->text().isEmpty()) {
        QMessageBox::warning(this, "Missing Info", "Please enter description and amount.");
        return;
    }
    
    VT_INFO("Recorded expense: {} - ${}", descriptionEdit_->text().toStdString(), amountEdit_->text().toStdString());
    
    QMessageBox::information(this, "Expense Recorded",
        QString("Expense recorded:\n\n%1\nAmount: $%2\nCategory: %3")
            .arg(descriptionEdit_->text())
            .arg(amountEdit_->text())
            .arg(categoryCombo_->currentText()));
    
    emit expenseRecorded();
    emit backRequested();
}

// ============================================================================
// End Day Zone
// ============================================================================
EndDayZone::EndDayZone(QWidget* parent)
    : Zone(ZoneType::Manager, parent) {
    setZoneName("End Day");
    setupUI();
    loadDaySummary();
}

void EndDayZone::setupUI() {
    mainLayout_ = new QVBoxLayout(this);
    mainLayout_->setContentsMargins(40, 40, 40, 40);
    mainLayout_->setSpacing(20);

    titleLabel_ = new QLabel("🏁 End Business Day");
    titleLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(titleLabel_);

    // Summaries
    salesSummary_ = new QLabel();
    salesSummary_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(salesSummary_);

    paymentSummary_ = new QLabel();
    paymentSummary_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(paymentSummary_);

    laborSummary_ = new QLabel();
    laborSummary_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(laborSummary_);

    warningsLabel_ = new QLabel();
    warningsLabel_->setAlignment(Qt::AlignCenter);
    mainLayout_->addWidget(warningsLabel_);

    mainLayout_->addStretch();

    auto* btnLayout = new QHBoxLayout();
    confirmBtn_ = new QPushButton("✅ Confirm End Day");
    connect(confirmBtn_, &QPushButton::clicked, this, &EndDayZone::onConfirmEndDay);
    btnLayout->addWidget(confirmBtn_);
    
    cancelBtn_ = new QPushButton("❌ Cancel");
    connect(cancelBtn_, &QPushButton::clicked, this, &EndDayZone::backRequested);
    btnLayout->addWidget(cancelBtn_);
    
    btnLayout->addStretch();
    mainLayout_->addLayout(btnLayout);

    updateSizes();
}

void EndDayZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void EndDayZone::updateSizes() {
    int h = height();
    int fs = qMax(14, h / 50);

    titleLabel_->setStyleSheet(QString("font-size: %1px; font-weight: bold; color: #2c3e50; padding: 20px;").arg(fs * 2));
    
    QString summaryStyle = QString("font-size: %1px; padding: 20px; background: #ecf0f1; border-radius: 10px; min-width: 400px;").arg(fs * 1.2);
    salesSummary_->setStyleSheet(summaryStyle);
    paymentSummary_->setStyleSheet(summaryStyle);
    laborSummary_->setStyleSheet(summaryStyle);
    warningsLabel_->setStyleSheet(QString("font-size: %1px; padding: 15px; color: #c0392b;").arg(fs));
    
    confirmBtn_->setStyleSheet(getButtonStyle(fs * 1.2, "#27ae60", "#2ecc71"));
    cancelBtn_->setStyleSheet(getButtonStyle(fs * 1.2, "#e74c3c", "#ec7063"));
}

void EndDayZone::loadDaySummary() {
    std::uniform_real_distribution<> salesDist(4000.0, 8000.0);
    double sales = salesDist(getRng());
    
    salesSummary_->setText(QString("💵 SALES SUMMARY\n\nGross Sales: $%1\nDiscounts: $%2\nNet Sales: $%3\nTax Collected: $%4")
        .arg(sales, 0, 'f', 2)
        .arg(sales * 0.03, 0, 'f', 2)
        .arg(sales * 0.97, 0, 'f', 2)
        .arg(sales * 0.0825, 0, 'f', 2));

    double cash = sales * 0.25;
    double card = sales * 0.75;
    paymentSummary_->setText(QString("💳 PAYMENTS\n\nCash: $%1\nCredit Cards: $%2\nGift Cards: $%3")
        .arg(cash, 0, 'f', 2)
        .arg(card, 0, 'f', 2)
        .arg(sales * 0.02, 0, 'f', 2));

    laborSummary_->setText(QString("👷 LABOR\n\nTotal Hours: 48\nLabor Cost: $%1\nLabor %%: %2%%")
        .arg(sales * 0.28, 0, 'f', 2)
        .arg(28));

    // Check for warnings
    int openChecks = getRng()() % 3;
    int unbalancedTills = getRng()() % 2;
    
    if (openChecks > 0 || unbalancedTills > 0) {
        QString warnings;
        if (openChecks > 0) warnings += QString("⚠️ %1 open checks\n").arg(openChecks);
        if (unbalancedTills > 0) warnings += QString("⚠️ %1 unbalanced tills\n").arg(unbalancedTills);
        warningsLabel_->setText(warnings);
        warningsLabel_->show();
    } else {
        warningsLabel_->hide();
    }
}

void EndDayZone::onConfirmEndDay() {
    auto reply = QMessageBox::question(this, "Confirm End Day",
        "Are you sure you want to end the business day?\n\n"
        "This will:\n"
        "• Close all open checks\n"
        "• Generate end-of-day reports\n"
        "• Reset daily totals\n\n"
        "This action cannot be undone.",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        VT_INFO("END DAY CONFIRMED at {}", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toStdString());
        
        QMessageBox::information(this, "Day Ended",
            "Business day has been closed.\n\n"
            "End-of-day reports have been generated.\n"
            "Daily totals have been reset.");
        
        emit endDayConfirmed();
        emit backRequested();
    }
}

} // namespace vt2
