// ViewTouch V2 - Kitchen Display System (KDS)
// Modern C++/Qt6 reimplementation

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QJsonObject>
#include <QTimer>
#include <QColor>

namespace vt2 {

//=============================================================================
// Kitchen Order Status
//=============================================================================
enum class KitchenOrderStatus {
    New,            // Just received
    InProgress,     // Being prepared
    Ready,          // Ready for pickup
    Served,         // Delivered to customer
    Recalled,       // Called back for remake
    Cancelled       // Order cancelled
};

//=============================================================================
// Kitchen Item - Individual item in an order
//=============================================================================
class KitchenItem : public QObject {
    Q_OBJECT
    
public:
    explicit KitchenItem(QObject* parent = nullptr);
    
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    int menuItemId() const { return m_menuItemId; }
    void setMenuItemId(int id) { m_menuItemId = id; }
    
    QString name() const { return m_name; }
    void setName(const QString& n) { m_name = n; }
    
    int quantity() const { return m_quantity; }
    void setQuantity(int q) { m_quantity = q; }
    
    QStringList modifiers() const { return m_modifiers; }
    void setModifiers(const QStringList& m) { m_modifiers = m; }
    void addModifier(const QString& m) { m_modifiers.append(m); }
    
    QString specialInstructions() const { return m_specialInstructions; }
    void setSpecialInstructions(const QString& s) { m_specialInstructions = s; }
    
    int seatNumber() const { return m_seatNumber; }
    void setSeatNumber(int s) { m_seatNumber = s; }
    
    int courseNumber() const { return m_courseNumber; }
    void setCourseNumber(int c) { m_courseNumber = c; }
    
    KitchenOrderStatus status() const { return m_status; }
    void setStatus(KitchenOrderStatus s) { m_status = s; }
    
    QDateTime completedAt() const { return m_completedAt; }
    void setCompletedAt(const QDateTime& dt) { m_completedAt = dt; }
    
    bool isRush() const { return m_isRush; }
    void setRush(bool r) { m_isRush = r; }
    
    bool isVoid() const { return m_isVoid; }
    void setVoid(bool v) { m_isVoid = v; }
    
    int categoryId() const { return m_categoryId; }
    void setCategoryId(int id) { m_categoryId = id; }
    
    QJsonObject toJson() const;
    static KitchenItem* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    int m_menuItemId = 0;
    QString m_name;
    int m_quantity = 1;
    QStringList m_modifiers;
    QString m_specialInstructions;
    int m_seatNumber = 0;
    int m_courseNumber = 1;
    KitchenOrderStatus m_status = KitchenOrderStatus::New;
    QDateTime m_completedAt;
    bool m_isRush = false;
    bool m_isVoid = false;
    int m_categoryId = 0;
};

//=============================================================================
// Kitchen Order - A complete order ticket
//=============================================================================
class KitchenOrder : public QObject {
    Q_OBJECT
    
public:
    explicit KitchenOrder(QObject* parent = nullptr);
    ~KitchenOrder();
    
    // Identification
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    int checkId() const { return m_checkId; }
    void setCheckId(int id) { m_checkId = id; }
    
    QString checkNumber() const { return m_checkNumber; }
    void setCheckNumber(const QString& n) { m_checkNumber = n; }
    
    QString tableNumber() const { return m_tableNumber; }
    void setTableNumber(const QString& t) { m_tableNumber = t; }
    
    int guestCount() const { return m_guestCount; }
    void setGuestCount(int c) { m_guestCount = c; }
    
    // Server info
    int employeeId() const { return m_employeeId; }
    void setEmployeeId(int id) { m_employeeId = id; }
    
    QString serverName() const { return m_serverName; }
    void setServerName(const QString& n) { m_serverName = n; }
    
    // Timing
    QDateTime receivedAt() const { return m_receivedAt; }
    void setReceivedAt(const QDateTime& dt) { m_receivedAt = dt; }
    
    QDateTime startedAt() const { return m_startedAt; }
    void setStartedAt(const QDateTime& dt) { m_startedAt = dt; }
    
    QDateTime completedAt() const { return m_completedAt; }
    void setCompletedAt(const QDateTime& dt) { m_completedAt = dt; }
    
    int elapsedSeconds() const;  // Time since received
    int cookTimeSeconds() const; // Time from started to completed
    
    // Status
    KitchenOrderStatus status() const { return m_status; }
    void setStatus(KitchenOrderStatus s);
    
    bool isRush() const { return m_isRush; }
    void setRush(bool r) { m_isRush = r; }
    
    bool isVIP() const { return m_isVIP; }
    void setVIP(bool v) { m_isVIP = v; }
    
    // Items
    void addItem(KitchenItem* item);
    void removeItem(KitchenItem* item);
    QList<KitchenItem*> items() const { return m_items; }
    QList<KitchenItem*> itemsByCategory(int categoryId) const;
    QList<KitchenItem*> itemsByCourse(int course) const;
    
    int totalItemCount() const;
    int completedItemCount() const;
    bool allItemsComplete() const;
    
    // Station routing
    QList<int> stationIds() const { return m_stationIds; }
    void addStation(int stationId);
    void removeStation(int stationId);
    
    // Notes
    QString notes() const { return m_notes; }
    void setNotes(const QString& n) { m_notes = n; }
    
    QJsonObject toJson() const;
    static KitchenOrder* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
signals:
    void statusChanged(KitchenOrderStatus newStatus);
    void itemCompleted(KitchenItem* item);
    
private:
    int m_id = 0;
    int m_checkId = 0;
    QString m_checkNumber;
    QString m_tableNumber;
    int m_guestCount = 0;
    
    int m_employeeId = 0;
    QString m_serverName;
    
    QDateTime m_receivedAt;
    QDateTime m_startedAt;
    QDateTime m_completedAt;
    
    KitchenOrderStatus m_status = KitchenOrderStatus::New;
    bool m_isRush = false;
    bool m_isVIP = false;
    
    QList<KitchenItem*> m_items;
    QList<int> m_stationIds;
    QString m_notes;
};

//=============================================================================
// Kitchen Station Configuration
//=============================================================================
class KitchenStation : public QObject {
    Q_OBJECT
    
public:
    explicit KitchenStation(QObject* parent = nullptr);
    
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    QString name() const { return m_name; }
    void setName(const QString& n) { m_name = n; }
    
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool e) { m_enabled = e; }
    
    // Display settings
    int displayId() const { return m_displayId; }
    void setDisplayId(int id) { m_displayId = id; }
    
    int columns() const { return m_columns; }
    void setColumns(int c) { m_columns = c; }
    
    int maxOrders() const { return m_maxOrders; }
    void setMaxOrders(int m) { m_maxOrders = m; }
    
    // Routing - which categories go to this station
    QList<int> categoryIds() const { return m_categoryIds; }
    void setCategoryIds(const QList<int>& ids) { m_categoryIds = ids; }
    void addCategory(int id) { if (!m_categoryIds.contains(id)) m_categoryIds.append(id); }
    void removeCategory(int id) { m_categoryIds.removeAll(id); }
    
    // Timing thresholds (in seconds)
    int warningTime() const { return m_warningTime; }
    void setWarningTime(int secs) { m_warningTime = secs; }
    
    int urgentTime() const { return m_urgentTime; }
    void setUrgentTime(int secs) { m_urgentTime = secs; }
    
    int lateTime() const { return m_lateTime; }
    void setLateTime(int secs) { m_lateTime = secs; }
    
    // Colors
    QColor normalColor() const { return m_normalColor; }
    void setNormalColor(const QColor& c) { m_normalColor = c; }
    
    QColor warningColor() const { return m_warningColor; }
    void setWarningColor(const QColor& c) { m_warningColor = c; }
    
    QColor urgentColor() const { return m_urgentColor; }
    void setUrgentColor(const QColor& c) { m_urgentColor = c; }
    
    QColor lateColor() const { return m_lateColor; }
    void setLateColor(const QColor& c) { m_lateColor = c; }
    
    // Sound alerts
    bool playSoundOnNew() const { return m_playSoundOnNew; }
    void setPlaySoundOnNew(bool p) { m_playSoundOnNew = p; }
    
    bool playSoundOnUrgent() const { return m_playSoundOnUrgent; }
    void setPlaySoundOnUrgent(bool p) { m_playSoundOnUrgent = p; }
    
    QJsonObject toJson() const;
    static KitchenStation* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    QString m_name;
    bool m_enabled = true;
    
    int m_displayId = 0;
    int m_columns = 4;
    int m_maxOrders = 20;
    
    QList<int> m_categoryIds;
    
    int m_warningTime = 300;   // 5 minutes
    int m_urgentTime = 600;    // 10 minutes
    int m_lateTime = 900;      // 15 minutes
    
    QColor m_normalColor = QColor(0, 128, 0);      // Green
    QColor m_warningColor = QColor(255, 165, 0);   // Orange
    QColor m_urgentColor = QColor(255, 0, 0);      // Red
    QColor m_lateColor = QColor(128, 0, 128);      // Purple
    
    bool m_playSoundOnNew = true;
    bool m_playSoundOnUrgent = true;
};

//=============================================================================
// Kitchen Display Manager - Singleton
//=============================================================================
class KitchenDisplayManager : public QObject {
    Q_OBJECT
    
public:
    static KitchenDisplayManager* instance();
    
    // Station management
    void addStation(KitchenStation* station);
    void removeStation(int stationId);
    KitchenStation* findStation(int id);
    QList<KitchenStation*> allStations() const { return m_stations; }
    
    // Order management
    KitchenOrder* createOrder(int checkId, const QString& tableNumber, 
                              int employeeId, const QString& serverName);
    KitchenOrder* findOrder(int orderId);
    KitchenOrder* findOrderByCheck(int checkId);
    
    // Order actions
    void sendOrder(KitchenOrder* order);
    void startOrder(int orderId);
    void completeItem(int orderId, int itemId);
    void completeOrder(int orderId);
    void recallOrder(int orderId);
    void cancelOrder(int orderId);
    void bumpOrder(int orderId);  // Remove from display
    
    // Query orders
    QList<KitchenOrder*> ordersForStation(int stationId);
    QList<KitchenOrder*> activeOrders();
    QList<KitchenOrder*> completedOrders(const QDate& date);
    
    // Statistics
    int averageCookTime(const QDate& date);  // In seconds
    int ordersCompleted(const QDate& date);
    int ordersLate(const QDate& date);
    QMap<int, int> ordersByStation(const QDate& date);
    
    // Display refresh
    void refreshDisplays();
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    
signals:
    void orderReceived(KitchenOrder* order);
    void orderStarted(KitchenOrder* order);
    void orderCompleted(KitchenOrder* order);
    void orderCancelled(KitchenOrder* order);
    void itemCompleted(KitchenOrder* order, KitchenItem* item);
    void orderTimingAlert(KitchenOrder* order, int elapsedSeconds);
    void displayRefreshNeeded();
    
private slots:
    void checkTimers();
    
private:
    explicit KitchenDisplayManager(QObject* parent = nullptr);
    static KitchenDisplayManager* s_instance;
    
    void routeOrderToStations(KitchenOrder* order);
    QColor colorForOrder(KitchenOrder* order, KitchenStation* station);
    
    QList<KitchenStation*> m_stations;
    QList<KitchenOrder*> m_orders;
    QList<KitchenOrder*> m_completedOrders;
    
    QTimer* m_timerCheck;
    
    int m_nextStationId = 1;
    int m_nextOrderId = 1;
    int m_nextItemId = 1;
};

} // namespace vt2
