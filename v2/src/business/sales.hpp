// ViewTouch V2 - Sales Tracking System
// Modern C++/Qt6 reimplementation

#ifndef VT2_SALES_HPP
#define VT2_SALES_HPP

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QJsonObject>

namespace vt2 {

// Sales Period
enum class SalesPeriod {
    None = 0,
    Day = 1,
    Week = 2,
    TwoWeeks = 3,
    FourWeeks = 4,
    Month = 5,
    HalfMonth = 6,
    Quarter = 7,
    YearToDate = 8
};

//=============================================================================
// SalesRecord - A single sales record
//=============================================================================
class SalesRecord : public QObject {
    Q_OBJECT

public:
    explicit SalesRecord(QObject* parent = nullptr);
    ~SalesRecord() override = default;

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    QDateTime timestamp() const { return m_timestamp; }
    void setTimestamp(const QDateTime& ts) { m_timestamp = ts; }
    
    int checkNumber() const { return m_checkNumber; }
    void setCheckNumber(int num) { m_checkNumber = num; }
    
    int employeeId() const { return m_employeeId; }
    void setEmployeeId(int id) { m_employeeId = id; }
    
    int tableNumber() const { return m_tableNumber; }
    void setTableNumber(int num) { m_tableNumber = num; }
    
    int guestCount() const { return m_guestCount; }
    void setGuestCount(int count) { m_guestCount = count; }
    
    // Amounts (in cents)
    int grossSales() const { return m_grossSales; }
    void setGrossSales(int amount) { m_grossSales = amount; }
    
    int netSales() const { return m_netSales; }
    void setNetSales(int amount) { m_netSales = amount; }
    
    int discounts() const { return m_discounts; }
    void setDiscounts(int amount) { m_discounts = amount; }
    
    int comps() const { return m_comps; }
    void setComps(int amount) { m_comps = amount; }
    
    int tax() const { return m_tax; }
    void setTax(int amount) { m_tax = amount; }
    
    int tips() const { return m_tips; }
    void setTips(int amount) { m_tips = amount; }
    
    // Payment breakdown
    int cashPayment() const { return m_cashPayment; }
    void setCashPayment(int amount) { m_cashPayment = amount; }
    
    int creditPayment() const { return m_creditPayment; }
    void setCreditPayment(int amount) { m_creditPayment = amount; }
    
    int otherPayment() const { return m_otherPayment; }
    void setOtherPayment(int amount) { m_otherPayment = amount; }
    
    // Serialization
    QJsonObject toJson() const;
    static SalesRecord* fromJson(const QJsonObject& json, QObject* parent = nullptr);

private:
    int m_id = 0;
    QDateTime m_timestamp;
    int m_checkNumber = 0;
    int m_employeeId = 0;
    int m_tableNumber = 0;
    int m_guestCount = 0;
    
    int m_grossSales = 0;
    int m_netSales = 0;
    int m_discounts = 0;
    int m_comps = 0;
    int m_tax = 0;
    int m_tips = 0;
    
    int m_cashPayment = 0;
    int m_creditPayment = 0;
    int m_otherPayment = 0;
};

//=============================================================================
// ItemSalesRecord - Sales record for a single menu item
//=============================================================================
class ItemSalesRecord : public QObject {
    Q_OBJECT

public:
    explicit ItemSalesRecord(QObject* parent = nullptr);
    ~ItemSalesRecord() override = default;

    int itemId() const { return m_itemId; }
    void setItemId(int id) { m_itemId = id; }
    
    QString itemName() const { return m_itemName; }
    void setItemName(const QString& name) { m_itemName = name; }
    
    int family() const { return m_family; }
    void setFamily(int fam) { m_family = fam; }
    
    int salesType() const { return m_salesType; }
    void setSalesType(int type) { m_salesType = type; }
    
    int quantitySold() const { return m_quantitySold; }
    void setQuantitySold(int qty) { m_quantitySold = qty; }
    void addQuantity(int qty) { m_quantitySold += qty; }
    
    int totalSales() const { return m_totalSales; }
    void setTotalSales(int amount) { m_totalSales = amount; }
    void addSales(int amount) { m_totalSales += amount; }
    
    int totalCost() const { return m_totalCost; }
    void setTotalCost(int amount) { m_totalCost = amount; }
    void addCost(int amount) { m_totalCost += amount; }
    
    int profit() const { return m_totalSales - m_totalCost; }
    
    // Serialization
    QJsonObject toJson() const;
    static ItemSalesRecord* fromJson(const QJsonObject& json, QObject* parent = nullptr);

private:
    int m_itemId = 0;
    QString m_itemName;
    int m_family = 0;
    int m_salesType = 0;
    int m_quantitySold = 0;
    int m_totalSales = 0;
    int m_totalCost = 0;
};

//=============================================================================
// DailySummary - Summary of a day's sales
//=============================================================================
class DailySummary : public QObject {
    Q_OBJECT

public:
    explicit DailySummary(QObject* parent = nullptr);
    DailySummary(const QDate& date, QObject* parent = nullptr);
    ~DailySummary() override = default;

    QDate date() const { return m_date; }
    void setDate(const QDate& date) { m_date = date; }
    
    // Counts
    int checkCount() const { return m_checkCount; }
    void setCheckCount(int count) { m_checkCount = count; }
    
    int guestCount() const { return m_guestCount; }
    void setGuestCount(int count) { m_guestCount = count; }
    
    int voidCount() const { return m_voidCount; }
    void setVoidCount(int count) { m_voidCount = count; }
    
    // Sales (in cents)
    int grossSales() const { return m_grossSales; }
    void setGrossSales(int amount) { m_grossSales = amount; }
    
    int netSales() const { return m_netSales; }
    void setNetSales(int amount) { m_netSales = amount; }
    
    int foodSales() const { return m_foodSales; }
    void setFoodSales(int amount) { m_foodSales = amount; }
    
    int beverageSales() const { return m_beverageSales; }
    void setBeverageSales(int amount) { m_beverageSales = amount; }
    
    int alcoholSales() const { return m_alcoholSales; }
    void setAlcoholSales(int amount) { m_alcoholSales = amount; }
    
    int merchandiseSales() const { return m_merchandiseSales; }
    void setMerchandiseSales(int amount) { m_merchandiseSales = amount; }
    
    // Deductions
    int discounts() const { return m_discounts; }
    void setDiscounts(int amount) { m_discounts = amount; }
    
    int comps() const { return m_comps; }
    void setComps(int amount) { m_comps = amount; }
    
    int coupons() const { return m_coupons; }
    void setCoupons(int amount) { m_coupons = amount; }
    
    // Tax & Tips
    int totalTax() const { return m_totalTax; }
    void setTotalTax(int amount) { m_totalTax = amount; }
    
    int totalTips() const { return m_totalTips; }
    void setTotalTips(int amount) { m_totalTips = amount; }
    
    // Payments
    int cashTotal() const { return m_cashTotal; }
    void setCashTotal(int amount) { m_cashTotal = amount; }
    
    int creditTotal() const { return m_creditTotal; }
    void setCreditTotal(int amount) { m_creditTotal = amount; }
    
    int debitTotal() const { return m_debitTotal; }
    void setDebitTotal(int amount) { m_debitTotal = amount; }
    
    int giftTotal() const { return m_giftTotal; }
    void setGiftTotal(int amount) { m_giftTotal = amount; }
    
    // Averages
    double averageCheck() const { 
        return m_checkCount > 0 ? static_cast<double>(m_netSales) / m_checkCount : 0; 
    }
    double averageGuest() const { 
        return m_guestCount > 0 ? static_cast<double>(m_netSales) / m_guestCount : 0; 
    }
    
    // Serialization
    QJsonObject toJson() const;
    static DailySummary* fromJson(const QJsonObject& json, QObject* parent = nullptr);

private:
    QDate m_date;
    
    int m_checkCount = 0;
    int m_guestCount = 0;
    int m_voidCount = 0;
    
    int m_grossSales = 0;
    int m_netSales = 0;
    int m_foodSales = 0;
    int m_beverageSales = 0;
    int m_alcoholSales = 0;
    int m_merchandiseSales = 0;
    
    int m_discounts = 0;
    int m_comps = 0;
    int m_coupons = 0;
    
    int m_totalTax = 0;
    int m_totalTips = 0;
    
    int m_cashTotal = 0;
    int m_creditTotal = 0;
    int m_debitTotal = 0;
    int m_giftTotal = 0;
};

//=============================================================================
// SalesManager - Manages all sales data
//=============================================================================
class SalesManager : public QObject {
    Q_OBJECT

public:
    static SalesManager* instance();
    
    // Record a completed check
    void recordSale(int checkNumber, int employeeId, int tableNumber, int guestCount,
                    int grossSales, int netSales, int discounts, int comps, int tax, int tips,
                    int cashPayment, int creditPayment, int otherPayment);
    
    // Record item sales
    void recordItemSale(int itemId, const QString& itemName, int family, int salesType,
                        int quantity, int totalSales, int totalCost);
    
    // Get sales records
    QList<SalesRecord*> salesForDate(const QDate& date) const;
    QList<SalesRecord*> salesForPeriod(const QDate& start, const QDate& end) const;
    QList<SalesRecord*> salesByEmployee(int employeeId) const;
    
    // Get item sales
    QList<ItemSalesRecord*> itemSalesForDate(const QDate& date) const;
    QList<ItemSalesRecord*> topSellingItems(int count = 10) const;
    
    // Get summaries
    DailySummary* summaryForDate(const QDate& date);
    QList<DailySummary*> summariesForPeriod(const QDate& start, const QDate& end);
    
    // Totals for period
    int totalSalesForPeriod(const QDate& start, const QDate& end) const;
    int totalChecksForPeriod(const QDate& start, const QDate& end) const;
    int totalGuestsForPeriod(const QDate& start, const QDate& end) const;
    
    // Today's stats
    int todaySales() const;
    int todayCheckCount() const;
    int todayGuestCount() const;
    
    // Clear old data
    void clearDataBefore(const QDate& date);
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

signals:
    void saleRecorded(SalesRecord* record);
    void itemSaleRecorded(ItemSalesRecord* record);
    void summaryUpdated(DailySummary* summary);

private:
    explicit SalesManager(QObject* parent = nullptr);
    static SalesManager* s_instance;
    
    void updateDailySummary(const QDate& date);
    
    QList<SalesRecord*> m_salesRecords;
    QMap<int, ItemSalesRecord*> m_itemSales;  // Keyed by item ID
    QMap<QDate, DailySummary*> m_dailySummaries;
    
    int m_nextRecordId = 1;
};

} // namespace vt2

#endif // VT2_SALES_HPP
