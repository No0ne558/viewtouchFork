// ViewTouch V2 - Payment Zone
// Complete payment entry with drawer integration

#pragma once

#include "zone/zone.hpp"
#include <QString>
#include <QList>
#include <QMap>

namespace vt {

//=============================================================================
// Tender Types (from original ViewTouch)
//=============================================================================

enum class TenderType {
    None = 0,
    Cash,
    Check,
    Charge,         // House account
    GiftCertificate,
    Coupon,
    Discount,
    CreditCard,
    DebitCard,
    Comp,           // Complimentary
    Employee,       // Employee meal
    Gratuity,
    RoomCharge,
    Tab,
    Expense,
    Split
};

//=============================================================================
// PaymentEntry - Single payment record
//=============================================================================

struct PaymentEntry {
    int id = 0;
    TenderType type = TenderType::None;
    int amount = 0;         // cents
    int tipAmount = 0;      // cents
    QString reference;      // Check #, CC last 4, etc.
    QString authCode;       // For CC
    bool approved = false;
    
    int total() const { return amount + tipAmount; }
};

//=============================================================================
// PaymentZone - Main Payment Display
//=============================================================================

class PaymentZone : public Zone {
    Q_OBJECT

public:
    PaymentZone();
    ~PaymentZone() override = default;

    // Zone type identification
    const char* typeName() const override { return "PaymentZone"; }

    // Rendering
    void renderContent(Renderer& renderer, Terminal* term) override;

    // Touch handling
    int touch(Terminal* term, int tx, int ty) override;

    // Amounts
    int checkTotal() const { return m_checkTotal; }
    void setCheckTotal(int total) { m_checkTotal = total; }

    int amountPaid() const;
    int balanceDue() const { return m_checkTotal - amountPaid(); }
    int changeDue() const;

    // Payments
    void addPayment(const PaymentEntry& payment);
    void removePayment(int index);
    void clearPayments();
    const QList<PaymentEntry>& payments() const { return m_payments; }

    // Input
    void appendDigit(int digit);
    void clearInput();
    void backspace();
    int inputAmount() const;
    QString inputDisplay() const;

    // Quick amounts
    void setInputToBalance() { m_inputBuffer = QString::number(balanceDue()); }
    void addDollarAmount(int dollars);

signals:
    void paymentAdded(TenderType type, int amount);
    void paymentRemoved(int index);
    void paymentComplete();
    void changeCalculated(int change);
    void inputChanged(int amount);

private:
    int m_checkTotal = 0;
    QList<PaymentEntry> m_payments;
    QString m_inputBuffer;
};

//=============================================================================
// TenderZone - Payment Type Button
//=============================================================================

class TenderZone : public Zone {
    Q_OBJECT

public:
    TenderZone();
    ~TenderZone() override = default;

    const char* typeName() const override { return "TenderZone"; }

    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;

    // Tender type
    TenderType tenderType() const { return m_tenderType; }
    void setTenderType(TenderType type);

    QString tenderName() const;

    // Fixed amount (0 = use input)
    int fixedAmount() const { return m_fixedAmount; }
    void setFixedAmount(int amount) { m_fixedAmount = amount; }

signals:
    void tenderSelected(TenderType type, int amount);

private:
    TenderType m_tenderType = TenderType::Cash;
    int m_fixedAmount = 0;
};

//=============================================================================
// DrawerZone - Cash Drawer Management
//=============================================================================

class DrawerZone : public Zone {
    Q_OBJECT

public:
    enum class DrawerAction {
        Open,       // Open drawer
        Balance,    // Balance drawer
        Pull,       // Pull drawer count
        Assign,     // Assign to employee
        Unassign    // Unassign drawer
    };

    DrawerZone();
    ~DrawerZone() override = default;

    const char* typeName() const override { return "DrawerZone"; }

    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;

    DrawerAction drawerAction() const { return m_action; }
    void setDrawerAction(DrawerAction action) { m_action = action; }

    QString actionLabel() const;

signals:
    void drawerActionRequested(DrawerAction action);
    void drawerOpened();
    void drawerBalanced(int amount);

private:
    DrawerAction m_action = DrawerAction::Open;
};

//=============================================================================
// SplitCheckZone - Check Splitting
//=============================================================================

class SplitCheckZone : public Zone {
    Q_OBJECT

public:
    enum class SplitMode {
        BySeat,     // Split by seat
        ByItem,     // Split selected items
        Even,       // Split evenly
        Custom      // Custom amounts
    };

    SplitCheckZone();
    ~SplitCheckZone() override = default;

    const char* typeName() const override { return "SplitCheckZone"; }

    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;

    SplitMode splitMode() const { return m_splitMode; }
    void setSplitMode(SplitMode mode) { m_splitMode = mode; }

    int splitCount() const { return m_splitCount; }
    void setSplitCount(int count) { m_splitCount = count; }

    QString modeLabel() const;

signals:
    void splitRequested(SplitMode mode, int count);

private:
    SplitMode m_splitMode = SplitMode::Even;
    int m_splitCount = 2;
};

//=============================================================================
// EndDayZone - End of Day Processing
//=============================================================================

class EndDayZone : public Zone {
    Q_OBJECT

public:
    EndDayZone();
    ~EndDayZone() override = default;

    const char* typeName() const override { return "EndDayZone"; }

    void renderContent(Renderer& renderer, Terminal* term) override;
    int touch(Terminal* term, int tx, int ty) override;

    // Pre-checks
    bool hasOpenChecks() const { return m_openCheckCount > 0; }
    void setOpenCheckCount(int count) { m_openCheckCount = count; }

    bool hasOpenDrawers() const { return m_openDrawerCount > 0; }
    void setOpenDrawerCount(int count) { m_openDrawerCount = count; }

    bool hasClockedIn() const { return m_clockedInCount > 0; }
    void setClockedInCount(int count) { m_clockedInCount = count; }

signals:
    void endDayRequested();
    void endDayConfirmed();
    void endDayCancelled();
    void preCheckFailed(const QString& reason);

private:
    int m_openCheckCount = 0;
    int m_openDrawerCount = 0;
    int m_clockedInCount = 0;
    bool m_confirmed = false;
};

} // namespace vt
