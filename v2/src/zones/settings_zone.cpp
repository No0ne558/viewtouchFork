/**
 * @file settings_zone.cpp
 * @brief Settings zone implementation
 */

#include "zones/settings_zone.hpp"
#include "core/types.hpp"
#include "core/logger.hpp"
#include <QPainter>
#include <QResizeEvent>

namespace vt2 {

// ============================================================================
// SettingsZone Implementation
// ============================================================================

SettingsZone::SettingsZone(QWidget* parent)
    : Zone(ZoneType::Settings, parent) {
    setZoneName("Settings");
    setupUI();
}

SettingsZone::~SettingsZone() = default;

void SettingsZone::setupUI() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(20, 20, 20, 20);
    m_mainLayout->setSpacing(20);
    
    // Title
    m_titleLabel = new QLabel("System Settings", this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(
        "color: white; font-size: 32px; font-weight: bold; "
        "background: transparent; padding: 10px;"
    );
    m_mainLayout->addWidget(m_titleLabel);
    
    // Subtitle/warning
    auto* subtitleLabel = new QLabel("Superuser Access Only", this);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet(
        "color: #ff6b6b; font-size: 16px; font-style: italic; "
        "background: transparent;"
    );
    m_mainLayout->addWidget(subtitleLabel);
    
    m_mainLayout->addSpacing(30);
    
    // Button grid
    m_buttonLayout = new QGridLayout();
    m_buttonLayout->setSpacing(20);
    
    // Hardware button
    m_hardwareBtn = createSettingsButton("Hardware\n\nDisplays & Printers", QColor(0, 150, 136));
    m_buttonLayout->addWidget(m_hardwareBtn, 0, 0);
    connect(m_hardwareBtn, &QPushButton::clicked, this, &SettingsZone::hardwareRequested);
    
    // Tax button
    m_taxBtn = createSettingsButton("Tax\n\nTax Rates & Rules", QColor(63, 81, 181));
    m_buttonLayout->addWidget(m_taxBtn, 0, 1);
    connect(m_taxBtn, &QPushButton::clicked, this, &SettingsZone::taxRequested);
    
    // Clear System button - red/dangerous
    m_clearSystemBtn = createSettingsButton("Clear System\n\n⚠ Database Reset", QColor(198, 40, 40));
    m_buttonLayout->addWidget(m_clearSystemBtn, 1, 0);
    connect(m_clearSystemBtn, &QPushButton::clicked, this, &SettingsZone::clearSystemRequested);
    
    // Placeholder for future settings
    auto* placeholderBtn = createSettingsButton("More Settings\n\nComing Soon", QColor(80, 80, 80));
    placeholderBtn->setEnabled(false);
    m_buttonLayout->addWidget(placeholderBtn, 1, 1);
    
    m_mainLayout->addLayout(m_buttonLayout, 1);
    
    // Back button at bottom
    m_backBtn = new QPushButton("← Back to Login", this);
    m_backBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_backBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #555;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 8px;"
        "  font-size: 20px;"
        "  font-weight: bold;"
        "  padding: 15px;"
        "}"
        "QPushButton:hover { background-color: #666; }"
        "QPushButton:pressed { background-color: #444; }"
    );
    m_mainLayout->addWidget(m_backBtn);
    connect(m_backBtn, &QPushButton::clicked, this, &SettingsZone::backRequested);
    
    m_allButtons = {m_hardwareBtn, m_taxBtn, m_clearSystemBtn, placeholderBtn};
}

QPushButton* SettingsZone::createSettingsButton(const QString& text, const QColor& color) {
    auto* btn = new QPushButton(text, this);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    QString styleSheet = QString(
        "QPushButton {"
        "  background-color: %1;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 12px;"
        "  font-size: 24px;"
        "  font-weight: bold;"
        "  padding: 20px;"
        "}"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %3; }"
        "QPushButton:disabled { background-color: #444; color: #888; }"
    ).arg(color.name())
     .arg(color.lighter(115).name())
     .arg(color.darker(115).name());
    
    btn->setStyleSheet(styleSheet);
    return btn;
}

void SettingsZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void SettingsZone::updateSizes() {
    int w = width();
    int h = height();
    
    int margin = qMax(20, w / 40);
    int spacing = qMax(15, w / 60);
    
    m_mainLayout->setContentsMargins(margin, margin, margin, margin);
    m_mainLayout->setSpacing(spacing);
    m_buttonLayout->setSpacing(spacing);
    
    // Title font scaling
    int titleFontSize = qMax(24, h / 20);
    m_titleLabel->setStyleSheet(QString(
        "color: white; font-size: %1px; font-weight: bold; "
        "background: transparent; padding: 10px;"
    ).arg(titleFontSize));
    
    // Button font scaling
    int btnFontSize = qMax(18, qMin(w, h) / 20);
    int borderRadius = qMax(8, qMin(w, h) / 60);
    int padding = qMax(15, qMin(w, h) / 40);
    
    for (auto* btn : m_allButtons) {
        QString currentStyle = btn->styleSheet();
        // Update just the font-size, padding, and border-radius
        currentStyle.replace(QRegularExpression("font-size: \\d+px"), 
                            QString("font-size: %1px").arg(btnFontSize));
        currentStyle.replace(QRegularExpression("border-radius: \\d+px"), 
                            QString("border-radius: %1px").arg(borderRadius));
        currentStyle.replace(QRegularExpression("padding: \\d+px"), 
                            QString("padding: %1px").arg(padding));
        btn->setStyleSheet(currentStyle);
    }
    
    // Back button
    int backFontSize = qMax(16, h / 35);
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
    
    m_backBtn->setFixedHeight(qMax(50, h / 12));
}

void SettingsZone::drawContent(QPainter& painter) {
    Q_UNUSED(painter);
}

// ============================================================================
// ClearSystemZone Implementation
// ============================================================================

ClearSystemZone::ClearSystemZone(QWidget* parent)
    : Zone(ZoneType::Settings, parent) {
    setZoneName("Clear System");
    setupUI();
}

ClearSystemZone::~ClearSystemZone() = default;

void ClearSystemZone::setupUI() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(40, 40, 40, 40);
    m_mainLayout->setSpacing(20);
    
    // Title
    m_titleLabel = new QLabel("⚠ CLEAR SYSTEM ⚠", this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet(
        "color: #ff6b6b; font-size: 36px; font-weight: bold; "
        "background: transparent;"
    );
    m_mainLayout->addWidget(m_titleLabel);
    
    // Warning message
    m_warningLabel = new QLabel(
        "This will clear the database.\n\n"
        "The following will be DELETED:\n"
        "• All checks and orders\n"
        "• All transaction history\n"
        "• All reports data\n\n"
        "The following will be KEPT:\n"
        "• Menu items\n"
        "• Employees\n"
        "• System settings",
        this
    );
    m_warningLabel->setAlignment(Qt::AlignCenter);
    m_warningLabel->setStyleSheet(
        "color: #ddd; font-size: 18px; background: #333; "
        "border-radius: 10px; padding: 20px;"
    );
    m_warningLabel->setWordWrap(true);
    m_mainLayout->addWidget(m_warningLabel);
    
    m_mainLayout->addStretch();
    
    // Instruction
    m_instructionLabel = new QLabel("Tap the button below 10 times to confirm", this);
    m_instructionLabel->setAlignment(Qt::AlignCenter);
    m_instructionLabel->setStyleSheet(
        "color: #ffa500; font-size: 20px; font-weight: bold; "
        "background: transparent;"
    );
    m_mainLayout->addWidget(m_instructionLabel);
    
    // Tap count display
    m_tapCountLabel = new QLabel("0 / 10", this);
    m_tapCountLabel->setAlignment(Qt::AlignCenter);
    m_tapCountLabel->setStyleSheet(
        "color: white; font-size: 48px; font-weight: bold; "
        "background: transparent;"
    );
    m_mainLayout->addWidget(m_tapCountLabel);
    
    // Clear button
    m_clearBtn = new QPushButton("TAP TO CONFIRM", this);
    m_clearBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_clearBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #c62828;"
        "  color: white;"
        "  border: 3px solid #ff5252;"
        "  border-radius: 12px;"
        "  font-size: 28px;"
        "  font-weight: bold;"
        "  padding: 25px;"
        "}"
        "QPushButton:hover { background-color: #d32f2f; }"
        "QPushButton:pressed { background-color: #b71c1c; }"
    );
    m_mainLayout->addWidget(m_clearBtn);
    connect(m_clearBtn, &QPushButton::clicked, this, &ClearSystemZone::onClearTapped);
    
    m_mainLayout->addStretch();
    
    // Back button
    m_backBtn = new QPushButton("← Cancel", this);
    m_backBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_backBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #555;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 8px;"
        "  font-size: 20px;"
        "  font-weight: bold;"
        "  padding: 15px;"
        "}"
        "QPushButton:hover { background-color: #666; }"
        "QPushButton:pressed { background-color: #444; }"
    );
    m_mainLayout->addWidget(m_backBtn);
    connect(m_backBtn, &QPushButton::clicked, this, [this]() {
        resetTapCount();
        emit backRequested();
    });
}

void ClearSystemZone::onClearTapped() {
    m_tapCount++;
    m_tapCountLabel->setText(QString("%1 / %2").arg(m_tapCount).arg(REQUIRED_TAPS));
    
    VT_INFO("Clear system tap: {} / {}", m_tapCount, REQUIRED_TAPS);
    
    // Update button color as we get closer
    if (m_tapCount >= 8) {
        m_clearBtn->setStyleSheet(
            "QPushButton {"
            "  background-color: #b71c1c;"
            "  color: white;"
            "  border: 3px solid #ff1744;"
            "  border-radius: 12px;"
            "  font-size: 28px;"
            "  font-weight: bold;"
            "  padding: 25px;"
            "}"
        );
        m_clearBtn->setText("⚠ FINAL WARNING ⚠");
    } else if (m_tapCount >= 5) {
        m_clearBtn->setStyleSheet(
            "QPushButton {"
            "  background-color: #d32f2f;"
            "  color: white;"
            "  border: 3px solid #ff5252;"
            "  border-radius: 12px;"
            "  font-size: 28px;"
            "  font-weight: bold;"
            "  padding: 25px;"
            "}"
        );
        m_clearBtn->setText("KEEP TAPPING...");
    }
    
    if (m_tapCount >= REQUIRED_TAPS) {
        VT_WARN("Clear system CONFIRMED after {} taps", REQUIRED_TAPS);
        m_clearBtn->setText("CLEARING...");
        m_clearBtn->setEnabled(false);
        emit clearConfirmed();
    }
}

void ClearSystemZone::resetTapCount() {
    m_tapCount = 0;
    m_tapCountLabel->setText("0 / 10");
    m_clearBtn->setText("TAP TO CONFIRM");
    m_clearBtn->setEnabled(true);
    m_clearBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #c62828;"
        "  color: white;"
        "  border: 3px solid #ff5252;"
        "  border-radius: 12px;"
        "  font-size: 28px;"
        "  font-weight: bold;"
        "  padding: 25px;"
        "}"
        "QPushButton:hover { background-color: #d32f2f; }"
        "QPushButton:pressed { background-color: #b71c1c; }"
    );
}

void ClearSystemZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void ClearSystemZone::updateSizes() {
    int w = width();
    int h = height();
    
    int margin = qMax(30, w / 30);
    m_mainLayout->setContentsMargins(margin, margin, margin, margin);
    
    // Title
    int titleFontSize = qMax(28, h / 18);
    m_titleLabel->setStyleSheet(QString(
        "color: #ff6b6b; font-size: %1px; font-weight: bold; "
        "background: transparent;"
    ).arg(titleFontSize));
    
    // Warning
    int warningFontSize = qMax(14, h / 40);
    m_warningLabel->setStyleSheet(QString(
        "color: #ddd; font-size: %1px; background: #333; "
        "border-radius: 10px; padding: 20px;"
    ).arg(warningFontSize));
    
    // Tap count
    int tapFontSize = qMax(36, h / 15);
    m_tapCountLabel->setStyleSheet(QString(
        "color: white; font-size: %1px; font-weight: bold; "
        "background: transparent;"
    ).arg(tapFontSize));
    
    // Clear button height
    m_clearBtn->setFixedHeight(qMax(80, h / 8));
    m_backBtn->setFixedHeight(qMax(50, h / 14));
}

void ClearSystemZone::drawContent(QPainter& painter) {
    Q_UNUSED(painter);
}

} // namespace vt2
