/**
 * @file manager_zone.cpp
 * @brief Manager zone implementation
 */

#include "zones/manager_zone.hpp"
#include "core/types.hpp"
#include "core/logger.hpp"
#include <QPainter>
#include <QResizeEvent>

namespace vt2 {

ManagerZone::ManagerZone(QWidget* parent)
    : Zone(ZoneType::Manager, parent) {
    setZoneName("Manager");
    setupUI();
}

ManagerZone::~ManagerZone() = default;

void ManagerZone::setupUI() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(15, 15, 15, 15);
    m_mainLayout->setSpacing(15);
    
    // Title
    m_titleLabel = new QLabel("Manager Functions", this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(
        "color: white; font-size: 28px; font-weight: bold; "
        "background: transparent; padding: 5px;"
    );
    m_mainLayout->addWidget(m_titleLabel);
    
    // Scrollable button area
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 12px; background: #333; }"
        "QScrollBar::handle:vertical { background: #666; border-radius: 6px; min-height: 40px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );
    
    m_buttonContainer = new QWidget();
    m_buttonContainer->setStyleSheet("background: transparent;");
    m_buttonLayout = new QGridLayout(m_buttonContainer);
    m_buttonLayout->setSpacing(12);
    m_buttonLayout->setContentsMargins(5, 5, 5, 5);
    
    // Row 0: User & Till Management (important functions)
    m_userManagerBtn = createManagerButton("User Manager\n👥\nManage Employees & Roles", colors::VTBlue);
    m_buttonLayout->addWidget(m_userManagerBtn, 0, 0);
    connect(m_userManagerBtn, &QPushButton::clicked, this, &ManagerZone::userManagerRequested);
    
    m_balanceTillsBtn = createManagerButton("Balance Tills\n💰\nBalance Server Tills", colors::VTGreen);
    m_buttonLayout->addWidget(m_balanceTillsBtn, 0, 1);
    connect(m_balanceTillsBtn, &QPushButton::clicked, this, &ManagerZone::balanceTillsRequested);
    
    m_auditBtn = createManagerButton("Audit\n📊\nSales Reports & History", colors::Purple);
    m_buttonLayout->addWidget(m_auditBtn, 0, 2);
    connect(m_auditBtn, &QPushButton::clicked, this, &ManagerZone::auditRequested);
    
    // Row 1: Performance & Revenue
    auto* menuPerfBtn = createManagerButton("Menu Item\nPerformance\n📈", colors::Teal);
    m_buttonLayout->addWidget(menuPerfBtn, 1, 0);
    connect(menuPerfBtn, &QPushButton::clicked, this, &ManagerZone::menuItemPerformanceRequested);
    
    auto* revenueBtn = createManagerButton("Today's Revenue\n& Productivity\n💵", colors::VTGreen);
    m_buttonLayout->addWidget(revenueBtn, 1, 1);
    connect(revenueBtn, &QPushButton::clicked, this, &ManagerZone::todaysRevenueRequested);
    
    auto* exceptionalBtn = createManagerButton("Exceptional\nTransactions\n⚠️", colors::Orange);
    m_buttonLayout->addWidget(exceptionalBtn, 1, 2);
    connect(exceptionalBtn, &QPushButton::clicked, this, &ManagerZone::exceptionalTransactionsRequested);
    
    // Row 2: Traffic & Receipts
    auto* franchiseBtn = createManagerButton("Franchise\nTraffic\n🏪", colors::VTBlue);
    m_buttonLayout->addWidget(franchiseBtn, 2, 0);
    connect(franchiseBtn, &QPushButton::clicked, this, &ManagerZone::franchiseTrafficRequested);
    
    auto* receiptsBtn = createManagerButton("Receipts Balance\n& Cash Deposits\n🧾", colors::Teal);
    m_buttonLayout->addWidget(receiptsBtn, 2, 1);
    connect(receiptsBtn, &QPushButton::clicked, this, &ManagerZone::receiptsBalanceRequested);
    
    auto* closedCheckBtn = createManagerButton("Closed Check\nSummary\n📅", colors::Purple);
    m_buttonLayout->addWidget(closedCheckBtn, 2, 2);
    connect(closedCheckBtn, &QPushButton::clicked, this, &ManagerZone::closedCheckSummaryRequested);
    
    // Row 3: Review & Edit
    auto* guestChecksBtn = createManagerButton("Review\nGuest Checks\n🔍", colors::VTBlue);
    m_buttonLayout->addWidget(guestChecksBtn, 3, 0);
    connect(guestChecksBtn, &QPushButton::clicked, this, &ManagerZone::reviewGuestChecksRequested);
    
    auto* expensesBtn = createManagerButton("Expenses\n💸\nView Expenses", colors::Orange);
    m_buttonLayout->addWidget(expensesBtn, 3, 1);
    connect(expensesBtn, &QPushButton::clicked, this, &ManagerZone::expensesRequested);
    
    auto* editMenuBtn = createManagerButton("Edit Menu Item\nProperties\n📝", colors::Teal);
    m_buttonLayout->addWidget(editMenuBtn, 3, 2);
    connect(editMenuBtn, &QPushButton::clicked, this, &ManagerZone::editMenuItemPropertiesRequested);
    
    // Row 4: End of Day Operations
    auto* payTipsBtn = createManagerButton("Pay Captured\nTips\n💳", colors::VTGreen);
    m_buttonLayout->addWidget(payTipsBtn, 4, 0);
    connect(payTipsBtn, &QPushButton::clicked, this, &ManagerZone::payCapturedTipsRequested);
    
    auto* recordExpBtn = createManagerButton("Record\nExpenses\n✏️", colors::Purple);
    m_buttonLayout->addWidget(recordExpBtn, 4, 1);
    connect(recordExpBtn, &QPushButton::clicked, this, &ManagerZone::recordExpensesRequested);
    
    m_endDayBtn = createManagerButton("End Day\n🌙\nClose Business Day", colors::VTRed);
    m_buttonLayout->addWidget(m_endDayBtn, 4, 2);
    connect(m_endDayBtn, &QPushButton::clicked, this, &ManagerZone::endDayRequested);
    
    m_scrollArea->setWidget(m_buttonContainer);
    m_mainLayout->addWidget(m_scrollArea, 1);
    
    // Back button at bottom
    m_backBtn = new QPushButton("← Back to Login", this);
    m_backBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_backBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #555;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 8px;"
        "  font-size: 18px;"
        "  font-weight: bold;"
        "  padding: 12px;"
        "}"
        "QPushButton:hover { background-color: #666; }"
        "QPushButton:pressed { background-color: #444; }"
    );
    m_mainLayout->addWidget(m_backBtn);
    connect(m_backBtn, &QPushButton::clicked, this, &ManagerZone::backRequested);
}

QPushButton* ManagerZone::createManagerButton(const QString& text, const QColor& color) {
    auto* btn = new QPushButton(text, this);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    btn->setMinimumHeight(100);
    
    QString styleSheet = QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 10px;"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "  padding: 15px;"
        "}"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %3; }"
    ).arg(color.name())
     .arg(color.lighter(115).name())
     .arg(color.darker(115).name());
    
    btn->setStyleSheet(styleSheet);
    m_allButtons.push_back(btn);
    return btn;
}

void ManagerZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void ManagerZone::updateSizes() {
    int w = width();
    int h = height();
    
    int margin = qMax(10, w / 60);
    int spacing = qMax(8, w / 80);
    
    m_mainLayout->setContentsMargins(margin, margin, margin, margin);
    m_mainLayout->setSpacing(spacing);
    m_buttonLayout->setSpacing(spacing);
    
    // Title font scaling
    int titleFontSize = qMax(20, h / 30);
    m_titleLabel->setStyleSheet(QString(
        "color: white; font-size: %1px; font-weight: bold; "
        "background: transparent; padding: 5px;"
    ).arg(titleFontSize));
    
    // Button font and size scaling
    int btnFontSize = qMax(13, qMin(w, h) / 40);
    int btnMinHeight = qMax(90, h / 8);
    int borderRadius = qMax(8, qMin(w, h) / 80);
    int padding = qMax(10, qMin(w, h) / 60);
    
    for (auto* btn : m_allButtons) {
        btn->setMinimumHeight(btnMinHeight);
        QString currentStyle = btn->styleSheet();
        currentStyle.replace(QRegularExpression("font-size: \\d+px"), 
                            QString("font-size: %1px").arg(btnFontSize));
        currentStyle.replace(QRegularExpression("border-radius: \\d+px"), 
                            QString("border-radius: %1px").arg(borderRadius));
        currentStyle.replace(QRegularExpression("padding: \\d+px"), 
                            QString("padding: %1px").arg(padding));
        btn->setStyleSheet(currentStyle);
    }
    
    // Back button
    int backFontSize = qMax(14, h / 45);
    m_backBtn->setStyleSheet(QString(
        "QPushButton {"
        "  background-color: #555;"
        "  color: white;"
        "  border: none;"
        "  border-radius: %1px;"
        "  font-size: %2px;"
        "  font-weight: bold;"
        "  padding: %3px;"
        "}"
        "QPushButton:hover { background-color: #666; }"
        "QPushButton:pressed { background-color: #444; }"
    ).arg(borderRadius).arg(backFontSize).arg(padding));
    
    m_backBtn->setFixedHeight(qMax(45, h / 15));
}

void ManagerZone::drawContent(QPainter& painter) {
    Q_UNUSED(painter);
}

} // namespace vt2
