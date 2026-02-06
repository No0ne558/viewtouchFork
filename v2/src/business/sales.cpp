// ViewTouch V2 - Sales Tracking Implementation
// Modern C++/Qt6 reimplementation

#include "sales.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <algorithm>

namespace vt2 {

//=============================================================================
// SalesRecord Implementation
//=============================================================================

SalesRecord::SalesRecord(QObject* parent)
    : QObject(parent)
    , m_timestamp(QDateTime::currentDateTime())
{
}

QJsonObject SalesRecord::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["timestamp"] = m_timestamp.toString(Qt::ISODate);
    json["checkNumber"] = m_checkNumber;
    json["employeeId"] = m_employeeId;
    json["tableNumber"] = m_tableNumber;
    json["guestCount"] = m_guestCount;
    json["grossSales"] = m_grossSales;
    json["netSales"] = m_netSales;
    json["discounts"] = m_discounts;
    json["comps"] = m_comps;
    json["tax"] = m_tax;
    json["tips"] = m_tips;
    json["cashPayment"] = m_cashPayment;
    json["creditPayment"] = m_creditPayment;
    json["otherPayment"] = m_otherPayment;
    return json;
}

SalesRecord* SalesRecord::fromJson(const QJsonObject& json, QObject* parent) {
    auto* record = new SalesRecord(parent);
    record->m_id = json["id"].toInt();
    record->m_timestamp = QDateTime::fromString(json["timestamp"].toString(), Qt::ISODate);
    record->m_checkNumber = json["checkNumber"].toInt();
    record->m_employeeId = json["employeeId"].toInt();
    record->m_tableNumber = json["tableNumber"].toInt();
    record->m_guestCount = json["guestCount"].toInt();
    record->m_grossSales = json["grossSales"].toInt();
    record->m_netSales = json["netSales"].toInt();
    record->m_discounts = json["discounts"].toInt();
    record->m_comps = json["comps"].toInt();
    record->m_tax = json["tax"].toInt();
    record->m_tips = json["tips"].toInt();
    record->m_cashPayment = json["cashPayment"].toInt();
    record->m_creditPayment = json["creditPayment"].toInt();
    record->m_otherPayment = json["otherPayment"].toInt();
    return record;
}

//=============================================================================
// ItemSalesRecord Implementation
//=============================================================================

ItemSalesRecord::ItemSalesRecord(QObject* parent)
    : QObject(parent)
{
}

QJsonObject ItemSalesRecord::toJson() const {
    QJsonObject json;
    json["itemId"] = m_itemId;
    json["itemName"] = m_itemName;
    json["family"] = m_family;
    json["salesType"] = m_salesType;
    json["quantitySold"] = m_quantitySold;
    json["totalSales"] = m_totalSales;
    json["totalCost"] = m_totalCost;
    return json;
}

ItemSalesRecord* ItemSalesRecord::fromJson(const QJsonObject& json, QObject* parent) {
    auto* record = new ItemSalesRecord(parent);
    record->m_itemId = json["itemId"].toInt();
    record->m_itemName = json["itemName"].toString();
    record->m_family = json["family"].toInt();
    record->m_salesType = json["salesType"].toInt();
    record->m_quantitySold = json["quantitySold"].toInt();
    record->m_totalSales = json["totalSales"].toInt();
    record->m_totalCost = json["totalCost"].toInt();
    return record;
}

//=============================================================================
// DailySummary Implementation
//=============================================================================

DailySummary::DailySummary(QObject* parent)
    : QObject(parent)
    , m_date(QDate::currentDate())
{
}

DailySummary::DailySummary(const QDate& date, QObject* parent)
    : QObject(parent)
    , m_date(date)
{
}

QJsonObject DailySummary::toJson() const {
    QJsonObject json;
    json["date"] = m_date.toString(Qt::ISODate);
    json["checkCount"] = m_checkCount;
    json["guestCount"] = m_guestCount;
    json["voidCount"] = m_voidCount;
    json["grossSales"] = m_grossSales;
    json["netSales"] = m_netSales;
    json["foodSales"] = m_foodSales;
    json["beverageSales"] = m_beverageSales;
    json["alcoholSales"] = m_alcoholSales;
    json["merchandiseSales"] = m_merchandiseSales;
    json["discounts"] = m_discounts;
    json["comps"] = m_comps;
    json["coupons"] = m_coupons;
    json["totalTax"] = m_totalTax;
    json["totalTips"] = m_totalTips;
    json["cashTotal"] = m_cashTotal;
    json["creditTotal"] = m_creditTotal;
    json["debitTotal"] = m_debitTotal;
    json["giftTotal"] = m_giftTotal;
    return json;
}

DailySummary* DailySummary::fromJson(const QJsonObject& json, QObject* parent) {
    auto* summary = new DailySummary(parent);
    summary->m_date = QDate::fromString(json["date"].toString(), Qt::ISODate);
    summary->m_checkCount = json["checkCount"].toInt();
    summary->m_guestCount = json["guestCount"].toInt();
    summary->m_voidCount = json["voidCount"].toInt();
    summary->m_grossSales = json["grossSales"].toInt();
    summary->m_netSales = json["netSales"].toInt();
    summary->m_foodSales = json["foodSales"].toInt();
    summary->m_beverageSales = json["beverageSales"].toInt();
    summary->m_alcoholSales = json["alcoholSales"].toInt();
    summary->m_merchandiseSales = json["merchandiseSales"].toInt();
    summary->m_discounts = json["discounts"].toInt();
    summary->m_comps = json["comps"].toInt();
    summary->m_coupons = json["coupons"].toInt();
    summary->m_totalTax = json["totalTax"].toInt();
    summary->m_totalTips = json["totalTips"].toInt();
    summary->m_cashTotal = json["cashTotal"].toInt();
    summary->m_creditTotal = json["creditTotal"].toInt();
    summary->m_debitTotal = json["debitTotal"].toInt();
    summary->m_giftTotal = json["giftTotal"].toInt();
    return summary;
}

//=============================================================================
// SalesManager Implementation
//=============================================================================

SalesManager* SalesManager::s_instance = nullptr;

SalesManager::SalesManager(QObject* parent)
    : QObject(parent)
{
}

SalesManager* SalesManager::instance() {
    if (!s_instance) {
        s_instance = new SalesManager();
    }
    return s_instance;
}

void SalesManager::recordSale(int checkNumber, int employeeId, int tableNumber, int guestCount,
                               int grossSales, int netSales, int discounts, int comps, int tax, int tips,
                               int cashPayment, int creditPayment, int otherPayment) {
    auto* record = new SalesRecord(this);
    record->setId(m_nextRecordId++);
    record->setCheckNumber(checkNumber);
    record->setEmployeeId(employeeId);
    record->setTableNumber(tableNumber);
    record->setGuestCount(guestCount);
    record->setGrossSales(grossSales);
    record->setNetSales(netSales);
    record->setDiscounts(discounts);
    record->setComps(comps);
    record->setTax(tax);
    record->setTips(tips);
    record->setCashPayment(cashPayment);
    record->setCreditPayment(creditPayment);
    record->setOtherPayment(otherPayment);
    
    m_salesRecords.append(record);
    
    updateDailySummary(record->timestamp().date());
    
    emit saleRecorded(record);
}

void SalesManager::recordItemSale(int itemId, const QString& itemName, int family, int salesType,
                                   int quantity, int totalSales, int totalCost) {
    ItemSalesRecord* record = m_itemSales.value(itemId);
    
    if (!record) {
        record = new ItemSalesRecord(this);
        record->setItemId(itemId);
        record->setItemName(itemName);
        record->setFamily(family);
        record->setSalesType(salesType);
        m_itemSales[itemId] = record;
    }
    
    record->addQuantity(quantity);
    record->addSales(totalSales);
    record->addCost(totalCost);
    
    emit itemSaleRecorded(record);
}

QList<SalesRecord*> SalesManager::salesForDate(const QDate& date) const {
    QList<SalesRecord*> result;
    for (auto* record : m_salesRecords) {
        if (record->timestamp().date() == date) {
            result.append(record);
        }
    }
    return result;
}

QList<SalesRecord*> SalesManager::salesForPeriod(const QDate& start, const QDate& end) const {
    QList<SalesRecord*> result;
    for (auto* record : m_salesRecords) {
        QDate d = record->timestamp().date();
        if (d >= start && d <= end) {
            result.append(record);
        }
    }
    return result;
}

QList<SalesRecord*> SalesManager::salesByEmployee(int employeeId) const {
    QList<SalesRecord*> result;
    for (auto* record : m_salesRecords) {
        if (record->employeeId() == employeeId) {
            result.append(record);
        }
    }
    return result;
}

QList<ItemSalesRecord*> SalesManager::itemSalesForDate(const QDate& /*date*/) const {
    // For now, return all item sales (would need per-day tracking for accurate results)
    return m_itemSales.values();
}

QList<ItemSalesRecord*> SalesManager::topSellingItems(int count) const {
    QList<ItemSalesRecord*> items = m_itemSales.values();
    
    std::sort(items.begin(), items.end(), [](ItemSalesRecord* a, ItemSalesRecord* b) {
        return a->quantitySold() > b->quantitySold();
    });
    
    if (items.size() > count) {
        items = items.mid(0, count);
    }
    
    return items;
}

DailySummary* SalesManager::summaryForDate(const QDate& date) {
    if (!m_dailySummaries.contains(date)) {
        m_dailySummaries[date] = new DailySummary(date, this);
        updateDailySummary(date);
    }
    return m_dailySummaries[date];
}

QList<DailySummary*> SalesManager::summariesForPeriod(const QDate& start, const QDate& end) {
    QList<DailySummary*> result;
    QDate current = start;
    while (current <= end) {
        result.append(summaryForDate(current));
        current = current.addDays(1);
    }
    return result;
}

void SalesManager::updateDailySummary(const QDate& date) {
    DailySummary* summary = m_dailySummaries.value(date);
    if (!summary) {
        summary = new DailySummary(date, this);
        m_dailySummaries[date] = summary;
    }
    
    // Reset totals
    summary->setCheckCount(0);
    summary->setGuestCount(0);
    summary->setGrossSales(0);
    summary->setNetSales(0);
    summary->setDiscounts(0);
    summary->setComps(0);
    summary->setTotalTax(0);
    summary->setTotalTips(0);
    summary->setCashTotal(0);
    summary->setCreditTotal(0);
    
    // Sum up all records for this date
    for (const auto* record : m_salesRecords) {
        if (record->timestamp().date() == date) {
            summary->setCheckCount(summary->checkCount() + 1);
            summary->setGuestCount(summary->guestCount() + record->guestCount());
            summary->setGrossSales(summary->grossSales() + record->grossSales());
            summary->setNetSales(summary->netSales() + record->netSales());
            summary->setDiscounts(summary->discounts() + record->discounts());
            summary->setComps(summary->comps() + record->comps());
            summary->setTotalTax(summary->totalTax() + record->tax());
            summary->setTotalTips(summary->totalTips() + record->tips());
            summary->setCashTotal(summary->cashTotal() + record->cashPayment());
            summary->setCreditTotal(summary->creditTotal() + record->creditPayment());
        }
    }
    
    emit summaryUpdated(summary);
}

int SalesManager::totalSalesForPeriod(const QDate& start, const QDate& end) const {
    int total = 0;
    for (const auto* record : m_salesRecords) {
        QDate d = record->timestamp().date();
        if (d >= start && d <= end) {
            total += record->netSales();
        }
    }
    return total;
}

int SalesManager::totalChecksForPeriod(const QDate& start, const QDate& end) const {
    int count = 0;
    for (const auto* record : m_salesRecords) {
        QDate d = record->timestamp().date();
        if (d >= start && d <= end) {
            ++count;
        }
    }
    return count;
}

int SalesManager::totalGuestsForPeriod(const QDate& start, const QDate& end) const {
    int count = 0;
    for (const auto* record : m_salesRecords) {
        QDate d = record->timestamp().date();
        if (d >= start && d <= end) {
            count += record->guestCount();
        }
    }
    return count;
}

int SalesManager::todaySales() const {
    QDate today = QDate::currentDate();
    int total = 0;
    for (const auto* record : m_salesRecords) {
        if (record->timestamp().date() == today) {
            total += record->netSales();
        }
    }
    return total;
}

int SalesManager::todayCheckCount() const {
    QDate today = QDate::currentDate();
    int count = 0;
    for (const auto* record : m_salesRecords) {
        if (record->timestamp().date() == today) {
            ++count;
        }
    }
    return count;
}

int SalesManager::todayGuestCount() const {
    QDate today = QDate::currentDate();
    int count = 0;
    for (const auto* record : m_salesRecords) {
        if (record->timestamp().date() == today) {
            count += record->guestCount();
        }
    }
    return count;
}

void SalesManager::clearDataBefore(const QDate& date) {
    auto it = m_salesRecords.begin();
    while (it != m_salesRecords.end()) {
        if ((*it)->timestamp().date() < date) {
            delete *it;
            it = m_salesRecords.erase(it);
        } else {
            ++it;
        }
    }
    
    auto sit = m_dailySummaries.begin();
    while (sit != m_dailySummaries.end()) {
        if (sit.key() < date) {
            delete sit.value();
            sit = m_dailySummaries.erase(sit);
        } else {
            ++sit;
        }
    }
}

bool SalesManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextRecordId"] = m_nextRecordId;
    
    QJsonArray salesArray;
    for (const auto* record : m_salesRecords) {
        salesArray.append(record->toJson());
    }
    root["salesRecords"] = salesArray;
    
    QJsonArray itemArray;
    for (const auto* record : m_itemSales) {
        itemArray.append(record->toJson());
    }
    root["itemSales"] = itemArray;
    
    QJsonArray summaryArray;
    for (const auto* summary : m_dailySummaries) {
        summaryArray.append(summary->toJson());
    }
    root["dailySummaries"] = summaryArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool SalesManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextRecordId = root["nextRecordId"].toInt(1);
    
    qDeleteAll(m_salesRecords);
    m_salesRecords.clear();
    
    QJsonArray salesArray = root["salesRecords"].toArray();
    for (const auto& ref : salesArray) {
        auto* record = SalesRecord::fromJson(ref.toObject(), this);
        m_salesRecords.append(record);
    }
    
    qDeleteAll(m_itemSales);
    m_itemSales.clear();
    
    QJsonArray itemArray = root["itemSales"].toArray();
    for (const auto& ref : itemArray) {
        auto* record = ItemSalesRecord::fromJson(ref.toObject(), this);
        m_itemSales[record->itemId()] = record;
    }
    
    qDeleteAll(m_dailySummaries);
    m_dailySummaries.clear();
    
    QJsonArray summaryArray = root["dailySummaries"].toArray();
    for (const auto& ref : summaryArray) {
        auto* summary = DailySummary::fromJson(ref.toObject(), this);
        m_dailySummaries[summary->date()] = summary;
    }
    
    return true;
}

} // namespace vt2
