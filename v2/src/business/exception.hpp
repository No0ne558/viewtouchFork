// ViewTouch V2 - Exception/Audit System
// Handles voids, comps, rebuilds with complete audit trail

#pragma once

#include <QObject>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QList>

namespace vt2 {

//=============================================================================
// Exception Types
//=============================================================================

enum class ExceptionType {
    None = 0,
    
    // Check-level exceptions
    VoidCheck,          // Void entire check
    RebuildCheck,       // Rebuild/reopen closed check
    ReOpenCheck,        // Reopen check for editing
    CancelCheck,        // Cancel check before payment
    
    // Item-level exceptions
    VoidItem,           // Void single item
    CompItem,           // Comp (complimentary) item
    DiscountItem,       // Discount item (amount or percent)
    ReturnItem,         // Return/refund item
    ChangePrice,        // Price override
    ChangeQuantity,     // Quantity override
    
    // Payment-level exceptions
    VoidPayment,        // Void payment
    RefundPayment,      // Refund payment
    AdjustTip,          // Adjust tip amount
    CashBack,           // Cash back on card
    
    // Cash drawer exceptions
    PaidOut,            // Cash paid out
    PaidIn,             // Cash paid in (deposit)
    NoSale,             // No-sale drawer open
    DrawerCount,        // Manual drawer count
    
    // Labor exceptions
    ClockAdjust,        // Clock in/out time adjustment
    BreakAdjust,        // Break time adjustment
    PayRateOverride,    // Pay rate override
    
    // System exceptions
    OverrideTotal,      // Manual total override
    TaxExempt,          // Tax exemption applied
    GratuityOverride,   // Auto-gratuity override
    PriceOverride       // Price level override
};

enum class ExceptionReason {
    None = 0,
    CustomerRequest,    // Customer requested
    ManagerDecision,    // Manager decision
    ItemQuality,        // Food quality issue
    ServiceIssue,       // Service problem
    OrderError,         // Order was entered wrong
    SystemError,        // System error/crash
    EmployeeError,      // Employee mistake
    PolicyException,    // Policy exception
    Promotion,          // Promotional comp
    VIP,                // VIP treatment
    Training,           // Training purposes
    Other               // Other (requires comment)
};

enum class ExceptionStatus {
    Pending,            // Waiting for approval
    Approved,           // Approved by manager
    Denied,             // Denied
    Applied,            // Applied to transaction
    Reversed            // Exception was reversed
};

//=============================================================================
// ExceptionRecord - Single exception/audit entry
//=============================================================================

class ExceptionRecord : public QObject {
    Q_OBJECT

    Q_PROPERTY(int id READ id WRITE setId NOTIFY changed)
    Q_PROPERTY(ExceptionType type READ type WRITE setType NOTIFY changed)
    Q_PROPERTY(ExceptionReason reason READ reason WRITE setReason NOTIFY changed)
    Q_PROPERTY(ExceptionStatus status READ status WRITE setStatus NOTIFY changed)

public:
    explicit ExceptionRecord(QObject* parent = nullptr);

    // Core identification
    int id() const { return m_id; }
    void setId(int id) { m_id = id; emit changed(); }

    ExceptionType type() const { return m_type; }
    void setType(ExceptionType t) { m_type = t; emit changed(); }

    ExceptionReason reason() const { return m_reason; }
    void setReason(ExceptionReason r) { m_reason = r; emit changed(); }

    ExceptionStatus status() const { return m_status; }
    void setStatus(ExceptionStatus s) { m_status = s; emit changed(); }

    // Transaction context
    int checkId() const { return m_checkId; }
    void setCheckId(int id) { m_checkId = id; }

    int subCheckId() const { return m_subCheckId; }
    void setSubCheckId(int id) { m_subCheckId = id; }

    int itemId() const { return m_itemId; }
    void setItemId(int id) { m_itemId = id; }

    int paymentId() const { return m_paymentId; }
    void setPaymentId(int id) { m_paymentId = id; }

    // Employee tracking
    int requestedBy() const { return m_requestedBy; }
    void setRequestedBy(int empId) { m_requestedBy = empId; }

    int approvedBy() const { return m_approvedBy; }
    void setApprovedBy(int empId) { m_approvedBy = empId; }

    // Timing
    QDateTime requestedAt() const { return m_requestedAt; }
    void setRequestedAt(const QDateTime& dt) { m_requestedAt = dt; }

    QDateTime approvedAt() const { return m_approvedAt; }
    void setApprovedAt(const QDateTime& dt) { m_approvedAt = dt; }

    QDateTime appliedAt() const { return m_appliedAt; }
    void setAppliedAt(const QDateTime& dt) { m_appliedAt = dt; }

    // Financial impact
    int originalAmount() const { return m_originalAmount; }
    void setOriginalAmount(int cents) { m_originalAmount = cents; }

    int adjustedAmount() const { return m_adjustedAmount; }
    void setAdjustedAmount(int cents) { m_adjustedAmount = cents; }

    int impactAmount() const { return m_originalAmount - m_adjustedAmount; }

    // Details
    QString description() const { return m_description; }
    void setDescription(const QString& desc) { m_description = desc; }

    QString comment() const { return m_comment; }
    void setComment(const QString& comment) { m_comment = comment; }

    // Security
    QString managerCode() const { return m_managerCode; }
    void setManagerCode(const QString& code) { m_managerCode = code; }

    bool requiresApproval() const { return m_requiresApproval; }
    void setRequiresApproval(bool req) { m_requiresApproval = req; }

    // Audit
    QString terminalId() const { return m_terminalId; }
    void setTerminalId(const QString& id) { m_terminalId = id; }

    QString ipAddress() const { return m_ipAddress; }
    void setIpAddress(const QString& ip) { m_ipAddress = ip; }

    // JSON serialization
    QJsonObject toJson() const;
    static ExceptionRecord* fromJson(const QJsonObject& json, QObject* parent = nullptr);

    // Type helpers
    static QString typeToString(ExceptionType type);
    static QString reasonToString(ExceptionReason reason);
    static QString statusToString(ExceptionStatus status);

signals:
    void changed();

private:
    int m_id = 0;
    ExceptionType m_type = ExceptionType::None;
    ExceptionReason m_reason = ExceptionReason::None;
    ExceptionStatus m_status = ExceptionStatus::Pending;

    int m_checkId = 0;
    int m_subCheckId = 0;
    int m_itemId = 0;
    int m_paymentId = 0;

    int m_requestedBy = 0;
    int m_approvedBy = 0;

    QDateTime m_requestedAt;
    QDateTime m_approvedAt;
    QDateTime m_appliedAt;

    int m_originalAmount = 0;
    int m_adjustedAmount = 0;

    QString m_description;
    QString m_comment;

    QString m_managerCode;
    bool m_requiresApproval = true;

    QString m_terminalId;
    QString m_ipAddress;
};

//=============================================================================
// ExceptionPolicy - Rules for when approval is needed
//=============================================================================

struct ExceptionPolicy {
    ExceptionType type = ExceptionType::None;
    bool requiresApproval = true;
    int maxAmountWithoutApproval = 0;  // In cents, 0 = always require
    int requiredSecurityLevel = 3;      // Manager level
    bool requiresComment = false;
    bool printReceipt = true;
    bool trackInReports = true;
    QString receiptMessage;

    QJsonObject toJson() const;
    static ExceptionPolicy fromJson(const QJsonObject& json);
};

//=============================================================================
// AuditLog - Comprehensive system audit
//=============================================================================

struct AuditEntry {
    int id = 0;
    QDateTime timestamp;
    QString action;             // What happened
    QString category;           // Category (security, transaction, system)
    int employeeId = 0;
    int checkId = 0;
    QString terminalId;
    QString details;            // JSON details
    QString ipAddress;
    QString beforeValue;        // State before change
    QString afterValue;         // State after change

    QJsonObject toJson() const;
    static AuditEntry fromJson(const QJsonObject& json);
};

//=============================================================================
// ExceptionManager - Central exception handling
//=============================================================================

class ExceptionManager : public QObject {
    Q_OBJECT

public:
    static ExceptionManager* instance();

    // Exception operations
    ExceptionRecord* createException(ExceptionType type, int checkId = 0);
    bool requestApproval(int exceptionId, int managerId, const QString& code);
    bool approveException(int exceptionId, int managerId, const QString& code);
    bool denyException(int exceptionId, int managerId, const QString& comment);
    bool applyException(int exceptionId);
    bool reverseException(int exceptionId, int managerId);

    // Quick operations (combines create + request + apply)
    ExceptionRecord* voidCheck(int checkId, ExceptionReason reason, 
                               int requestedBy, int managerId = 0);
    ExceptionRecord* voidItem(int checkId, int itemId, ExceptionReason reason,
                              int requestedBy, int managerId = 0);
    ExceptionRecord* compItem(int checkId, int itemId, int amount,
                              ExceptionReason reason, int requestedBy);
    ExceptionRecord* rebuildCheck(int checkId, int requestedBy, int managerId);

    // Drawer operations
    ExceptionRecord* paidOut(int amount, const QString& description, int employeeId);
    ExceptionRecord* paidIn(int amount, const QString& description, int employeeId);
    ExceptionRecord* noSale(int employeeId, const QString& reason);

    // Policy management
    void setPolicy(ExceptionType type, const ExceptionPolicy& policy);
    ExceptionPolicy policy(ExceptionType type) const;
    bool requiresApproval(ExceptionType type, int amount = 0) const;
    int requiredSecurityLevel(ExceptionType type) const;

    // Query exceptions
    QList<ExceptionRecord*> exceptionsForCheck(int checkId);
    QList<ExceptionRecord*> exceptionsForEmployee(int employeeId);
    QList<ExceptionRecord*> pendingExceptions();
    QList<ExceptionRecord*> exceptionsByType(ExceptionType type);
    QList<ExceptionRecord*> exceptionsInDateRange(const QDate& from, const QDate& to);

    // Audit logging
    void logAudit(const QString& action, const QString& category,
                  int employeeId = 0, int checkId = 0,
                  const QString& details = QString());
    QList<AuditEntry> auditLog(const QDate& from, const QDate& to);
    QList<AuditEntry> auditLogForEmployee(int employeeId);
    QList<AuditEntry> auditLogForCheck(int checkId);

    // Reporting
    QJsonObject dailyExceptionSummary(const QDate& date);
    QJsonObject employeeExceptionSummary(int employeeId, 
                                          const QDate& from, const QDate& to);

    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

signals:
    void exceptionCreated(ExceptionRecord* record);
    void exceptionApproved(int id);
    void exceptionDenied(int id);
    void exceptionApplied(int id);
    void exceptionReversed(int id);
    void approvalRequired(ExceptionRecord* record);
    void auditLogged(const AuditEntry& entry);

private:
    explicit ExceptionManager(QObject* parent = nullptr);
    static ExceptionManager* s_instance;

    int m_nextExceptionId = 1;
    int m_nextAuditId = 1;
    QList<ExceptionRecord*> m_exceptions;
    QList<AuditEntry> m_auditLog;
    QMap<ExceptionType, ExceptionPolicy> m_policies;

    void initializeDefaultPolicies();
    bool validateManagerCode(const QString& code);
};

//=============================================================================
// VoidZone - Zone for voiding items/checks
//=============================================================================

class VoidZone : public QObject {
    Q_OBJECT

    Q_PROPERTY(int checkId READ checkId WRITE setCheckId NOTIFY checkChanged)
    Q_PROPERTY(int selectedItem READ selectedItem WRITE setSelectedItem NOTIFY selectionChanged)

public:
    explicit VoidZone(QObject* parent = nullptr);

    int checkId() const { return m_checkId; }
    void setCheckId(int id) { m_checkId = id; emit checkChanged(); }

    int selectedItem() const { return m_selectedItem; }
    void setSelectedItem(int id) { m_selectedItem = id; emit selectionChanged(); }

    // Operations
    Q_INVOKABLE void voidSelectedItem();
    Q_INVOKABLE void voidEntireCheck();
    Q_INVOKABLE void cancelOperation();

    // Reason selection
    Q_INVOKABLE void selectReason(ExceptionReason reason);
    Q_INVOKABLE void setComment(const QString& comment);
    Q_INVOKABLE void submitVoid();

signals:
    void checkChanged();
    void selectionChanged();
    void voidComplete(int exceptionId);
    void voidCancelled();
    void approvalRequired(int exceptionId);

private:
    int m_checkId = 0;
    int m_selectedItem = 0;
    ExceptionReason m_selectedReason = ExceptionReason::None;
    QString m_comment;
    bool m_voidEntireCheck = false;
};

//=============================================================================
// CompZone - Zone for comping items
//=============================================================================

class CompZone : public QObject {
    Q_OBJECT

public:
    explicit CompZone(QObject* parent = nullptr);

    int checkId() const { return m_checkId; }
    void setCheckId(int id) { m_checkId = id; }

    int selectedItem() const { return m_selectedItem; }
    void setSelectedItem(int id) { m_selectedItem = id; }

    // Comp modes
    Q_INVOKABLE void compFullItem();
    Q_INVOKABLE void compPartialAmount(int cents);
    Q_INVOKABLE void compPercentage(int percent);
    Q_INVOKABLE void selectReason(ExceptionReason reason);
    Q_INVOKABLE void setComment(const QString& comment);
    Q_INVOKABLE void submitComp();

signals:
    void compComplete(int exceptionId);
    void compCancelled();
    void approvalRequired(int exceptionId);

private:
    int m_checkId = 0;
    int m_selectedItem = 0;
    int m_compAmount = 0;
    ExceptionReason m_reason = ExceptionReason::None;
    QString m_comment;
};

} // namespace vt2
