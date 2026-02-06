// ViewTouch V2 - Cash Drawer Implementation
// Modern C++/Qt6 reimplementation

#include "cashdrawer.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

namespace vt2 {

//=============================================================================
// DenominationCount Implementation
//=============================================================================

QJsonObject DenominationCount::toJson() const {
    QJsonObject json;
    json["hundreds"] = hundreds;
    json["fifties"] = fifties;
    json["twenties"] = twenties;
    json["tens"] = tens;
    json["fives"] = fives;
    json["twos"] = twos;
    json["ones"] = ones;
    json["dollarCoins"] = dollarCoins;
    json["halfDollars"] = halfDollars;
    json["quarters"] = quarters;
    json["dimes"] = dimes;
    json["nickels"] = nickels;
    json["pennies"] = pennies;
    return json;
}

DenominationCount DenominationCount::fromJson(const QJsonObject& json) {
    DenominationCount dc;
    dc.hundreds = json["hundreds"].toInt();
    dc.fifties = json["fifties"].toInt();
    dc.twenties = json["twenties"].toInt();
    dc.tens = json["tens"].toInt();
    dc.fives = json["fives"].toInt();
    dc.twos = json["twos"].toInt();
    dc.ones = json["ones"].toInt();
    dc.dollarCoins = json["dollarCoins"].toInt();
    dc.halfDollars = json["halfDollars"].toInt();
    dc.quarters = json["quarters"].toInt();
    dc.dimes = json["dimes"].toInt();
    dc.nickels = json["nickels"].toInt();
    dc.pennies = json["pennies"].toInt();
    return dc;
}

//=============================================================================
// DrawerEvent Implementation
//=============================================================================

DrawerEvent::DrawerEvent(QObject* parent)
    : QObject(parent)
    , m_timestamp(QDateTime::currentDateTime())
{
}

QJsonObject DrawerEvent::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["drawerId"] = m_drawerId;
    json["eventType"] = static_cast<int>(m_eventType);
    json["timestamp"] = m_timestamp.toString(Qt::ISODate);
    json["employeeId"] = m_employeeId;
    json["amount"] = m_amount;
    json["reason"] = m_reason;
    json["denominationCount"] = m_denominationCount.toJson();
    json["checkId"] = m_checkId;
    json["requiresApproval"] = m_requiresApproval;
    json["approvedBy"] = m_approvedBy;
    return json;
}

DrawerEvent* DrawerEvent::fromJson(const QJsonObject& json, QObject* parent) {
    auto* event = new DrawerEvent(parent);
    event->m_id = json["id"].toInt();
    event->m_drawerId = json["drawerId"].toInt();
    event->m_eventType = static_cast<DrawerEventType>(json["eventType"].toInt());
    event->m_timestamp = QDateTime::fromString(json["timestamp"].toString(), Qt::ISODate);
    event->m_employeeId = json["employeeId"].toInt();
    event->m_amount = json["amount"].toInt();
    event->m_reason = json["reason"].toString();
    event->m_denominationCount = DenominationCount::fromJson(json["denominationCount"].toObject());
    event->m_checkId = json["checkId"].toInt();
    event->m_requiresApproval = json["requiresApproval"].toBool();
    event->m_approvedBy = json["approvedBy"].toInt();
    return event;
}

//=============================================================================
// DrawerSession Implementation
//=============================================================================

DrawerSession::DrawerSession(QObject* parent)
    : QObject(parent)
    , m_startTime(QDateTime::currentDateTime())
{
}

void DrawerSession::calculateExpected() {
    m_expectedCash = m_startingCash + m_cashSales - m_cashRefunds 
                   - m_paidOuts + m_paidIns - m_drops + m_loans;
}

QJsonObject DrawerSession::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["drawerId"] = m_drawerId;
    json["employeeId"] = m_employeeId;
    json["startTime"] = m_startTime.toString(Qt::ISODate);
    if (m_endTime.isValid()) {
        json["endTime"] = m_endTime.toString(Qt::ISODate);
    }
    json["status"] = static_cast<int>(m_status);
    json["startingCash"] = m_startingCash;
    json["startingCount"] = m_startingCount.toJson();
    json["expectedCash"] = m_expectedCash;
    json["expectedTotal"] = m_expectedTotal;
    json["actualCash"] = m_actualCash;
    json["endingCount"] = m_endingCount.toJson();
    json["cashSales"] = m_cashSales;
    json["cashRefunds"] = m_cashRefunds;
    json["paidOuts"] = m_paidOuts;
    json["paidIns"] = m_paidIns;
    json["drops"] = m_drops;
    json["loans"] = m_loans;
    return json;
}

DrawerSession* DrawerSession::fromJson(const QJsonObject& json, QObject* parent) {
    auto* session = new DrawerSession(parent);
    session->m_id = json["id"].toInt();
    session->m_drawerId = json["drawerId"].toInt();
    session->m_employeeId = json["employeeId"].toInt();
    session->m_startTime = QDateTime::fromString(json["startTime"].toString(), Qt::ISODate);
    if (json.contains("endTime")) {
        session->m_endTime = QDateTime::fromString(json["endTime"].toString(), Qt::ISODate);
    }
    session->m_status = static_cast<SessionStatus>(json["status"].toInt());
    session->m_startingCash = json["startingCash"].toInt();
    session->m_startingCount = DenominationCount::fromJson(json["startingCount"].toObject());
    session->m_expectedCash = json["expectedCash"].toInt();
    session->m_expectedTotal = json["expectedTotal"].toInt();
    session->m_actualCash = json["actualCash"].toInt();
    session->m_endingCount = DenominationCount::fromJson(json["endingCount"].toObject());
    session->m_cashSales = json["cashSales"].toInt();
    session->m_cashRefunds = json["cashRefunds"].toInt();
    session->m_paidOuts = json["paidOuts"].toInt();
    session->m_paidIns = json["paidIns"].toInt();
    session->m_drops = json["drops"].toInt();
    session->m_loans = json["loans"].toInt();
    return session;
}

//=============================================================================
// DrawerConfig Implementation
//=============================================================================

DrawerConfig::DrawerConfig(QObject* parent)
    : QObject(parent)
{
}

QJsonObject DrawerConfig::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["terminalId"] = m_terminalId;
    json["printerId"] = m_printerId;
    json["enabled"] = m_enabled;
    json["defaultStartingCash"] = m_defaultStartingCash;
    json["requireStartingCount"] = m_requireStartingCount;
    json["requireEmployeeId"] = m_requireEmployeeId;
    json["blindDrops"] = m_blindDrops;
    json["blindClose"] = m_blindClose;
    json["overShortThreshold"] = m_overShortThreshold;
    return json;
}

DrawerConfig* DrawerConfig::fromJson(const QJsonObject& json, QObject* parent) {
    auto* config = new DrawerConfig(parent);
    config->m_id = json["id"].toInt();
    config->m_name = json["name"].toString();
    config->m_terminalId = json["terminalId"].toInt();
    config->m_printerId = json["printerId"].toInt();
    config->m_enabled = json["enabled"].toBool(true);
    config->m_defaultStartingCash = json["defaultStartingCash"].toInt(20000);
    config->m_requireStartingCount = json["requireStartingCount"].toBool();
    config->m_requireEmployeeId = json["requireEmployeeId"].toBool(true);
    config->m_blindDrops = json["blindDrops"].toBool();
    config->m_blindClose = json["blindClose"].toBool();
    config->m_overShortThreshold = json["overShortThreshold"].toInt(500);
    return config;
}

//=============================================================================
// CashDrawerManager Implementation
//=============================================================================

CashDrawerManager* CashDrawerManager::s_instance = nullptr;

CashDrawerManager::CashDrawerManager(QObject* parent)
    : QObject(parent)
{
}

CashDrawerManager* CashDrawerManager::instance() {
    if (!s_instance) {
        s_instance = new CashDrawerManager();
    }
    return s_instance;
}

void CashDrawerManager::addDrawer(DrawerConfig* drawer) {
    if (drawer->id() == 0) {
        drawer->setId(m_nextDrawerId++);
    }
    drawer->setParent(this);
    m_drawers.append(drawer);
    m_drawerStatus[drawer->id()] = DrawerStatus::Closed;
}

void CashDrawerManager::removeDrawer(int drawerId) {
    for (int i = 0; i < m_drawers.size(); ++i) {
        if (m_drawers[i]->id() == drawerId) {
            delete m_drawers.takeAt(i);
            m_drawerStatus.remove(drawerId);
            m_activeSessions.remove(drawerId);
            return;
        }
    }
}

DrawerConfig* CashDrawerManager::findDrawer(int id) {
    for (auto* drawer : m_drawers) {
        if (drawer->id() == id) return drawer;
    }
    return nullptr;
}

DrawerConfig* CashDrawerManager::drawerForTerminal(int terminalId) {
    for (auto* drawer : m_drawers) {
        if (drawer->terminalId() == terminalId && drawer->isEnabled()) {
            return drawer;
        }
    }
    return nullptr;
}

bool CashDrawerManager::openDrawer(int drawerId, int employeeId, int checkId) {
    auto* drawer = findDrawer(drawerId);
    if (!drawer || !drawer->isEnabled()) return false;
    
    // Check if session is active
    if (!m_activeSessions.contains(drawerId)) {
        return false;  // No active session
    }
    
    // Record the event
    auto* event = createEvent(drawerId, DrawerEventType::Open, employeeId, 0);
    event->setCheckId(checkId);
    
    // Kick the physical drawer
    kickDrawer(drawerId);
    
    m_drawerStatus[drawerId] = DrawerStatus::Open;
    emit drawerOpened(drawerId, employeeId);
    
    return true;
}

bool CashDrawerManager::kickDrawer(int drawerId) {
    auto* drawer = findDrawer(drawerId);
    if (!drawer || !drawer->isEnabled()) return false;
    
    // In a real implementation, this would send ESC/POS commands to the printer
    // ESC p m t1 t2 - where m=0 for pin 2, t1/t2 are pulse times
    // For now, just return success
    
    return true;
}

DrawerStatus CashDrawerManager::getStatus(int drawerId) {
    return m_drawerStatus.value(drawerId, DrawerStatus::Unknown);
}

DrawerSession* CashDrawerManager::startSession(int drawerId, int employeeId, int startingCash) {
    auto* drawer = findDrawer(drawerId);
    if (!drawer) return nullptr;
    
    // End any existing session
    if (m_activeSessions.contains(drawerId)) {
        auto* existingSession = m_activeSessions[drawerId];
        if (existingSession->status() == DrawerSession::SessionStatus::Active) {
            // Force end the session
            DenominationCount emptyCount;
            endSession(drawerId, emptyCount);
        }
    }
    
    auto* session = new DrawerSession(this);
    session->setId(m_nextSessionId++);
    session->setDrawerId(drawerId);
    session->setEmployeeId(employeeId);
    session->setStartingCash(startingCash);
    session->setStatus(DrawerSession::SessionStatus::Active);
    
    m_sessions.append(session);
    m_activeSessions[drawerId] = session;
    
    // Record starting cash event
    auto* event = createEvent(drawerId, DrawerEventType::StartingCash, employeeId, startingCash);
    Q_UNUSED(event);
    
    emit sessionStarted(session);
    return session;
}

DrawerSession* CashDrawerManager::currentSession(int drawerId) {
    return m_activeSessions.value(drawerId, nullptr);
}

bool CashDrawerManager::endSession(int drawerId, const DenominationCount& endingCount) {
    auto* session = currentSession(drawerId);
    if (!session) return false;
    
    auto* drawer = findDrawer(drawerId);
    
    session->setEndTime(QDateTime::currentDateTime());
    session->setEndingCount(endingCount);
    session->setActualCash(endingCount.totalCents());
    session->calculateExpected();
    
    int variance = session->overShort();
    
    if (variance == 0) {
        session->setStatus(DrawerSession::SessionStatus::Balanced);
    } else {
        session->setStatus(DrawerSession::SessionStatus::OverShort);
        
        // Check if variance exceeds threshold
        if (drawer && qAbs(variance) > drawer->overShortThreshold()) {
            emit overShortAlert(session, variance);
        }
    }
    
    // Record checkout event
    auto* event = createEvent(drawerId, DrawerEventType::CheckOut, 
                             session->employeeId(), session->actualCash());
    event->setDenominationCount(endingCount);
    
    m_activeSessions.remove(drawerId);
    m_drawerStatus[drawerId] = DrawerStatus::Closed;
    
    emit sessionEnded(session);
    emit drawerClosed(drawerId);
    
    return true;
}

QList<DrawerSession*> CashDrawerManager::sessionsForDrawer(int drawerId) {
    QList<DrawerSession*> result;
    for (auto* session : m_sessions) {
        if (session->drawerId() == drawerId) {
            result.append(session);
        }
    }
    return result;
}

QList<DrawerSession*> CashDrawerManager::sessionsForDate(const QDate& date) {
    QList<DrawerSession*> result;
    for (auto* session : m_sessions) {
        if (session->startTime().date() == date) {
            result.append(session);
        }
    }
    return result;
}

DrawerEvent* CashDrawerManager::createEvent(int drawerId, DrawerEventType type, 
                                            int employeeId, int amount) {
    auto* event = new DrawerEvent(this);
    event->setId(m_nextEventId++);
    event->setDrawerId(drawerId);
    event->setEventType(type);
    event->setEmployeeId(employeeId);
    event->setAmount(amount);
    
    m_events.append(event);
    return event;
}

bool CashDrawerManager::recordDrop(int drawerId, int employeeId, int amount, const QString& reason) {
    auto* session = currentSession(drawerId);
    if (!session) return false;
    
    auto* event = createEvent(drawerId, DrawerEventType::Drop, employeeId, amount);
    event->setReason(reason);
    
    session->addDrop(amount);
    
    emit cashDropped(drawerId, amount);
    return true;
}

bool CashDrawerManager::recordLoan(int drawerId, int employeeId, int amount, const QString& reason) {
    auto* session = currentSession(drawerId);
    if (!session) return false;
    
    auto* event = createEvent(drawerId, DrawerEventType::Loan, employeeId, amount);
    event->setReason(reason);
    
    session->addLoan(amount);
    return true;
}

bool CashDrawerManager::recordPaidOut(int drawerId, int employeeId, int amount, const QString& reason) {
    auto* session = currentSession(drawerId);
    if (!session) return false;
    
    auto* event = createEvent(drawerId, DrawerEventType::Drop, employeeId, amount);
    event->setReason("Paid Out: " + reason);
    
    session->addPaidOut(amount);
    return true;
}

bool CashDrawerManager::recordPaidIn(int drawerId, int employeeId, int amount, const QString& reason) {
    auto* session = currentSession(drawerId);
    if (!session) return false;
    
    auto* event = createEvent(drawerId, DrawerEventType::Loan, employeeId, amount);
    event->setReason("Paid In: " + reason);
    
    session->addPaidIn(amount);
    return true;
}

bool CashDrawerManager::recordAdjustment(int drawerId, int employeeId, int amount, const QString& reason) {
    auto* event = createEvent(drawerId, DrawerEventType::Adjustment, employeeId, amount);
    event->setReason(reason);
    event->setRequiresApproval(true);
    
    return true;
}

QList<DrawerEvent*> CashDrawerManager::eventsForDrawer(int drawerId) {
    QList<DrawerEvent*> result;
    for (auto* event : m_events) {
        if (event->drawerId() == drawerId) {
            result.append(event);
        }
    }
    return result;
}

QList<DrawerEvent*> CashDrawerManager::eventsForSession(int sessionId) {
    // Find the session to get drawer and time range
    DrawerSession* targetSession = nullptr;
    for (auto* session : m_sessions) {
        if (session->id() == sessionId) {
            targetSession = session;
            break;
        }
    }
    
    if (!targetSession) return QList<DrawerEvent*>();
    
    QList<DrawerEvent*> result;
    for (auto* event : m_events) {
        if (event->drawerId() == targetSession->drawerId()) {
            if (event->timestamp() >= targetSession->startTime()) {
                if (!targetSession->endTime().isValid() || 
                    event->timestamp() <= targetSession->endTime()) {
                    result.append(event);
                }
            }
        }
    }
    return result;
}

QList<DrawerEvent*> CashDrawerManager::eventsForDate(const QDate& date) {
    QList<DrawerEvent*> result;
    for (auto* event : m_events) {
        if (event->timestamp().date() == date) {
            result.append(event);
        }
    }
    return result;
}

int CashDrawerManager::totalCashInDrawers() {
    int total = 0;
    for (auto* session : m_activeSessions) {
        session->calculateExpected();
        total += session->expectedCash();
    }
    return total;
}

int CashDrawerManager::totalDropsForDate(const QDate& date) {
    int total = 0;
    for (const auto* event : m_events) {
        if (event->timestamp().date() == date && 
            event->eventType() == DrawerEventType::Drop) {
            total += event->amount();
        }
    }
    return total;
}

QMap<int, int> CashDrawerManager::overShortByEmployee(const QDate& start, const QDate& end) {
    QMap<int, int> result;
    for (const auto* session : m_sessions) {
        QDate d = session->startTime().date();
        if (d >= start && d <= end && 
            session->status() != DrawerSession::SessionStatus::Active) {
            result[session->employeeId()] += session->overShort();
        }
    }
    return result;
}

bool CashDrawerManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextDrawerId"] = m_nextDrawerId;
    root["nextSessionId"] = m_nextSessionId;
    root["nextEventId"] = m_nextEventId;
    
    QJsonArray drawerArray;
    for (const auto* drawer : m_drawers) {
        drawerArray.append(drawer->toJson());
    }
    root["drawers"] = drawerArray;
    
    QJsonArray sessionArray;
    for (const auto* session : m_sessions) {
        sessionArray.append(session->toJson());
    }
    root["sessions"] = sessionArray;
    
    QJsonArray eventArray;
    for (const auto* event : m_events) {
        eventArray.append(event->toJson());
    }
    root["events"] = eventArray;
    
    // Active sessions mapping
    QJsonObject activeObj;
    for (auto it = m_activeSessions.constBegin(); it != m_activeSessions.constEnd(); ++it) {
        activeObj[QString::number(it.key())] = it.value()->id();
    }
    root["activeSessions"] = activeObj;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool CashDrawerManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextDrawerId = root["nextDrawerId"].toInt(1);
    m_nextSessionId = root["nextSessionId"].toInt(1);
    m_nextEventId = root["nextEventId"].toInt(1);
    
    qDeleteAll(m_drawers);
    m_drawers.clear();
    
    QJsonArray drawerArray = root["drawers"].toArray();
    for (const auto& ref : drawerArray) {
        auto* drawer = DrawerConfig::fromJson(ref.toObject(), this);
        m_drawers.append(drawer);
        m_drawerStatus[drawer->id()] = DrawerStatus::Closed;
    }
    
    qDeleteAll(m_sessions);
    m_sessions.clear();
    
    QJsonArray sessionArray = root["sessions"].toArray();
    for (const auto& ref : sessionArray) {
        auto* session = DrawerSession::fromJson(ref.toObject(), this);
        m_sessions.append(session);
    }
    
    qDeleteAll(m_events);
    m_events.clear();
    
    QJsonArray eventArray = root["events"].toArray();
    for (const auto& ref : eventArray) {
        auto* event = DrawerEvent::fromJson(ref.toObject(), this);
        m_events.append(event);
    }
    
    // Restore active sessions
    m_activeSessions.clear();
    QJsonObject activeObj = root["activeSessions"].toObject();
    for (auto it = activeObj.constBegin(); it != activeObj.constEnd(); ++it) {
        int drawerId = it.key().toInt();
        int sessionId = it.value().toInt();
        
        for (auto* session : m_sessions) {
            if (session->id() == sessionId) {
                m_activeSessions[drawerId] = session;
                m_drawerStatus[drawerId] = DrawerStatus::Closed;
                break;
            }
        }
    }
    
    return true;
}

} // namespace vt2
