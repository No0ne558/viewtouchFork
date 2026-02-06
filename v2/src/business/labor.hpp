// ViewTouch V2 - Labor Tracking System
// Modern C++/Qt6 reimplementation

#ifndef VT2_LABOR_HPP
#define VT2_LABOR_HPP

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QJsonObject>

namespace vt2 {

//=============================================================================
// TimeEntry - A single time clock entry
//=============================================================================
class TimeEntry : public QObject {
    Q_OBJECT

public:
    explicit TimeEntry(QObject* parent = nullptr);
    ~TimeEntry() override = default;

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    int employeeId() const { return m_employeeId; }
    void setEmployeeId(int id) { m_employeeId = id; }
    
    int jobType() const { return m_jobType; }
    void setJobType(int type) { m_jobType = type; }
    
    QDateTime clockIn() const { return m_clockIn; }
    void setClockIn(const QDateTime& dt) { m_clockIn = dt; }
    
    QDateTime clockOut() const { return m_clockOut; }
    void setClockOut(const QDateTime& dt) { m_clockOut = dt; }
    
    bool isClockedIn() const { return m_clockIn.isValid() && !m_clockOut.isValid(); }
    
    // Duration in minutes
    int duration() const;
    
    // Hours worked (for display)
    double hoursWorked() const { return duration() / 60.0; }
    
    // Break tracking
    int breakMinutes() const { return m_breakMinutes; }
    void setBreakMinutes(int mins) { m_breakMinutes = mins; }
    void addBreakMinutes(int mins) { m_breakMinutes += mins; }
    
    // Overtime
    bool isOvertime() const { return m_isOvertime; }
    void setOvertime(bool ot) { m_isOvertime = ot; }
    
    // Pay
    int payRate() const { return m_payRate; }  // Cents per hour
    void setPayRate(int rate) { m_payRate = rate; }
    
    int earnedPay() const;  // Total pay for this entry
    
    // Tips
    int tipsEarned() const { return m_tipsEarned; }
    void setTipsEarned(int tips) { m_tipsEarned = tips; }
    void addTips(int tips) { m_tipsEarned += tips; }
    
    // Sales (for servers)
    int totalSales() const { return m_totalSales; }
    void setTotalSales(int sales) { m_totalSales = sales; }
    void addSales(int sales) { m_totalSales += sales; }
    
    // Notes
    QString notes() const { return m_notes; }
    void setNotes(const QString& notes) { m_notes = notes; }
    
    // Modified flag
    bool wasModified() const { return m_wasModified; }
    void setModified(bool mod) { m_wasModified = mod; }
    
    int modifiedBy() const { return m_modifiedBy; }
    void setModifiedBy(int empId) { m_modifiedBy = empId; }
    
    // Serialization
    QJsonObject toJson() const;
    static TimeEntry* fromJson(const QJsonObject& json, QObject* parent = nullptr);

private:
    int m_id = 0;
    int m_employeeId = 0;
    int m_jobType = 0;
    
    QDateTime m_clockIn;
    QDateTime m_clockOut;
    
    int m_breakMinutes = 0;
    bool m_isOvertime = false;
    
    int m_payRate = 0;
    int m_tipsEarned = 0;
    int m_totalSales = 0;
    
    QString m_notes;
    
    bool m_wasModified = false;
    int m_modifiedBy = 0;
};

//=============================================================================
// LaborSummary - Summary of labor for a period
//=============================================================================
class LaborSummary : public QObject {
    Q_OBJECT

public:
    explicit LaborSummary(QObject* parent = nullptr);
    ~LaborSummary() override = default;

    QDate startDate() const { return m_startDate; }
    void setStartDate(const QDate& date) { m_startDate = date; }
    
    QDate endDate() const { return m_endDate; }
    void setEndDate(const QDate& date) { m_endDate = date; }
    
    // Hours
    double totalHours() const { return m_totalHours; }
    void setTotalHours(double hours) { m_totalHours = hours; }
    
    double regularHours() const { return m_regularHours; }
    void setRegularHours(double hours) { m_regularHours = hours; }
    
    double overtimeHours() const { return m_overtimeHours; }
    void setOvertimeHours(double hours) { m_overtimeHours = hours; }
    
    // Pay (in cents)
    int regularPay() const { return m_regularPay; }
    void setRegularPay(int pay) { m_regularPay = pay; }
    
    int overtimePay() const { return m_overtimePay; }
    void setOvertimePay(int pay) { m_overtimePay = pay; }
    
    int totalPay() const { return m_regularPay + m_overtimePay; }
    
    // Tips
    int totalTips() const { return m_totalTips; }
    void setTotalTips(int tips) { m_totalTips = tips; }
    
    // Labor cost metrics
    int totalSales() const { return m_totalSales; }
    void setTotalSales(int sales) { m_totalSales = sales; }
    
    double laborCostPercent() const {
        return m_totalSales > 0 ? (totalPay() * 100.0) / m_totalSales : 0;
    }
    
    // Serialization
    QJsonObject toJson() const;
    static LaborSummary* fromJson(const QJsonObject& json, QObject* parent = nullptr);

private:
    QDate m_startDate;
    QDate m_endDate;
    
    double m_totalHours = 0.0;
    double m_regularHours = 0.0;
    double m_overtimeHours = 0.0;
    
    int m_regularPay = 0;
    int m_overtimePay = 0;
    int m_totalTips = 0;
    int m_totalSales = 0;
};

//=============================================================================
// LaborManager - Manages time clock and labor tracking
//=============================================================================
class LaborManager : public QObject {
    Q_OBJECT

public:
    static LaborManager* instance();
    
    // Time clock operations
    TimeEntry* clockIn(int employeeId, int jobType, int payRate);
    bool clockOut(int employeeId);
    bool startBreak(int employeeId);
    bool endBreak(int employeeId);
    
    // Current status
    TimeEntry* currentEntry(int employeeId) const;
    bool isClockedIn(int employeeId) const;
    QList<TimeEntry*> currentlyClockedIn() const;
    
    // Time entry queries
    QList<TimeEntry*> entriesForEmployee(int employeeId) const;
    QList<TimeEntry*> entriesForDate(const QDate& date) const;
    QList<TimeEntry*> entriesForPeriod(const QDate& start, const QDate& end) const;
    QList<TimeEntry*> allEntries() const { return m_entries; }
    
    // Time entry management
    TimeEntry* findEntry(int id);
    void editEntry(TimeEntry* entry, int modifiedBy);
    void deleteEntry(TimeEntry* entry);
    
    // Summaries
    LaborSummary* summaryForEmployee(int employeeId, const QDate& start, const QDate& end);
    LaborSummary* summaryForPeriod(const QDate& start, const QDate& end);
    
    // Stats
    double totalHoursForDate(const QDate& date) const;
    int totalLaborCostForDate(const QDate& date) const;
    int employeesOnClock() const { return currentlyClockedIn().size(); }
    
    // Overtime threshold (default 40 hours/week)
    int overtimeThreshold() const { return m_overtimeThreshold; }
    void setOvertimeThreshold(int minutes) { m_overtimeThreshold = minutes; }
    
    double overtimeMultiplier() const { return m_overtimeMultiplier; }
    void setOvertimeMultiplier(double mult) { m_overtimeMultiplier = mult; }
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

signals:
    void employeeClockedIn(TimeEntry* entry);
    void employeeClockedOut(TimeEntry* entry);
    void entryModified(TimeEntry* entry);
    void laborChanged();

private:
    explicit LaborManager(QObject* parent = nullptr);
    static LaborManager* s_instance;
    
    QList<TimeEntry*> m_entries;
    int m_nextEntryId = 1;
    
    int m_overtimeThreshold = 40 * 60;  // 40 hours in minutes
    double m_overtimeMultiplier = 1.5;
};

} // namespace vt2

#endif // VT2_LABOR_HPP
