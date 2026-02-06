// ViewTouch V2 - Payment Zone Implementation

#include "zone/payment_zone.hpp"
#include "render/renderer.hpp"
#include "terminal/terminal.hpp"

namespace vt {

//=============================================================================
// PaymentZone Implementation
//=============================================================================

PaymentZone::PaymentZone()
    : Zone()
{
    setZoneType(ZoneType::PaymentEntry);
    setName("Payment");
}

int PaymentZone::amountPaid() const {
    int total = 0;
    for (const auto& p : m_payments) {
        if (p.approved) {
            total += p.total();
        }
    }
    return total;
}

int PaymentZone::changeDue() const {
    int balance = balanceDue();
    return (balance < 0) ? -balance : 0;
}

void PaymentZone::addPayment(const PaymentEntry& payment) {
    m_payments.append(payment);
    setNeedsUpdate(true);
    emit paymentAdded(payment.type, payment.total());
    
    if (balanceDue() <= 0) {
        emit paymentComplete();
        if (changeDue() > 0) {
            emit changeCalculated(changeDue());
        }
    }
}

void PaymentZone::removePayment(int index) {
    if (index >= 0 && index < m_payments.size()) {
        m_payments.removeAt(index);
        setNeedsUpdate(true);
        emit paymentRemoved(index);
    }
}

void PaymentZone::clearPayments() {
    m_payments.clear();
    setNeedsUpdate(true);
}

void PaymentZone::appendDigit(int digit) {
    if (digit >= 0 && digit <= 9) {
        m_inputBuffer += QString::number(digit);
        setNeedsUpdate(true);
        emit inputChanged(inputAmount());
    }
}

void PaymentZone::clearInput() {
    m_inputBuffer.clear();
    setNeedsUpdate(true);
    emit inputChanged(0);
}

void PaymentZone::backspace() {
    if (!m_inputBuffer.isEmpty()) {
        m_inputBuffer.chop(1);
        setNeedsUpdate(true);
        emit inputChanged(inputAmount());
    }
}

int PaymentZone::inputAmount() const {
    return m_inputBuffer.toInt();
}

QString PaymentZone::inputDisplay() const {
    int cents = inputAmount();
    return QString("$%1.%2")
        .arg(cents / 100)
        .arg(cents % 100, 2, 10, QChar('0'));
}

void PaymentZone::addDollarAmount(int dollars) {
    int current = inputAmount();
    m_inputBuffer = QString::number(current + dollars * 100);
    setNeedsUpdate(true);
    emit inputChanged(inputAmount());
}

void PaymentZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    int yPos = y() + 15;
    int lineHeight = 18;
    
    // Check total
    QString totalStr = QString("Total: $%1.%2")
        .arg(m_checkTotal / 100)
        .arg(m_checkTotal % 100, 2, 10, QChar('0'));
    renderer.drawText(totalStr, x() + 10, yPos, static_cast<uint8_t>(font()), 0);
    yPos += lineHeight;
    
    // Payments made
    for (const auto& p : m_payments) {
        QString typeStr;
        switch (p.type) {
            case TenderType::Cash: typeStr = "Cash"; break;
            case TenderType::CreditCard: typeStr = "Credit"; break;
            case TenderType::DebitCard: typeStr = "Debit"; break;
            case TenderType::Check: typeStr = "Check"; break;
            case TenderType::GiftCertificate: typeStr = "Gift"; break;
            case TenderType::Comp: typeStr = "Comp"; break;
            default: typeStr = "Other"; break;
        }
        
        QString payStr = QString("%1: $%2.%3")
            .arg(typeStr)
            .arg(p.total() / 100)
            .arg(p.total() % 100, 2, 10, QChar('0'));
        renderer.drawText(payStr, x() + 20, yPos, static_cast<uint8_t>(font()), 0);
        yPos += lineHeight;
    }
    
    // Balance due
    int balance = balanceDue();
    QString balanceStr = QString("Balance: $%1.%2")
        .arg(balance / 100)
        .arg(qAbs(balance % 100), 2, 10, QChar('0'));
    renderer.drawText(balanceStr, x() + 10, y() + h() - 40, static_cast<uint8_t>(font()), 0);
    
    // Change due
    if (changeDue() > 0) {
        QString changeStr = QString("Change: $%1.%2")
            .arg(changeDue() / 100)
            .arg(changeDue() % 100, 2, 10, QChar('0'));
        renderer.drawText(changeStr, x() + 10, y() + h() - 20, static_cast<uint8_t>(font()), 0);
    }
    
    // Input display
    if (!m_inputBuffer.isEmpty()) {
        renderer.drawText(inputDisplay(), x() + w() - 80, y() + h() - 20, static_cast<uint8_t>(font()), 0);
    }
}

int PaymentZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    return 0;
}

//=============================================================================
// TenderZone Implementation
//=============================================================================

TenderZone::TenderZone()
    : Zone()
{
    setZoneType(ZoneType::Tender);
    setName("Tender");
}

void TenderZone::setTenderType(TenderType type) {
    m_tenderType = type;
    setNeedsUpdate(true);
}

QString TenderZone::tenderName() const {
    switch (m_tenderType) {
        case TenderType::Cash: return "Cash";
        case TenderType::CreditCard: return "Credit Card";
        case TenderType::DebitCard: return "Debit Card";
        case TenderType::Check: return "Check";
        case TenderType::GiftCertificate: return "Gift Certificate";
        case TenderType::Coupon: return "Coupon";
        case TenderType::Discount: return "Discount";
        case TenderType::Charge: return "House Account";
        case TenderType::Comp: return "Comp";
        case TenderType::Employee: return "Employee Meal";
        case TenderType::Gratuity: return "Gratuity";
        case TenderType::RoomCharge: return "Room Charge";
        case TenderType::Tab: return "Tab";
        case TenderType::Expense: return "Expense";
        case TenderType::Split: return "Split";
        default: return "Unknown";
    }
}

void TenderZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    int cx = x() + w() / 2;
    int textColor = static_cast<int>(effectiveColor());
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_20);
    
    // Draw tender name centered
    renderer.drawTextCentered(tenderName(), cx, y() + h() / 3, fontId, textColor);
    
    // Draw fixed amount if set
    if (m_fixedAmount > 0) {
        QString amtStr = QString("$%1.%2")
            .arg(m_fixedAmount / 100)
            .arg(m_fixedAmount % 100, 2, 10, QChar('0'));
        uint8_t priceFontId = static_cast<uint8_t>(FontId::Times14);
        renderer.drawTextCentered(amtStr, cx, y() + h() * 2 / 3, priceFontId, textColor);
    }
}

int TenderZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    emit tenderSelected(m_tenderType, m_fixedAmount);
    return 0;
}

//=============================================================================
// DrawerZone Implementation
//=============================================================================

DrawerZone::DrawerZone()
    : Zone()
{
    setZoneType(ZoneType::DrawerManage);
    setName("Drawer");
}

QString DrawerZone::actionLabel() const {
    switch (m_action) {
        case DrawerAction::Open: return "Open Drawer";
        case DrawerAction::Balance: return "Balance Drawer";
        case DrawerAction::Pull: return "Pull Drawer";
        case DrawerAction::Assign: return "Assign Drawer";
        case DrawerAction::Unassign: return "Unassign Drawer";
        default: return "Drawer";
    }
}

void DrawerZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    int cx = x() + w() / 2;
    int textColor = static_cast<int>(effectiveColor());
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_20);
    
    // Draw action label centered
    renderer.drawTextCentered(actionLabel(), cx, y() + h() / 2, fontId, textColor);
}

int DrawerZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    emit drawerActionRequested(m_action);
    
    if (m_action == DrawerAction::Open) {
        emit drawerOpened();
    }
    
    return 0;
}

//=============================================================================
// SplitCheckZone Implementation
//=============================================================================

SplitCheckZone::SplitCheckZone()
    : Zone()
{
    setZoneType(ZoneType::SplitCheck);
    setName("Split Check");
}

QString SplitCheckZone::modeLabel() const {
    switch (m_splitMode) {
        case SplitMode::BySeat: return "Split by Seat";
        case SplitMode::ByItem: return "Split by Item";
        case SplitMode::Even: return QString("Split %1 Ways").arg(m_splitCount);
        case SplitMode::Custom: return "Custom Split";
        default: return "Split";
    }
}

void SplitCheckZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    int cx = x() + w() / 2;
    int textColor = static_cast<int>(effectiveColor());
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_20);
    
    // Draw split mode centered
    renderer.drawTextCentered(modeLabel(), cx, y() + h() / 2, fontId, textColor);
}

int SplitCheckZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    emit splitRequested(m_splitMode, m_splitCount);
    return 0;
}

//=============================================================================
// EndDayZone Implementation
//=============================================================================

EndDayZone::EndDayZone()
    : Zone()
{
    setZoneType(ZoneType::EndDay);
    setName("End Day");
}

void EndDayZone::renderContent(Renderer& renderer, Terminal* term) {
    Q_UNUSED(term);
    
    int cx = x() + w() / 2;
    int textColor = static_cast<int>(effectiveColor());
    uint8_t fontId = static_cast<uint8_t>(font());
    if (fontId == 0) fontId = static_cast<uint8_t>(FontId::Times_20);
    uint8_t smallFontId = static_cast<uint8_t>(FontId::Times14);
    
    if (m_confirmed) {
        renderer.drawTextCentered("Confirm End Day?", cx, y() + h() / 2, fontId, textColor);
    } else {
        renderer.drawTextCentered("End Day", cx, y() + h() / 4, fontId, textColor);
        
        // Show pre-check status centered
        int yPos = y() + h() / 2;
        int lineHeight = 18;
        
        if (m_openCheckCount > 0) {
            QString str = QString("Open Checks: %1").arg(m_openCheckCount);
            renderer.drawTextCentered(str, cx, yPos, smallFontId, textColor);
            yPos += lineHeight;
        }
        
        if (m_openDrawerCount > 0) {
            QString str = QString("Open Drawers: %1").arg(m_openDrawerCount);
            renderer.drawTextCentered(str, cx, yPos, smallFontId, textColor);
            yPos += lineHeight;
        }
        
        if (m_clockedInCount > 0) {
            QString str = QString("Clocked In: %1").arg(m_clockedInCount);
            renderer.drawTextCentered(str, cx, yPos, smallFontId, textColor);
        }
    }
}

int EndDayZone::touch(Terminal* term, int tx, int ty) {
    Q_UNUSED(term);
    Q_UNUSED(tx);
    Q_UNUSED(ty);
    
    // Check for blockers
    if (m_openCheckCount > 0) {
        emit preCheckFailed("There are open checks");
        return 0;
    }
    
    if (m_openDrawerCount > 0) {
        emit preCheckFailed("There are open drawers");
        return 0;
    }
    
    if (!m_confirmed) {
        m_confirmed = true;
        setNeedsUpdate(true);
        emit endDayRequested();
    } else {
        m_confirmed = false;
        setNeedsUpdate(true);
        emit endDayConfirmed();
    }
    
    return 0;
}

} // namespace vt
