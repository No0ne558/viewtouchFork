// ViewTouch V2 - Tips Management System
// Modern C++/Qt6 reimplementation

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QDate>
#include <QDateTime>
#include <QJsonObject>

namespace vt2 {

//=============================================================================
// Tip Entry - Individual tip record
//=============================================================================
class TipEntry : public QObject {
    Q_OBJECT
    Q_PROPERTY(int id READ id WRITE setId)
    Q_PROPERTY(int checkId READ checkId WRITE setCheckId)
    Q_PROPERTY(int employeeId READ employeeId WRITE setEmployeeId)
    Q_PROPERTY(int amount READ amount WRITE setAmount)
    Q_PROPERTY(TipType tipType READ tipType WRITE setTipType)
    
public:
    enum class TipType {
        Cash,
        CreditCard,
        Automatic,
        Manual
    };
    Q_ENUM(TipType)
    
    explicit TipEntry(QObject* parent = nullptr);
    
    // Accessors
    int id() const { return m_id; }
    int checkId() const { return m_checkId; }
    int employeeId() const { return m_employeeId; }
    int amount() const { return m_amount; }  // In cents
    TipType tipType() const { return m_tipType; }
    QDateTime timestamp() const { return m_timestamp; }
    bool isPooled() const { return m_isPooled; }
    QString note() const { return m_note; }
    
    // Mutators
    void setId(int id) { m_id = id; }
    void setCheckId(int id) { m_checkId = id; }
    void setEmployeeId(int id) { m_employeeId = id; }
    void setAmount(int cents) { m_amount = cents; }
    void setTipType(TipType type) { m_tipType = type; }
    void setTimestamp(const QDateTime& dt) { m_timestamp = dt; }
    void setPooled(bool pooled) { m_isPooled = pooled; }
    void setNote(const QString& n) { m_note = n; }
    
    // Serialization
    QJsonObject toJson() const;
    static TipEntry* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    int m_checkId = 0;
    int m_employeeId = 0;
    int m_amount = 0;
    TipType m_tipType = TipType::Cash;
    QDateTime m_timestamp;
    bool m_isPooled = false;
    QString m_note;
};

//=============================================================================
// Tip Distribution - For tip pooling
//=============================================================================
class TipDistribution : public QObject {
    Q_OBJECT
    
public:
    explicit TipDistribution(QObject* parent = nullptr);
    
    // Accessors
    int id() const { return m_id; }
    QDate date() const { return m_date; }
    int totalPoolAmount() const { return m_totalPoolAmount; }
    QMap<int, int> employeeShares() const { return m_employeeShares; }
    QMap<int, double> employeePercentages() const { return m_employeePercentages; }
    bool isDistributed() const { return m_isDistributed; }
    
    // Mutators
    void setId(int id) { m_id = id; }
    void setDate(const QDate& d) { m_date = d; }
    void setTotalPoolAmount(int cents) { m_totalPoolAmount = cents; }
    void setEmployeeShare(int empId, int cents) { m_employeeShares[empId] = cents; }
    void setEmployeePercentage(int empId, double pct) { m_employeePercentages[empId] = pct; }
    void setDistributed(bool d) { m_isDistributed = d; }
    
    // Calculate share for employee based on percentage
    int calculateShare(int empId) const;
    
    // Serialization
    QJsonObject toJson() const;
    static TipDistribution* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    QDate m_date;
    int m_totalPoolAmount = 0;
    QMap<int, int> m_employeeShares;         // empId -> cents
    QMap<int, double> m_employeePercentages; // empId -> percentage
    bool m_isDistributed = false;
};

//=============================================================================
// Employee Tip Summary
//=============================================================================
class TipSummary : public QObject {
    Q_OBJECT
    
public:
    explicit TipSummary(QObject* parent = nullptr);
    
    int employeeId() const { return m_employeeId; }
    QDate startDate() const { return m_startDate; }
    QDate endDate() const { return m_endDate; }
    
    int cashTips() const { return m_cashTips; }
    int creditCardTips() const { return m_creditCardTips; }
    int pooledTips() const { return m_pooledTips; }
    int autoGratuity() const { return m_autoGratuity; }
    int totalTips() const { return m_cashTips + m_creditCardTips + m_pooledTips + m_autoGratuity; }
    
    int totalSales() const { return m_totalSales; }
    double tipPercentage() const { 
        return m_totalSales > 0 ? (totalTips() * 100.0 / m_totalSales) : 0.0; 
    }
    
    void setEmployeeId(int id) { m_employeeId = id; }
    void setStartDate(const QDate& d) { m_startDate = d; }
    void setEndDate(const QDate& d) { m_endDate = d; }
    void setCashTips(int c) { m_cashTips = c; }
    void setCreditCardTips(int c) { m_creditCardTips = c; }
    void setPooledTips(int c) { m_pooledTips = c; }
    void setAutoGratuity(int c) { m_autoGratuity = c; }
    void setTotalSales(int c) { m_totalSales = c; }
    
    QJsonObject toJson() const;
    static TipSummary* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_employeeId = 0;
    QDate m_startDate;
    QDate m_endDate;
    int m_cashTips = 0;
    int m_creditCardTips = 0;
    int m_pooledTips = 0;
    int m_autoGratuity = 0;
    int m_totalSales = 0;
};

//=============================================================================
// Tips Manager - Singleton
//=============================================================================
class TipsManager : public QObject {
    Q_OBJECT
    
public:
    static TipsManager* instance();
    
    // Tip entry management
    TipEntry* addTip(int checkId, int employeeId, int amount, TipEntry::TipType type);
    TipEntry* findTip(int id);
    void editTip(TipEntry* tip);
    void deleteTip(TipEntry* tip);
    
    // Query tips
    QList<TipEntry*> tipsForEmployee(int employeeId) const;
    QList<TipEntry*> tipsForDate(const QDate& date) const;
    QList<TipEntry*> tipsForPeriod(const QDate& start, const QDate& end) const;
    QList<TipEntry*> tipsForCheck(int checkId) const;
    
    // Totals
    int totalTipsForEmployee(int employeeId, const QDate& date) const;
    int totalTipsForDate(const QDate& date) const;
    
    // Tip pooling
    void enablePooling(bool enable) { m_poolingEnabled = enable; }
    bool isPoolingEnabled() const { return m_poolingEnabled; }
    void setPoolPercentage(double pct) { m_poolPercentage = pct; }
    double poolPercentage() const { return m_poolPercentage; }
    
    TipDistribution* createDistribution(const QDate& date);
    TipDistribution* findDistribution(int id);
    TipDistribution* distributionForDate(const QDate& date);
    void executeDistribution(TipDistribution* dist);
    
    // Auto gratuity settings
    void setAutoGratuityEnabled(bool enable) { m_autoGratuityEnabled = enable; }
    bool isAutoGratuityEnabled() const { return m_autoGratuityEnabled; }
    void setAutoGratuityPercent(double pct) { m_autoGratuityPercent = pct; }
    double autoGratuityPercent() const { return m_autoGratuityPercent; }
    void setAutoGratuityThreshold(int guests) { m_autoGratuityThreshold = guests; }
    int autoGratuityThreshold() const { return m_autoGratuityThreshold; }
    int calculateAutoGratuity(int subtotal) const;
    
    // Summaries
    TipSummary* summaryForEmployee(int employeeId, const QDate& start, const QDate& end);
    TipSummary* summaryForDate(const QDate& date);
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    
signals:
    void tipAdded(TipEntry* tip);
    void tipModified(TipEntry* tip);
    void tipRemoved(int tipId);
    void distributionCreated(TipDistribution* dist);
    void tipsChanged();
    
private:
    explicit TipsManager(QObject* parent = nullptr);
    static TipsManager* s_instance;
    
    QList<TipEntry*> m_tips;
    QList<TipDistribution*> m_distributions;
    int m_nextTipId = 1;
    int m_nextDistId = 1;
    
    // Pooling settings
    bool m_poolingEnabled = false;
    double m_poolPercentage = 0.0;  // Percentage of tips that go to pool
    
    // Auto gratuity settings
    bool m_autoGratuityEnabled = false;
    double m_autoGratuityPercent = 18.0;  // Default 18%
    int m_autoGratuityThreshold = 8;       // Guests needed to trigger
};

} // namespace vt2
