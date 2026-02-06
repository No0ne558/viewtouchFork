// ViewTouch V2 - Kitchen Display System Implementation
// Modern C++/Qt6 reimplementation

#include "kitchen_display.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

namespace vt2 {

//=============================================================================
// KitchenItem Implementation
//=============================================================================

KitchenItem::KitchenItem(QObject* parent)
    : QObject(parent)
{
}

QJsonObject KitchenItem::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["menuItemId"] = m_menuItemId;
    json["name"] = m_name;
    json["quantity"] = m_quantity;
    
    QJsonArray modArray;
    for (const auto& mod : m_modifiers) {
        modArray.append(mod);
    }
    json["modifiers"] = modArray;
    
    json["specialInstructions"] = m_specialInstructions;
    json["seatNumber"] = m_seatNumber;
    json["courseNumber"] = m_courseNumber;
    json["status"] = static_cast<int>(m_status);
    if (m_completedAt.isValid()) {
        json["completedAt"] = m_completedAt.toString(Qt::ISODate);
    }
    json["isRush"] = m_isRush;
    json["isVoid"] = m_isVoid;
    json["categoryId"] = m_categoryId;
    return json;
}

KitchenItem* KitchenItem::fromJson(const QJsonObject& json, QObject* parent) {
    auto* item = new KitchenItem(parent);
    item->m_id = json["id"].toInt();
    item->m_menuItemId = json["menuItemId"].toInt();
    item->m_name = json["name"].toString();
    item->m_quantity = json["quantity"].toInt(1);
    
    QJsonArray modArray = json["modifiers"].toArray();
    for (const auto& ref : modArray) {
        item->m_modifiers.append(ref.toString());
    }
    
    item->m_specialInstructions = json["specialInstructions"].toString();
    item->m_seatNumber = json["seatNumber"].toInt();
    item->m_courseNumber = json["courseNumber"].toInt(1);
    item->m_status = static_cast<KitchenOrderStatus>(json["status"].toInt());
    if (json.contains("completedAt")) {
        item->m_completedAt = QDateTime::fromString(json["completedAt"].toString(), Qt::ISODate);
    }
    item->m_isRush = json["isRush"].toBool();
    item->m_isVoid = json["isVoid"].toBool();
    item->m_categoryId = json["categoryId"].toInt();
    return item;
}

//=============================================================================
// KitchenOrder Implementation
//=============================================================================

KitchenOrder::KitchenOrder(QObject* parent)
    : QObject(parent)
    , m_receivedAt(QDateTime::currentDateTime())
{
}

KitchenOrder::~KitchenOrder() {
    qDeleteAll(m_items);
}

int KitchenOrder::elapsedSeconds() const {
    if (!m_receivedAt.isValid()) return 0;
    return m_receivedAt.secsTo(QDateTime::currentDateTime());
}

int KitchenOrder::cookTimeSeconds() const {
    if (!m_startedAt.isValid() || !m_completedAt.isValid()) return 0;
    return m_startedAt.secsTo(m_completedAt);
}

void KitchenOrder::setStatus(KitchenOrderStatus s) {
    if (m_status != s) {
        m_status = s;
        
        if (s == KitchenOrderStatus::InProgress && !m_startedAt.isValid()) {
            m_startedAt = QDateTime::currentDateTime();
        } else if (s == KitchenOrderStatus::Ready && !m_completedAt.isValid()) {
            m_completedAt = QDateTime::currentDateTime();
        }
        
        emit statusChanged(s);
    }
}

void KitchenOrder::addItem(KitchenItem* item) {
    item->setParent(this);
    m_items.append(item);
}

void KitchenOrder::removeItem(KitchenItem* item) {
    m_items.removeOne(item);
    delete item;
}

QList<KitchenItem*> KitchenOrder::itemsByCategory(int categoryId) const {
    QList<KitchenItem*> result;
    for (auto* item : m_items) {
        if (item->categoryId() == categoryId) {
            result.append(item);
        }
    }
    return result;
}

QList<KitchenItem*> KitchenOrder::itemsByCourse(int course) const {
    QList<KitchenItem*> result;
    for (auto* item : m_items) {
        if (item->courseNumber() == course) {
            result.append(item);
        }
    }
    return result;
}

int KitchenOrder::totalItemCount() const {
    int count = 0;
    for (const auto* item : m_items) {
        if (!item->isVoid()) {
            count += item->quantity();
        }
    }
    return count;
}

int KitchenOrder::completedItemCount() const {
    int count = 0;
    for (const auto* item : m_items) {
        if (!item->isVoid() && item->status() == KitchenOrderStatus::Ready) {
            count += item->quantity();
        }
    }
    return count;
}

bool KitchenOrder::allItemsComplete() const {
    for (const auto* item : m_items) {
        if (!item->isVoid() && item->status() != KitchenOrderStatus::Ready) {
            return false;
        }
    }
    return true;
}

void KitchenOrder::addStation(int stationId) {
    if (!m_stationIds.contains(stationId)) {
        m_stationIds.append(stationId);
    }
}

void KitchenOrder::removeStation(int stationId) {
    m_stationIds.removeAll(stationId);
}

QJsonObject KitchenOrder::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["checkId"] = m_checkId;
    json["checkNumber"] = m_checkNumber;
    json["tableNumber"] = m_tableNumber;
    json["guestCount"] = m_guestCount;
    json["employeeId"] = m_employeeId;
    json["serverName"] = m_serverName;
    json["receivedAt"] = m_receivedAt.toString(Qt::ISODate);
    if (m_startedAt.isValid()) {
        json["startedAt"] = m_startedAt.toString(Qt::ISODate);
    }
    if (m_completedAt.isValid()) {
        json["completedAt"] = m_completedAt.toString(Qt::ISODate);
    }
    json["status"] = static_cast<int>(m_status);
    json["isRush"] = m_isRush;
    json["isVIP"] = m_isVIP;
    
    QJsonArray itemArray;
    for (const auto* item : m_items) {
        itemArray.append(item->toJson());
    }
    json["items"] = itemArray;
    
    QJsonArray stationArray;
    for (int id : m_stationIds) {
        stationArray.append(id);
    }
    json["stationIds"] = stationArray;
    
    json["notes"] = m_notes;
    return json;
}

KitchenOrder* KitchenOrder::fromJson(const QJsonObject& json, QObject* parent) {
    auto* order = new KitchenOrder(parent);
    order->m_id = json["id"].toInt();
    order->m_checkId = json["checkId"].toInt();
    order->m_checkNumber = json["checkNumber"].toString();
    order->m_tableNumber = json["tableNumber"].toString();
    order->m_guestCount = json["guestCount"].toInt();
    order->m_employeeId = json["employeeId"].toInt();
    order->m_serverName = json["serverName"].toString();
    order->m_receivedAt = QDateTime::fromString(json["receivedAt"].toString(), Qt::ISODate);
    if (json.contains("startedAt")) {
        order->m_startedAt = QDateTime::fromString(json["startedAt"].toString(), Qt::ISODate);
    }
    if (json.contains("completedAt")) {
        order->m_completedAt = QDateTime::fromString(json["completedAt"].toString(), Qt::ISODate);
    }
    order->m_status = static_cast<KitchenOrderStatus>(json["status"].toInt());
    order->m_isRush = json["isRush"].toBool();
    order->m_isVIP = json["isVIP"].toBool();
    
    QJsonArray itemArray = json["items"].toArray();
    for (const auto& ref : itemArray) {
        auto* item = KitchenItem::fromJson(ref.toObject(), order);
        order->m_items.append(item);
    }
    
    QJsonArray stationArray = json["stationIds"].toArray();
    for (const auto& ref : stationArray) {
        order->m_stationIds.append(ref.toInt());
    }
    
    order->m_notes = json["notes"].toString();
    return order;
}

//=============================================================================
// KitchenStation Implementation
//=============================================================================

KitchenStation::KitchenStation(QObject* parent)
    : QObject(parent)
{
}

QJsonObject KitchenStation::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["enabled"] = m_enabled;
    json["displayId"] = m_displayId;
    json["columns"] = m_columns;
    json["maxOrders"] = m_maxOrders;
    
    QJsonArray catArray;
    for (int id : m_categoryIds) {
        catArray.append(id);
    }
    json["categoryIds"] = catArray;
    
    json["warningTime"] = m_warningTime;
    json["urgentTime"] = m_urgentTime;
    json["lateTime"] = m_lateTime;
    
    json["normalColor"] = m_normalColor.name();
    json["warningColor"] = m_warningColor.name();
    json["urgentColor"] = m_urgentColor.name();
    json["lateColor"] = m_lateColor.name();
    
    json["playSoundOnNew"] = m_playSoundOnNew;
    json["playSoundOnUrgent"] = m_playSoundOnUrgent;
    
    return json;
}

KitchenStation* KitchenStation::fromJson(const QJsonObject& json, QObject* parent) {
    auto* station = new KitchenStation(parent);
    station->m_id = json["id"].toInt();
    station->m_name = json["name"].toString();
    station->m_enabled = json["enabled"].toBool(true);
    station->m_displayId = json["displayId"].toInt();
    station->m_columns = json["columns"].toInt(4);
    station->m_maxOrders = json["maxOrders"].toInt(20);
    
    QJsonArray catArray = json["categoryIds"].toArray();
    for (const auto& ref : catArray) {
        station->m_categoryIds.append(ref.toInt());
    }
    
    station->m_warningTime = json["warningTime"].toInt(300);
    station->m_urgentTime = json["urgentTime"].toInt(600);
    station->m_lateTime = json["lateTime"].toInt(900);
    
    station->m_normalColor = QColor(json["normalColor"].toString("#008000"));
    station->m_warningColor = QColor(json["warningColor"].toString("#FFA500"));
    station->m_urgentColor = QColor(json["urgentColor"].toString("#FF0000"));
    station->m_lateColor = QColor(json["lateColor"].toString("#800080"));
    
    station->m_playSoundOnNew = json["playSoundOnNew"].toBool(true);
    station->m_playSoundOnUrgent = json["playSoundOnUrgent"].toBool(true);
    
    return station;
}

//=============================================================================
// KitchenDisplayManager Implementation
//=============================================================================

KitchenDisplayManager* KitchenDisplayManager::s_instance = nullptr;

KitchenDisplayManager::KitchenDisplayManager(QObject* parent)
    : QObject(parent)
    , m_timerCheck(new QTimer(this))
{
    // Check timing every 10 seconds
    connect(m_timerCheck, &QTimer::timeout, this, &KitchenDisplayManager::checkTimers);
    m_timerCheck->start(10000);
}

KitchenDisplayManager* KitchenDisplayManager::instance() {
    if (!s_instance) {
        s_instance = new KitchenDisplayManager();
    }
    return s_instance;
}

void KitchenDisplayManager::addStation(KitchenStation* station) {
    if (station->id() == 0) {
        station->setId(m_nextStationId++);
    }
    station->setParent(this);
    m_stations.append(station);
}

void KitchenDisplayManager::removeStation(int stationId) {
    for (int i = 0; i < m_stations.size(); ++i) {
        if (m_stations[i]->id() == stationId) {
            delete m_stations.takeAt(i);
            return;
        }
    }
}

KitchenStation* KitchenDisplayManager::findStation(int id) {
    for (auto* station : m_stations) {
        if (station->id() == id) return station;
    }
    return nullptr;
}

KitchenOrder* KitchenDisplayManager::createOrder(int checkId, const QString& tableNumber,
                                                  int employeeId, const QString& serverName) {
    auto* order = new KitchenOrder(this);
    order->setId(m_nextOrderId++);
    order->setCheckId(checkId);
    order->setTableNumber(tableNumber);
    order->setEmployeeId(employeeId);
    order->setServerName(serverName);
    order->setStatus(KitchenOrderStatus::New);
    
    return order;
}

KitchenOrder* KitchenDisplayManager::findOrder(int orderId) {
    for (auto* order : m_orders) {
        if (order->id() == orderId) return order;
    }
    return nullptr;
}

KitchenOrder* KitchenDisplayManager::findOrderByCheck(int checkId) {
    for (auto* order : m_orders) {
        if (order->checkId() == checkId) return order;
    }
    return nullptr;
}

void KitchenDisplayManager::sendOrder(KitchenOrder* order) {
    if (!m_orders.contains(order)) {
        m_orders.append(order);
        order->setParent(this);
    }
    
    order->setStatus(KitchenOrderStatus::New);
    routeOrderToStations(order);
    
    emit orderReceived(order);
    emit displayRefreshNeeded();
}

void KitchenDisplayManager::startOrder(int orderId) {
    auto* order = findOrder(orderId);
    if (!order) return;
    
    order->setStatus(KitchenOrderStatus::InProgress);
    
    // Update all items
    for (auto* item : order->items()) {
        if (item->status() == KitchenOrderStatus::New) {
            item->setStatus(KitchenOrderStatus::InProgress);
        }
    }
    
    emit orderStarted(order);
    emit displayRefreshNeeded();
}

void KitchenDisplayManager::completeItem(int orderId, int itemId) {
    auto* order = findOrder(orderId);
    if (!order) return;
    
    for (auto* item : order->items()) {
        if (item->id() == itemId) {
            item->setStatus(KitchenOrderStatus::Ready);
            item->setCompletedAt(QDateTime::currentDateTime());
            
            emit itemCompleted(order, item);
            emit order->itemCompleted(item);
            
            // Check if all items are done
            if (order->allItemsComplete()) {
                completeOrder(orderId);
            }
            
            emit displayRefreshNeeded();
            return;
        }
    }
}

void KitchenDisplayManager::completeOrder(int orderId) {
    auto* order = findOrder(orderId);
    if (!order) return;
    
    order->setStatus(KitchenOrderStatus::Ready);
    
    // Mark all items as ready
    for (auto* item : order->items()) {
        if (item->status() != KitchenOrderStatus::Ready) {
            item->setStatus(KitchenOrderStatus::Ready);
            item->setCompletedAt(QDateTime::currentDateTime());
        }
    }
    
    emit orderCompleted(order);
    emit displayRefreshNeeded();
}

void KitchenDisplayManager::recallOrder(int orderId) {
    auto* order = findOrder(orderId);
    if (!order) return;
    
    order->setStatus(KitchenOrderStatus::Recalled);
    
    // Move back to active orders if it was in completed
    if (m_completedOrders.contains(order)) {
        m_completedOrders.removeOne(order);
        if (!m_orders.contains(order)) {
            m_orders.append(order);
        }
    }
    
    emit displayRefreshNeeded();
}

void KitchenDisplayManager::cancelOrder(int orderId) {
    auto* order = findOrder(orderId);
    if (!order) return;
    
    order->setStatus(KitchenOrderStatus::Cancelled);
    
    emit orderCancelled(order);
    
    // Remove from active orders
    m_orders.removeOne(order);
    
    emit displayRefreshNeeded();
}

void KitchenDisplayManager::bumpOrder(int orderId) {
    auto* order = findOrder(orderId);
    if (!order) return;
    
    // Move to completed orders
    m_orders.removeOne(order);
    m_completedOrders.append(order);
    
    order->setStatus(KitchenOrderStatus::Served);
    
    emit displayRefreshNeeded();
}

QList<KitchenOrder*> KitchenDisplayManager::ordersForStation(int stationId) {
    QList<KitchenOrder*> result;
    for (auto* order : m_orders) {
        if (order->stationIds().contains(stationId)) {
            result.append(order);
        }
    }
    return result;
}

QList<KitchenOrder*> KitchenDisplayManager::activeOrders() {
    QList<KitchenOrder*> result;
    for (auto* order : m_orders) {
        if (order->status() != KitchenOrderStatus::Cancelled &&
            order->status() != KitchenOrderStatus::Served) {
            result.append(order);
        }
    }
    return result;
}

QList<KitchenOrder*> KitchenDisplayManager::completedOrders(const QDate& date) {
    QList<KitchenOrder*> result;
    for (auto* order : m_completedOrders) {
        if (order->completedAt().date() == date) {
            result.append(order);
        }
    }
    return result;
}

void KitchenDisplayManager::routeOrderToStations(KitchenOrder* order) {
    order->stationIds().clear();
    
    for (auto* station : m_stations) {
        if (!station->isEnabled()) continue;
        
        // Check if any items in the order belong to this station's categories
        for (const auto* item : order->items()) {
            if (station->categoryIds().contains(item->categoryId())) {
                order->addStation(station->id());
                break;
            }
        }
    }
    
    // If no stations matched, add to all stations
    if (order->stationIds().isEmpty()) {
        for (auto* station : m_stations) {
            if (station->isEnabled()) {
                order->addStation(station->id());
            }
        }
    }
}

QColor KitchenDisplayManager::colorForOrder(KitchenOrder* order, KitchenStation* station) {
    int elapsed = order->elapsedSeconds();
    
    if (elapsed >= station->lateTime()) {
        return station->lateColor();
    } else if (elapsed >= station->urgentTime()) {
        return station->urgentColor();
    } else if (elapsed >= station->warningTime()) {
        return station->warningColor();
    }
    return station->normalColor();
}

void KitchenDisplayManager::checkTimers() {
    for (auto* order : m_orders) {
        if (order->status() == KitchenOrderStatus::New ||
            order->status() == KitchenOrderStatus::InProgress) {
            
            int elapsed = order->elapsedSeconds();
            
            // Find the tightest threshold among assigned stations
            for (int stationId : order->stationIds()) {
                auto* station = findStation(stationId);
                if (!station) continue;
                
                if (elapsed >= station->urgentTime() && elapsed < station->urgentTime() + 10) {
                    emit orderTimingAlert(order, elapsed);
                }
            }
        }
    }
    
    emit displayRefreshNeeded();
}

int KitchenDisplayManager::averageCookTime(const QDate& date) {
    int total = 0;
    int count = 0;
    
    for (const auto* order : m_completedOrders) {
        if (order->completedAt().date() == date) {
            int cookTime = order->cookTimeSeconds();
            if (cookTime > 0) {
                total += cookTime;
                count++;
            }
        }
    }
    
    return (count > 0) ? (total / count) : 0;
}

int KitchenDisplayManager::ordersCompleted(const QDate& date) {
    int count = 0;
    for (const auto* order : m_completedOrders) {
        if (order->completedAt().date() == date) {
            count++;
        }
    }
    return count;
}

int KitchenDisplayManager::ordersLate(const QDate& date) {
    int count = 0;
    
    // Find the maximum "late" time across all stations
    int maxLateTime = 900;  // Default 15 minutes
    for (const auto* station : m_stations) {
        if (station->lateTime() > maxLateTime) {
            maxLateTime = station->lateTime();
        }
    }
    
    for (const auto* order : m_completedOrders) {
        if (order->completedAt().date() == date) {
            if (order->elapsedSeconds() > maxLateTime) {
                count++;
            }
        }
    }
    return count;
}

QMap<int, int> KitchenDisplayManager::ordersByStation(const QDate& date) {
    QMap<int, int> result;
    
    for (const auto* order : m_completedOrders) {
        if (order->completedAt().date() == date) {
            for (int stationId : order->stationIds()) {
                result[stationId]++;
            }
        }
    }
    
    return result;
}

void KitchenDisplayManager::refreshDisplays() {
    emit displayRefreshNeeded();
}

bool KitchenDisplayManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextStationId"] = m_nextStationId;
    root["nextOrderId"] = m_nextOrderId;
    root["nextItemId"] = m_nextItemId;
    
    QJsonArray stationArray;
    for (const auto* station : m_stations) {
        stationArray.append(station->toJson());
    }
    root["stations"] = stationArray;
    
    QJsonArray orderArray;
    for (const auto* order : m_orders) {
        orderArray.append(order->toJson());
    }
    root["orders"] = orderArray;
    
    QJsonArray completedArray;
    for (const auto* order : m_completedOrders) {
        completedArray.append(order->toJson());
    }
    root["completedOrders"] = completedArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool KitchenDisplayManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextStationId = root["nextStationId"].toInt(1);
    m_nextOrderId = root["nextOrderId"].toInt(1);
    m_nextItemId = root["nextItemId"].toInt(1);
    
    qDeleteAll(m_stations);
    m_stations.clear();
    
    QJsonArray stationArray = root["stations"].toArray();
    for (const auto& ref : stationArray) {
        auto* station = KitchenStation::fromJson(ref.toObject(), this);
        m_stations.append(station);
    }
    
    qDeleteAll(m_orders);
    m_orders.clear();
    
    QJsonArray orderArray = root["orders"].toArray();
    for (const auto& ref : orderArray) {
        auto* order = KitchenOrder::fromJson(ref.toObject(), this);
        m_orders.append(order);
    }
    
    qDeleteAll(m_completedOrders);
    m_completedOrders.clear();
    
    QJsonArray completedArray = root["completedOrders"].toArray();
    for (const auto& ref : completedArray) {
        auto* order = KitchenOrder::fromJson(ref.toObject(), this);
        m_completedOrders.append(order);
    }
    
    return true;
}

} // namespace vt2
