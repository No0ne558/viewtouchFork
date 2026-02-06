// ViewTouch V2 - Cash Drawer System
// Modern C++/Qt6 reimplementation

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QJsonObject>

namespace vt2 {

//=============================================================================
// Drawer Event Types
//=============================================================================
enum class DrawerEventType {
    Open,           // Drawer opened (by system)
    Close,          // Drawer closed
    Drop,           // Cash removed to safe
    Loan,           // Cash added from safe
    Adjustment,     // Count adjustment
    CheckOut,       // End of shift checkout
    StartingCash,   // Initial drawer amount
    Override        // Forced open
};

enum class DrawerStatus {
    Closed,
    Open,
    Locked,
    Unknown
};

//=============================================================================
// Denomination Count - For counting cash
//=============================================================================
struct DenominationCount {
    // Bills
    int hundreds = 0;     // $100
    int fifties = 0;      // $50
    int twenties = 0;     // $20
    int tens = 0;         // $10
    int fives = 0;        // $5
    int twos = 0;         // $2
    int ones = 0;         // $1
    
    // Coins
    int dollarCoins = 0;  // $1
    int halfDollars = 0;  // $0.50
    int quarters = 0;     // $0.25
    int dimes = 0;        // $0.10
    int nickels = 0;      // $0.05
    int pennies = 0;      // $0.01
    
    // Calculate total in cents
    int totalCents() const {
        return (hundreds * 10000) + (fifties * 5000) + (twenties * 2000) +
               (tens * 1000) + (fives * 500) + (twos * 200) + (ones * 100) +
               (dollarCoins * 100) + (halfDollars * 50) + (quarters * 25) +
               (dimes * 10) + (nickels * 5) + pennies;
    }
    
    QJsonObject toJson() const;
    static DenominationCount fromJson(const QJsonObject& json);
};

//=============================================================================
// Drawer Event - Records drawer activity
//=============================================================================
class DrawerEvent : public QObject {
    Q_OBJECT
    
public:
    explicit DrawerEvent(QObject* parent = nullptr);
    
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    int drawerId() const { return m_drawerId; }
    void setDrawerId(int id) { m_drawerId = id; }
    
    DrawerEventType eventType() const { return m_eventType; }
    void setEventType(DrawerEventType t) { m_eventType = t; }
    
    QDateTime timestamp() const { return m_timestamp; }
    void setTimestamp(const QDateTime& dt) { m_timestamp = dt; }
    
    int employeeId() const { return m_employeeId; }
    void setEmployeeId(int id) { m_employeeId = id; }
    
    int amount() const { return m_amount; }  // In cents
    void setAmount(int cents) { m_amount = cents; }
    
    QString reason() const { return m_reason; }
    void setReason(const QString& r) { m_reason = r; }
    
    DenominationCount denominationCount() const { return m_denominationCount; }
    void setDenominationCount(const DenominationCount& dc) { m_denominationCount = dc; }
    
    int checkId() const { return m_checkId; }
    void setCheckId(int id) { m_checkId = id; }
    
    bool requiresApproval() const { return m_requiresApproval; }
    void setRequiresApproval(bool r) { m_requiresApproval = r; }
    
    int approvedBy() const { return m_approvedBy; }
    void setApprovedBy(int id) { m_approvedBy = id; }
    
    QJsonObject toJson() const;
    static DrawerEvent* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    int m_drawerId = 0;
    DrawerEventType m_eventType = DrawerEventType::Open;
    QDateTime m_timestamp;
    int m_employeeId = 0;
    int m_amount = 0;
    QString m_reason;
    DenominationCount m_denominationCount;
    int m_checkId = 0;
    bool m_requiresApproval = false;
    int m_approvedBy = 0;
};

//=============================================================================
// Drawer Session - A shift/assignment period
//=============================================================================
class DrawerSession : public QObject {
    Q_OBJECT
    
public:
    enum class SessionStatus {
        Active,
        Closed,
        Balanced,
        OverShort
    };
    
    explicit DrawerSession(QObject* parent = nullptr);
    
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    int drawerId() const { return m_drawerId; }
    void setDrawerId(int id) { m_drawerId = id; }
    
    int employeeId() const { return m_employeeId; }
    void setEmployeeId(int id) { m_employeeId = id; }
    
    QDateTime startTime() const { return m_startTime; }
    void setStartTime(const QDateTime& dt) { m_startTime = dt; }
    
    QDateTime endTime() const { return m_endTime; }
    void setEndTime(const QDateTime& dt) { m_endTime = dt; }
    
    SessionStatus status() const { return m_status; }
    void setStatus(SessionStatus s) { m_status = s; }
    
    // Starting amounts
    int startingCash() const { return m_startingCash; }
    void setStartingCash(int cents) { m_startingCash = cents; }
    
    DenominationCount startingCount() const { return m_startingCount; }
    void setStartingCount(const DenominationCount& dc) { m_startingCount = dc; }
    
    // Expected totals (calculated from transactions)
    int expectedCash() const { return m_expectedCash; }
    void setExpectedCash(int cents) { m_expectedCash = cents; }
    
    int expectedTotal() const { return m_expectedTotal; }
    void setExpectedTotal(int cents) { m_expectedTotal = cents; }
    
    // Actual counted amounts
    int actualCash() const { return m_actualCash; }
    void setActualCash(int cents) { m_actualCash = cents; }
    
    DenominationCount endingCount() const { return m_endingCount; }
    void setEndingCount(const DenominationCount& dc) { m_endingCount = dc; }
    
    // Over/Short
    int overShort() const { return m_actualCash - m_expectedCash; }
    
    // Transaction totals
    int cashSales() const { return m_cashSales; }
    void setCashSales(int cents) { m_cashSales = cents; }
    void addCashSale(int cents) { m_cashSales += cents; }
    
    int cashRefunds() const { return m_cashRefunds; }
    void setCashRefunds(int cents) { m_cashRefunds = cents; }
    void addCashRefund(int cents) { m_cashRefunds += cents; }
    
    int paidOuts() const { return m_paidOuts; }
    void setPaidOuts(int cents) { m_paidOuts = cents; }
    void addPaidOut(int cents) { m_paidOuts += cents; }
    
    int paidIns() const { return m_paidIns; }
    void setPaidIns(int cents) { m_paidIns = cents; }
    void addPaidIn(int cents) { m_paidIns += cents; }
    
    int drops() const { return m_drops; }
    void setDrops(int cents) { m_drops = cents; }
    void addDrop(int cents) { m_drops += cents; }
    
    int loans() const { return m_loans; }
    void setLoans(int cents) { m_loans = cents; }
    void addLoan(int cents) { m_loans += cents; }
    
    // Calculate expected cash
    void calculateExpected();
    
    QJsonObject toJson() const;
    static DrawerSession* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    int m_drawerId = 0;
    int m_employeeId = 0;
    QDateTime m_startTime;
    QDateTime m_endTime;
    SessionStatus m_status = SessionStatus::Active;
    
    int m_startingCash = 0;
    DenominationCount m_startingCount;
    int m_expectedCash = 0;
    int m_expectedTotal = 0;
    int m_actualCash = 0;
    DenominationCount m_endingCount;
    
    int m_cashSales = 0;
    int m_cashRefunds = 0;
    int m_paidOuts = 0;
    int m_paidIns = 0;
    int m_drops = 0;
    int m_loans = 0;
};

//=============================================================================
// Cash Drawer Configuration
//=============================================================================
class DrawerConfig : public QObject {
    Q_OBJECT
    
public:
    explicit DrawerConfig(QObject* parent = nullptr);
    
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    QString name() const { return m_name; }
    void setName(const QString& n) { m_name = n; }
    
    int terminalId() const { return m_terminalId; }
    void setTerminalId(int id) { m_terminalId = id; }
    
    int printerId() const { return m_printerId; }
    void setPrinterId(int id) { m_printerId = id; }
    
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool e) { m_enabled = e; }
    
    // Starting cash settings
    int defaultStartingCash() const { return m_defaultStartingCash; }
    void setDefaultStartingCash(int cents) { m_defaultStartingCash = cents; }
    
    bool requireStartingCount() const { return m_requireStartingCount; }
    void setRequireStartingCount(bool r) { m_requireStartingCount = r; }
    
    // Security settings
    bool requireEmployeeId() const { return m_requireEmployeeId; }
    void setRequireEmployeeId(bool r) { m_requireEmployeeId = r; }
    
    bool blindDrops() const { return m_blindDrops; }
    void setBlindDrops(bool b) { m_blindDrops = b; }
    
    bool blindClose() const { return m_blindClose; }
    void setBlindClose(bool b) { m_blindClose = b; }
    
    int overShortThreshold() const { return m_overShortThreshold; }
    void setOverShortThreshold(int cents) { m_overShortThreshold = cents; }
    
    QJsonObject toJson() const;
    static DrawerConfig* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    QString m_name;
    int m_terminalId = 0;
    int m_printerId = 0;
    bool m_enabled = true;
    
    int m_defaultStartingCash = 20000;  // $200.00
    bool m_requireStartingCount = false;
    
    bool m_requireEmployeeId = true;
    bool m_blindDrops = false;
    bool m_blindClose = false;
    int m_overShortThreshold = 500;  // $5.00
};

//=============================================================================
// Cash Drawer Manager - Singleton
//=============================================================================
class CashDrawerManager : public QObject {
    Q_OBJECT
    
public:
    static CashDrawerManager* instance();
    
    // Drawer configuration
    void addDrawer(DrawerConfig* drawer);
    void removeDrawer(int drawerId);
    DrawerConfig* findDrawer(int id);
    DrawerConfig* drawerForTerminal(int terminalId);
    QList<DrawerConfig*> allDrawers() const { return m_drawers; }
    
    // Drawer control
    bool openDrawer(int drawerId, int employeeId, int checkId = 0);
    bool kickDrawer(int drawerId);  // Hardware kick
    DrawerStatus getStatus(int drawerId);
    
    // Session management
    DrawerSession* startSession(int drawerId, int employeeId, int startingCash);
    DrawerSession* currentSession(int drawerId);
    bool endSession(int drawerId, const DenominationCount& endingCount);
    QList<DrawerSession*> sessionsForDrawer(int drawerId);
    QList<DrawerSession*> sessionsForDate(const QDate& date);
    
    // Cash operations
    bool recordDrop(int drawerId, int employeeId, int amount, const QString& reason = "");
    bool recordLoan(int drawerId, int employeeId, int amount, const QString& reason = "");
    bool recordPaidOut(int drawerId, int employeeId, int amount, const QString& reason);
    bool recordPaidIn(int drawerId, int employeeId, int amount, const QString& reason);
    bool recordAdjustment(int drawerId, int employeeId, int amount, const QString& reason);
    
    // Event history
    QList<DrawerEvent*> eventsForDrawer(int drawerId);
    QList<DrawerEvent*> eventsForSession(int sessionId);
    QList<DrawerEvent*> eventsForDate(const QDate& date);
    
    // Reporting
    int totalCashInDrawers();
    int totalDropsForDate(const QDate& date);
    QMap<int, int> overShortByEmployee(const QDate& start, const QDate& end);
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    
signals:
    void drawerOpened(int drawerId, int employeeId);
    void drawerClosed(int drawerId);
    void sessionStarted(DrawerSession* session);
    void sessionEnded(DrawerSession* session);
    void cashDropped(int drawerId, int amount);
    void overShortAlert(DrawerSession* session, int variance);
    
private:
    explicit CashDrawerManager(QObject* parent = nullptr);
    static CashDrawerManager* s_instance;
    
    DrawerEvent* createEvent(int drawerId, DrawerEventType type, int employeeId, int amount);
    
    QList<DrawerConfig*> m_drawers;
    QList<DrawerSession*> m_sessions;
    QList<DrawerEvent*> m_events;
    QMap<int, DrawerSession*> m_activeSessions;  // drawerId -> session
    QMap<int, DrawerStatus> m_drawerStatus;
    
    int m_nextDrawerId = 1;
    int m_nextSessionId = 1;
    int m_nextEventId = 1;
};

} // namespace vt2
