/**
 * @file login_zone.cpp
 * @brief Employee login zone with PIN keypad implementation
 */

#include "zones/login_zone.hpp"
#include "core/logger.hpp"
#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFont>
#include <QResizeEvent>

namespace vt2 {

LoginZone::LoginZone(QWidget* parent)
    : Zone(ZoneType::Login, parent) {
    setZoneName("Login");
    setupKeypad();
}

LoginZone::~LoginZone() = default;

void LoginZone::setupKeypad() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);
    
    // PIN display field (masked)
    m_pinDisplay = new QLineEdit(this);
    m_pinDisplay->setReadOnly(true);
    m_pinDisplay->setAlignment(Qt::AlignCenter);
    m_pinDisplay->setEchoMode(QLineEdit::Password);
    m_pinDisplay->setMaxLength(MAX_PIN_LENGTH);
    m_pinDisplay->setPlaceholderText("Enter PIN");
    mainLayout->addWidget(m_pinDisplay);
    
    // Error label (hidden by default)
    m_errorLabel = new QLabel(this);
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->hide();
    mainLayout->addWidget(m_errorLabel);
    
    // Keypad container
    m_keypadWidget = new QWidget(this);
    m_keypadLayout = new QGridLayout(m_keypadWidget);
    m_keypadLayout->setSpacing(8);
    m_keypadLayout->setContentsMargins(0, 0, 0, 0);
    
    // Create digit buttons 1-9
    for (int i = 1; i <= 9; ++i) {
        auto* btn = createKeypadButton(QString::number(i));
        int row = (i - 1) / 3;
        int col = (i - 1) % 3;
        m_keypadLayout->addWidget(btn, row, col);
        m_allButtons.push_back(btn);
        
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onDigitPressed(i);
        });
    }
    
    // Bottom row: Clear, 0, Backspace
    m_clearBtn = createKeypadButton("CLR");
    m_keypadLayout->addWidget(m_clearBtn, 3, 0);
    m_allButtons.push_back(m_clearBtn);
    connect(m_clearBtn, &QPushButton::clicked, this, &LoginZone::onClearPressed);
    
    auto* zeroBtn = createKeypadButton("0");
    m_keypadLayout->addWidget(zeroBtn, 3, 1);
    m_allButtons.push_back(zeroBtn);
    connect(zeroBtn, &QPushButton::clicked, this, [this]() {
        onDigitPressed(0);
    });
    
    m_backBtn = createKeypadButton("⌫");
    m_keypadLayout->addWidget(m_backBtn, 3, 2);
    m_allButtons.push_back(m_backBtn);
    connect(m_backBtn, &QPushButton::clicked, this, &LoginZone::onBackspacePressed);
    
    mainLayout->addWidget(m_keypadWidget, 1);  // Give it stretch
    mainLayout->addStretch(0);
}

QPushButton* LoginZone::createKeypadButton(const QString& text) {
    auto* btn = new QPushButton(text, this);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return btn;
}

void LoginZone::resizeEvent(QResizeEvent* event) {
    Zone::resizeEvent(event);
    updateSizes();
}

void LoginZone::updateSizes() {
    int w = width();
    int h = height();
    
    // Calculate sizes based on zone dimensions
    int margin = qMax(10, w / 30);
    int spacing = qMax(5, w / 50);
    
    // Update main layout
    if (auto* mainLayout = qobject_cast<QVBoxLayout*>(layout())) {
        mainLayout->setContentsMargins(margin, margin, margin, margin);
        mainLayout->setSpacing(spacing);
    }
    
    // PIN display sizing
    int pinHeight = qMax(50, h / 10);
    int pinFontSize = qMax(18, h / 20);
    m_pinDisplay->setFixedHeight(pinHeight);
    QFont pinFont = m_pinDisplay->font();
    pinFont.setPointSize(pinFontSize);
    pinFont.setLetterSpacing(QFont::AbsoluteSpacing, pinFontSize / 4);
    m_pinDisplay->setFont(pinFont);
    m_pinDisplay->setStyleSheet(QString(
        "QLineEdit {"
        "  background-color: #2a2a3a;"
        "  color: white;"
        "  border: 2px solid #4a4a5a;"
        "  border-radius: %1px;"
        "  padding: %2px;"
        "}"
    ).arg(pinHeight / 8).arg(pinHeight / 8));
    
    // Error label sizing
    int errorFontSize = qMax(12, h / 40);
    m_errorLabel->setFixedHeight(qMax(20, h / 30));
    m_errorLabel->setStyleSheet(QString(
        "color: #ff6b6b; font-size: %1px; font-weight: bold;"
    ).arg(errorFontSize));
    
    // Keypad layout spacing
    m_keypadLayout->setSpacing(qMax(5, w / 40));
    
    // Button sizes and fonts
    int btnFontSize = qMax(16, qMin(w, h) / 12);
    int borderRadius = qMax(4, qMin(w, h) / 60);
    
    QString digitStyle = QString(
        "QPushButton {"
        "  background-color: #3a3a4a;"
        "  color: white;"
        "  border: none;"
        "  border-radius: %1px;"
        "  font-size: %2px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #4a4a5a; }"
        "QPushButton:pressed { background-color: #2a2a3a; }"
    ).arg(borderRadius).arg(btnFontSize);
    
    QString clearStyle = QString(
        "QPushButton {"
        "  background-color: #c44;"
        "  color: white;"
        "  border: none;"
        "  border-radius: %1px;"
        "  font-size: %2px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover { background-color: #d55; }"
        "QPushButton:pressed { background-color: #b33; }"
    ).arg(borderRadius).arg(btnFontSize * 3 / 4);
    
    QString backStyle = QString(
        "QPushButton {"
        "  background-color: #666;"
        "  color: white;"
        "  border: none;"
        "  border-radius: %1px;"
        "  font-size: %2px;"
        "}"
        "QPushButton:hover { background-color: #777; }"
        "QPushButton:pressed { background-color: #555; }"
    ).arg(borderRadius).arg(btnFontSize);
    
    for (auto* btn : m_allButtons) {
        if (btn == m_clearBtn) {
            btn->setStyleSheet(clearStyle);
        } else if (btn == m_backBtn) {
            btn->setStyleSheet(backStyle);
        } else {
            btn->setStyleSheet(digitStyle);
        }
    }
}

void LoginZone::onDigitPressed(int digit) {
    if (m_enteredPin.length() < MAX_PIN_LENGTH) {
        m_enteredPin += QString::number(digit);
        updatePinDisplay();
        clearError();
        emit pinChanged();
    }
}

void LoginZone::onClearPressed() {
    clearPin();
}

void LoginZone::onBackspacePressed() {
    if (!m_enteredPin.isEmpty()) {
        m_enteredPin.chop(1);
        updatePinDisplay();
        emit pinChanged();
    }
}

void LoginZone::updatePinDisplay() {
    m_pinDisplay->setText(m_enteredPin);
}

void LoginZone::clearPin() {
    m_enteredPin.clear();
    updatePinDisplay();
    clearError();
    emit pinChanged();
}

void LoginZone::setErrorMessage(const QString& message) {
    m_errorLabel->setText(message);
    m_errorLabel->show();
}

void LoginZone::clearError() {
    m_errorLabel->hide();
}

void LoginZone::drawContent(QPainter& painter) {
    // Content is handled by Qt widgets, no custom painting needed
    Q_UNUSED(painter);
}

} // namespace vt2
