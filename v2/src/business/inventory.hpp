// ViewTouch V2 - Inventory System
// Modern C++/Qt6 reimplementation

#ifndef VT2_INVENTORY_HPP
#define VT2_INVENTORY_HPP

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QJsonObject>

namespace vt2 {

// Unit types
enum class UnitType {
    Each = 0,
    Pound = 1,
    Ounce = 2,
    Gallon = 3,
    Quart = 4,
    Pint = 5,
    Cup = 6,
    Liter = 7,
    Milliliter = 8,
    Kilogram = 9,
    Gram = 10,
    Case = 11,
    Box = 12,
    Bag = 13,
    Bottle = 14,
    Can = 15,
    Dozen = 16
};

//=============================================================================
// InventoryItem - A single inventory item
//=============================================================================
class InventoryItem : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(double quantity READ quantity WRITE setQuantity NOTIFY quantityChanged)
    Q_PROPERTY(double reorderLevel READ reorderLevel WRITE setReorderLevel NOTIFY reorderLevelChanged)

public:
    explicit InventoryItem(QObject* parent = nullptr);
    InventoryItem(const QString& name, QObject* parent = nullptr);
    ~InventoryItem() override = default;

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    QString name() const { return m_name; }
    void setName(const QString& name);
    
    QString description() const { return m_description; }
    void setDescription(const QString& desc) { m_description = desc; }
    
    QString sku() const { return m_sku; }
    void setSku(const QString& sku) { m_sku = sku; }
    
    QString barcode() const { return m_barcode; }
    void setBarcode(const QString& code) { m_barcode = code; }
    
    // Category
    QString category() const { return m_category; }
    void setCategory(const QString& cat) { m_category = cat; }
    
    QString vendor() const { return m_vendor; }
    void setVendor(const QString& vendor) { m_vendor = vendor; }
    
    // Quantity
    UnitType unit() const { return m_unit; }
    void setUnit(UnitType unit) { m_unit = unit; }
    
    double quantity() const { return m_quantity; }
    void setQuantity(double qty);
    void adjustQuantity(double delta);
    
    double reorderLevel() const { return m_reorderLevel; }
    void setReorderLevel(double level);
    
    double parLevel() const { return m_parLevel; }
    void setParLevel(double level) { m_parLevel = level; }
    
    bool needsReorder() const { return m_quantity <= m_reorderLevel; }
    
    // Cost
    int costPerUnit() const { return m_costPerUnit; }  // In cents
    void setCostPerUnit(int cost) { m_costPerUnit = cost; }
    
    int totalValue() const { return static_cast<int>(m_quantity * m_costPerUnit); }
    
    // Dates
    QDateTime lastReceived() const { return m_lastReceived; }
    void setLastReceived(const QDateTime& dt) { m_lastReceived = dt; }
    
    QDateTime lastCounted() const { return m_lastCounted; }
    void setLastCounted(const QDateTime& dt) { m_lastCounted = dt; }
    
    // Active
    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }
    
    // Serialization
    QJsonObject toJson() const;
    static InventoryItem* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void nameChanged();
    void quantityChanged();
    void reorderLevelChanged();

private:
    int m_id = 0;
    QString m_name;
    QString m_description;
    QString m_sku;
    QString m_barcode;
    QString m_category;
    QString m_vendor;
    
    UnitType m_unit = UnitType::Each;
    double m_quantity = 0.0;
    double m_reorderLevel = 0.0;
    double m_parLevel = 0.0;
    
    int m_costPerUnit = 0;
    
    QDateTime m_lastReceived;
    QDateTime m_lastCounted;
    
    bool m_active = true;
};

//=============================================================================
// InventoryTransaction - Record of inventory change
//=============================================================================
class InventoryTransaction : public QObject {
    Q_OBJECT

public:
    enum class TransactionType {
        Received = 1,
        Used = 2,
        Wasted = 3,
        Counted = 4,
        Transferred = 5,
        Returned = 6
    };

    explicit InventoryTransaction(QObject* parent = nullptr);
    ~InventoryTransaction() override = default;

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    int itemId() const { return m_itemId; }
    void setItemId(int id) { m_itemId = id; }
    
    TransactionType type() const { return m_type; }
    void setType(TransactionType type) { m_type = type; }
    
    double quantity() const { return m_quantity; }
    void setQuantity(double qty) { m_quantity = qty; }
    
    double previousQuantity() const { return m_previousQuantity; }
    void setPreviousQuantity(double qty) { m_previousQuantity = qty; }
    
    int employeeId() const { return m_employeeId; }
    void setEmployeeId(int id) { m_employeeId = id; }
    
    QDateTime timestamp() const { return m_timestamp; }
    void setTimestamp(const QDateTime& ts) { m_timestamp = ts; }
    
    QString notes() const { return m_notes; }
    void setNotes(const QString& notes) { m_notes = notes; }
    
    int cost() const { return m_cost; }  // Total cost of transaction
    void setCost(int cost) { m_cost = cost; }
    
    // Serialization
    QJsonObject toJson() const;
    static InventoryTransaction* fromJson(const QJsonObject& json, QObject* parent = nullptr);

private:
    int m_id = 0;
    int m_itemId = 0;
    TransactionType m_type = TransactionType::Received;
    double m_quantity = 0.0;
    double m_previousQuantity = 0.0;
    int m_employeeId = 0;
    QDateTime m_timestamp;
    QString m_notes;
    int m_cost = 0;
};

//=============================================================================
// InventoryManager - Manages all inventory
//=============================================================================
class InventoryManager : public QObject {
    Q_OBJECT

public:
    static InventoryManager* instance();
    
    // Item management
    InventoryItem* createItem(const QString& name);
    InventoryItem* findById(int id);
    InventoryItem* findBySku(const QString& sku);
    InventoryItem* findByBarcode(const QString& barcode);
    QList<InventoryItem*> searchByName(const QString& name);
    QList<InventoryItem*> allItems() const { return m_items; }
    QList<InventoryItem*> activeItems() const;
    QList<InventoryItem*> lowStockItems() const;
    QList<InventoryItem*> itemsByCategory(const QString& category) const;
    
    void deleteItem(InventoryItem* item);
    
    // Transactions
    void receiveInventory(InventoryItem* item, double quantity, int cost, int employeeId, const QString& notes = QString());
    void useInventory(InventoryItem* item, double quantity, int employeeId, const QString& notes = QString());
    void wasteInventory(InventoryItem* item, double quantity, int employeeId, const QString& notes = QString());
    void countInventory(InventoryItem* item, double newQuantity, int employeeId, const QString& notes = QString());
    
    QList<InventoryTransaction*> transactionsForItem(int itemId) const;
    QList<InventoryTransaction*> transactionsForPeriod(const QDate& start, const QDate& end) const;
    QList<InventoryTransaction*> allTransactions() const { return m_transactions; }
    
    // Categories
    QStringList categories() const;
    
    // Stats
    int itemCount() const { return m_items.size(); }
    int lowStockCount() const { return lowStockItems().size(); }
    int totalInventoryValue() const;
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

signals:
    void itemCreated(InventoryItem* item);
    void itemDeleted(InventoryItem* item);
    void itemUpdated(InventoryItem* item);
    void transactionRecorded(InventoryTransaction* tx);
    void inventoryChanged();
    void lowStockAlert(InventoryItem* item);

private:
    explicit InventoryManager(QObject* parent = nullptr);
    void recordTransaction(InventoryItem* item, InventoryTransaction::TransactionType type, 
                           double quantity, int employeeId, const QString& notes, int cost = 0);
    
    static InventoryManager* s_instance;
    
    QList<InventoryItem*> m_items;
    QList<InventoryTransaction*> m_transactions;
    int m_nextItemId = 1;
    int m_nextTransactionId = 1;
};

// Helper function
QString unitTypeToString(UnitType unit);

} // namespace vt2

#endif // VT2_INVENTORY_HPP
