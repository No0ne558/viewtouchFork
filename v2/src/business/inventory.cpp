// ViewTouch V2 - Inventory System Implementation
// Modern C++/Qt6 reimplementation

#include "inventory.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QSet>

namespace vt2 {

QString unitTypeToString(UnitType unit) {
    switch (unit) {
        case UnitType::Each:       return "ea";
        case UnitType::Pound:      return "lb";
        case UnitType::Ounce:      return "oz";
        case UnitType::Gallon:     return "gal";
        case UnitType::Quart:      return "qt";
        case UnitType::Pint:       return "pt";
        case UnitType::Cup:        return "cup";
        case UnitType::Liter:      return "L";
        case UnitType::Milliliter: return "mL";
        case UnitType::Kilogram:   return "kg";
        case UnitType::Gram:       return "g";
        case UnitType::Case:       return "case";
        case UnitType::Box:        return "box";
        case UnitType::Bag:        return "bag";
        case UnitType::Bottle:     return "btl";
        case UnitType::Can:        return "can";
        case UnitType::Dozen:      return "dz";
        default:                   return "ea";
    }
}

//=============================================================================
// InventoryItem Implementation
//=============================================================================

InventoryItem::InventoryItem(QObject* parent)
    : QObject(parent)
{
}

InventoryItem::InventoryItem(const QString& name, QObject* parent)
    : QObject(parent)
    , m_name(name)
{
}

void InventoryItem::setName(const QString& name) {
    if (m_name != name) {
        m_name = name;
        emit nameChanged();
    }
}

void InventoryItem::setQuantity(double qty) {
    if (m_quantity != qty) {
        m_quantity = qty;
        emit quantityChanged();
    }
}

void InventoryItem::adjustQuantity(double delta) {
    setQuantity(m_quantity + delta);
}

void InventoryItem::setReorderLevel(double level) {
    if (m_reorderLevel != level) {
        m_reorderLevel = level;
        emit reorderLevelChanged();
    }
}

QJsonObject InventoryItem::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["description"] = m_description;
    json["sku"] = m_sku;
    json["barcode"] = m_barcode;
    json["category"] = m_category;
    json["vendor"] = m_vendor;
    json["unit"] = static_cast<int>(m_unit);
    json["quantity"] = m_quantity;
    json["reorderLevel"] = m_reorderLevel;
    json["parLevel"] = m_parLevel;
    json["costPerUnit"] = m_costPerUnit;
    json["lastReceived"] = m_lastReceived.toString(Qt::ISODate);
    json["lastCounted"] = m_lastCounted.toString(Qt::ISODate);
    json["active"] = m_active;
    return json;
}

InventoryItem* InventoryItem::fromJson(const QJsonObject& json, QObject* parent) {
    auto* item = new InventoryItem(parent);
    item->m_id = json["id"].toInt();
    item->m_name = json["name"].toString();
    item->m_description = json["description"].toString();
    item->m_sku = json["sku"].toString();
    item->m_barcode = json["barcode"].toString();
    item->m_category = json["category"].toString();
    item->m_vendor = json["vendor"].toString();
    item->m_unit = static_cast<UnitType>(json["unit"].toInt());
    item->m_quantity = json["quantity"].toDouble();
    item->m_reorderLevel = json["reorderLevel"].toDouble();
    item->m_parLevel = json["parLevel"].toDouble();
    item->m_costPerUnit = json["costPerUnit"].toInt();
    item->m_lastReceived = QDateTime::fromString(json["lastReceived"].toString(), Qt::ISODate);
    item->m_lastCounted = QDateTime::fromString(json["lastCounted"].toString(), Qt::ISODate);
    item->m_active = json["active"].toBool(true);
    return item;
}

//=============================================================================
// InventoryTransaction Implementation
//=============================================================================

InventoryTransaction::InventoryTransaction(QObject* parent)
    : QObject(parent)
    , m_timestamp(QDateTime::currentDateTime())
{
}

QJsonObject InventoryTransaction::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["itemId"] = m_itemId;
    json["type"] = static_cast<int>(m_type);
    json["quantity"] = m_quantity;
    json["previousQuantity"] = m_previousQuantity;
    json["employeeId"] = m_employeeId;
    json["timestamp"] = m_timestamp.toString(Qt::ISODate);
    json["notes"] = m_notes;
    json["cost"] = m_cost;
    return json;
}

InventoryTransaction* InventoryTransaction::fromJson(const QJsonObject& json, QObject* parent) {
    auto* tx = new InventoryTransaction(parent);
    tx->m_id = json["id"].toInt();
    tx->m_itemId = json["itemId"].toInt();
    tx->m_type = static_cast<TransactionType>(json["type"].toInt());
    tx->m_quantity = json["quantity"].toDouble();
    tx->m_previousQuantity = json["previousQuantity"].toDouble();
    tx->m_employeeId = json["employeeId"].toInt();
    tx->m_timestamp = QDateTime::fromString(json["timestamp"].toString(), Qt::ISODate);
    tx->m_notes = json["notes"].toString();
    tx->m_cost = json["cost"].toInt();
    return tx;
}

//=============================================================================
// InventoryManager Implementation
//=============================================================================

InventoryManager* InventoryManager::s_instance = nullptr;

InventoryManager::InventoryManager(QObject* parent)
    : QObject(parent)
{
}

InventoryManager* InventoryManager::instance() {
    if (!s_instance) {
        s_instance = new InventoryManager();
    }
    return s_instance;
}

InventoryItem* InventoryManager::createItem(const QString& name) {
    auto* item = new InventoryItem(name, this);
    item->setId(m_nextItemId++);
    m_items.append(item);
    emit itemCreated(item);
    emit inventoryChanged();
    return item;
}

InventoryItem* InventoryManager::findById(int id) {
    for (auto* item : m_items) {
        if (item->id() == id) return item;
    }
    return nullptr;
}

InventoryItem* InventoryManager::findBySku(const QString& sku) {
    for (auto* item : m_items) {
        if (item->sku() == sku) return item;
    }
    return nullptr;
}

InventoryItem* InventoryManager::findByBarcode(const QString& barcode) {
    for (auto* item : m_items) {
        if (item->barcode() == barcode) return item;
    }
    return nullptr;
}

QList<InventoryItem*> InventoryManager::searchByName(const QString& name) {
    QList<InventoryItem*> results;
    QString lower = name.toLower();
    for (auto* item : m_items) {
        if (item->name().toLower().contains(lower)) {
            results.append(item);
        }
    }
    return results;
}

QList<InventoryItem*> InventoryManager::activeItems() const {
    QList<InventoryItem*> results;
    for (auto* item : m_items) {
        if (item->isActive()) results.append(item);
    }
    return results;
}

QList<InventoryItem*> InventoryManager::lowStockItems() const {
    QList<InventoryItem*> results;
    for (auto* item : m_items) {
        if (item->isActive() && item->needsReorder()) {
            results.append(item);
        }
    }
    return results;
}

QList<InventoryItem*> InventoryManager::itemsByCategory(const QString& category) const {
    QList<InventoryItem*> results;
    for (auto* item : m_items) {
        if (item->category() == category) results.append(item);
    }
    return results;
}

void InventoryManager::deleteItem(InventoryItem* item) {
    if (item && m_items.removeOne(item)) {
        emit itemDeleted(item);
        emit inventoryChanged();
        delete item;
    }
}

void InventoryManager::recordTransaction(InventoryItem* item, InventoryTransaction::TransactionType type,
                                          double quantity, int employeeId, const QString& notes, int cost) {
    auto* tx = new InventoryTransaction(this);
    tx->setId(m_nextTransactionId++);
    tx->setItemId(item->id());
    tx->setType(type);
    tx->setQuantity(quantity);
    tx->setPreviousQuantity(item->quantity());
    tx->setEmployeeId(employeeId);
    tx->setNotes(notes);
    tx->setCost(cost);
    
    m_transactions.append(tx);
    emit transactionRecorded(tx);
}

void InventoryManager::receiveInventory(InventoryItem* item, double quantity, int cost, 
                                         int employeeId, const QString& notes) {
    if (!item) return;
    
    recordTransaction(item, InventoryTransaction::TransactionType::Received, 
                      quantity, employeeId, notes, cost);
    
    item->adjustQuantity(quantity);
    item->setLastReceived(QDateTime::currentDateTime());
    
    emit itemUpdated(item);
    emit inventoryChanged();
}

void InventoryManager::useInventory(InventoryItem* item, double quantity, 
                                     int employeeId, const QString& notes) {
    if (!item) return;
    
    recordTransaction(item, InventoryTransaction::TransactionType::Used,
                      quantity, employeeId, notes);
    
    item->adjustQuantity(-quantity);
    
    if (item->needsReorder()) {
        emit lowStockAlert(item);
    }
    
    emit itemUpdated(item);
    emit inventoryChanged();
}

void InventoryManager::wasteInventory(InventoryItem* item, double quantity,
                                       int employeeId, const QString& notes) {
    if (!item) return;
    
    int wasteCost = static_cast<int>(quantity * item->costPerUnit());
    recordTransaction(item, InventoryTransaction::TransactionType::Wasted,
                      quantity, employeeId, notes, wasteCost);
    
    item->adjustQuantity(-quantity);
    
    if (item->needsReorder()) {
        emit lowStockAlert(item);
    }
    
    emit itemUpdated(item);
    emit inventoryChanged();
}

void InventoryManager::countInventory(InventoryItem* item, double newQuantity,
                                       int employeeId, const QString& notes) {
    if (!item) return;
    
    double diff = newQuantity - item->quantity();
    recordTransaction(item, InventoryTransaction::TransactionType::Counted,
                      diff, employeeId, notes);
    
    item->setQuantity(newQuantity);
    item->setLastCounted(QDateTime::currentDateTime());
    
    if (item->needsReorder()) {
        emit lowStockAlert(item);
    }
    
    emit itemUpdated(item);
    emit inventoryChanged();
}

QList<InventoryTransaction*> InventoryManager::transactionsForItem(int itemId) const {
    QList<InventoryTransaction*> results;
    for (auto* tx : m_transactions) {
        if (tx->itemId() == itemId) results.append(tx);
    }
    return results;
}

QList<InventoryTransaction*> InventoryManager::transactionsForPeriod(const QDate& start, const QDate& end) const {
    QList<InventoryTransaction*> results;
    for (auto* tx : m_transactions) {
        QDate d = tx->timestamp().date();
        if (d >= start && d <= end) {
            results.append(tx);
        }
    }
    return results;
}

QStringList InventoryManager::categories() const {
    QSet<QString> cats;
    for (const auto* item : m_items) {
        if (!item->category().isEmpty()) {
            cats.insert(item->category());
        }
    }
    return cats.values();
}

int InventoryManager::totalInventoryValue() const {
    int total = 0;
    for (const auto* item : m_items) {
        if (item->isActive()) {
            total += item->totalValue();
        }
    }
    return total;
}

bool InventoryManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextItemId"] = m_nextItemId;
    root["nextTransactionId"] = m_nextTransactionId;
    
    QJsonArray itemArray;
    for (const auto* item : m_items) {
        itemArray.append(item->toJson());
    }
    root["items"] = itemArray;
    
    QJsonArray txArray;
    for (const auto* tx : m_transactions) {
        txArray.append(tx->toJson());
    }
    root["transactions"] = txArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool InventoryManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextItemId = root["nextItemId"].toInt(1);
    m_nextTransactionId = root["nextTransactionId"].toInt(1);
    
    qDeleteAll(m_items);
    m_items.clear();
    
    QJsonArray itemArray = root["items"].toArray();
    for (const auto& ref : itemArray) {
        auto* item = InventoryItem::fromJson(ref.toObject(), this);
        m_items.append(item);
    }
    
    qDeleteAll(m_transactions);
    m_transactions.clear();
    
    QJsonArray txArray = root["transactions"].toArray();
    for (const auto& ref : txArray) {
        auto* tx = InventoryTransaction::fromJson(ref.toObject(), this);
        m_transactions.append(tx);
    }
    
    emit inventoryChanged();
    return true;
}

} // namespace vt2
