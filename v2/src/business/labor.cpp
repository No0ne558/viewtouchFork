// ViewTouch V2 - Labor Tracking Implementation
// Modern C++/Qt6 reimplementation

#include "labor.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

namespace vt2 {

//=============================================================================
// TimeEntry Implementation
//=============================================================================

TimeEntry::TimeEntry(QObject* parent)
    : QObject(parent)
{
}

int TimeEntry::duration() const {
    if (!m_clockIn.isValid()) return 0;
    
    QDateTime end = m_clockOut.isValid() ? m_clockOut : QDateTime::currentDateTime();
    int totalMins = m_clockIn.secsTo(end) / 60;
    return totalMins - m_breakMinutes;
}

int TimeEntry::earnedPay() const {
    if (m_payRate == 0) return 0;
    
    int mins = duration();
    double hours = mins / 60.0;
    
    if (m_isOvertime) {
        // 1.5x pay for overtime
        return static_cast<int>(hours * m_payRate * 1.5);
    }
    return static_cast<int>(hours * m_payRate);
}

QJsonObject TimeEntry::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["employeeId"] = m_employeeId;
    json["jobType"] = m_jobType;
    json["clockIn"] = m_clockIn.toString(Qt::ISODate);
    if (m_clockOut.isValid()) {
        json["clockOut"] = m_clockOut.toString(Qt::ISODate);
    }
    json["breakMinutes"] = m_breakMinutes;
    json["isOvertime"] = m_isOvertime;
    json["payRate"] = m_payRate;
    json["tipsEarned"] = m_tipsEarned;
    json["totalSales"] = m_totalSales;
    json["notes"] = m_notes;
    json["wasModified"] = m_wasModified;
    json["modifiedBy"] = m_modifiedBy;
    return json;
}

TimeEntry* TimeEntry::fromJson(const QJsonObject& json, QObject* parent) {
    auto* entry = new TimeEntry(parent);
    entry->m_id = json["id"].toInt();
    entry->m_employeeId = json["employeeId"].toInt();
    entry->m_jobType = json["jobType"].toInt();
    entry->m_clockIn = QDateTime::fromString(json["clockIn"].toString(), Qt::ISODate);
    if (json.contains("clockOut")) {
        entry->m_clockOut = QDateTime::fromString(json["clockOut"].toString(), Qt::ISODate);
    }
    entry->m_breakMinutes = json["breakMinutes"].toInt();
    entry->m_isOvertime = json["isOvertime"].toBool();
    entry->m_payRate = json["payRate"].toInt();
    entry->m_tipsEarned = json["tipsEarned"].toInt();
    entry->m_totalSales = json["totalSales"].toInt();
    entry->m_notes = json["notes"].toString();
    entry->m_wasModified = json["wasModified"].toBool();
    entry->m_modifiedBy = json["modifiedBy"].toInt();
    return entry;
}

//=============================================================================
// LaborSummary Implementation
//=============================================================================

LaborSummary::LaborSummary(QObject* parent)
    : QObject(parent)
{
}

QJsonObject LaborSummary::toJson() const {
    QJsonObject json;
    json["startDate"] = m_startDate.toString(Qt::ISODate);
    json["endDate"] = m_endDate.toString(Qt::ISODate);
    json["totalHours"] = m_totalHours;
    json["regularHours"] = m_regularHours;
    json["overtimeHours"] = m_overtimeHours;
    json["regularPay"] = m_regularPay;
    json["overtimePay"] = m_overtimePay;
    json["totalTips"] = m_totalTips;
    json["totalSales"] = m_totalSales;
    return json;
}

LaborSummary* LaborSummary::fromJson(const QJsonObject& json, QObject* parent) {
    auto* summary = new LaborSummary(parent);
    summary->m_startDate = QDate::fromString(json["startDate"].toString(), Qt::ISODate);
    summary->m_endDate = QDate::fromString(json["endDate"].toString(), Qt::ISODate);
    summary->m_totalHours = json["totalHours"].toDouble();
    summary->m_regularHours = json["regularHours"].toDouble();
    summary->m_overtimeHours = json["overtimeHours"].toDouble();
    summary->m_regularPay = json["regularPay"].toInt();
    summary->m_overtimePay = json["overtimePay"].toInt();
    summary->m_totalTips = json["totalTips"].toInt();
    summary->m_totalSales = json["totalSales"].toInt();
    return summary;
}

//=============================================================================
// LaborManager Implementation
//=============================================================================

LaborManager* LaborManager::s_instance = nullptr;

LaborManager::LaborManager(QObject* parent)
    : QObject(parent)
{
}

LaborManager* LaborManager::instance() {
    if (!s_instance) {
        s_instance = new LaborManager();
    }
    return s_instance;
}

TimeEntry* LaborManager::clockIn(int employeeId, int jobType, int payRate) {
    // Check if already clocked in
    if (isClockedIn(employeeId)) {
        return currentEntry(employeeId);
    }
    
    auto* entry = new TimeEntry(this);
    entry->setId(m_nextEntryId++);
    entry->setEmployeeId(employeeId);
    entry->setJobType(jobType);
    entry->setPayRate(payRate);
    entry->setClockIn(QDateTime::currentDateTime());
    
    m_entries.append(entry);
    
    emit employeeClockedIn(entry);
    emit laborChanged();
    
    return entry;
}

bool LaborManager::clockOut(int employeeId) {
    auto* entry = currentEntry(employeeId);
    if (!entry) return false;
    
    entry->setClockOut(QDateTime::currentDateTime());
    
    // Check for overtime (simple check - would need more complex logic for weekly overtime)
    // For now, just mark if shift is over 8 hours
    if (entry->duration() > 8 * 60) {
        entry->setOvertime(true);
    }
    
    emit employeeClockedOut(entry);
    emit laborChanged();
    
    return true;
}

bool LaborManager::startBreak(int employeeId) {
    // In a real implementation, we'd track break start time
    // For now, this is a placeholder
    return isClockedIn(employeeId);
}

bool LaborManager::endBreak(int employeeId) {
    auto* entry = currentEntry(employeeId);
    if (!entry) return false;
    
    // Add break time (would need actual tracking of break start)
    entry->addBreakMinutes(30);  // Default 30 min break
    
    emit laborChanged();
    return true;
}

TimeEntry* LaborManager::currentEntry(int employeeId) const {
    for (auto* entry : m_entries) {
        if (entry->employeeId() == employeeId && entry->isClockedIn()) {
            return entry;
        }
    }
    return nullptr;
}

bool LaborManager::isClockedIn(int employeeId) const {
    return currentEntry(employeeId) != nullptr;
}

QList<TimeEntry*> LaborManager::currentlyClockedIn() const {
    QList<TimeEntry*> result;
    for (auto* entry : m_entries) {
        if (entry->isClockedIn()) {
            result.append(entry);
        }
    }
    return result;
}

QList<TimeEntry*> LaborManager::entriesForEmployee(int employeeId) const {
    QList<TimeEntry*> result;
    for (auto* entry : m_entries) {
        if (entry->employeeId() == employeeId) {
            result.append(entry);
        }
    }
    return result;
}

QList<TimeEntry*> LaborManager::entriesForDate(const QDate& date) const {
    QList<TimeEntry*> result;
    for (auto* entry : m_entries) {
        if (entry->clockIn().date() == date) {
            result.append(entry);
        }
    }
    return result;
}

QList<TimeEntry*> LaborManager::entriesForPeriod(const QDate& start, const QDate& end) const {
    QList<TimeEntry*> result;
    for (auto* entry : m_entries) {
        QDate d = entry->clockIn().date();
        if (d >= start && d <= end) {
            result.append(entry);
        }
    }
    return result;
}

TimeEntry* LaborManager::findEntry(int id) {
    for (auto* entry : m_entries) {
        if (entry->id() == id) return entry;
    }
    return nullptr;
}

void LaborManager::editEntry(TimeEntry* entry, int modifiedBy) {
    if (entry) {
        entry->setModified(true);
        entry->setModifiedBy(modifiedBy);
        emit entryModified(entry);
        emit laborChanged();
    }
}

void LaborManager::deleteEntry(TimeEntry* entry) {
    if (entry && m_entries.removeOne(entry)) {
        delete entry;
        emit laborChanged();
    }
}

LaborSummary* LaborManager::summaryForEmployee(int employeeId, const QDate& start, const QDate& end) {
    auto* summary = new LaborSummary();
    summary->setStartDate(start);
    summary->setEndDate(end);
    
    double totalMins = 0;
    double regularMins = 0;
    double overtimeMins = 0;
    int regularPay = 0;
    int overtimePay = 0;
    int tips = 0;
    int sales = 0;
    
    auto entries = entriesForEmployee(employeeId);
    for (const auto* entry : entries) {
        QDate d = entry->clockIn().date();
        if (d >= start && d <= end) {
            int mins = entry->duration();
            totalMins += mins;
            
            if (entry->isOvertime()) {
                overtimeMins += mins;
                overtimePay += entry->earnedPay();
            } else {
                regularMins += mins;
                regularPay += entry->earnedPay();
            }
            
            tips += entry->tipsEarned();
            sales += entry->totalSales();
        }
    }
    
    summary->setTotalHours(totalMins / 60.0);
    summary->setRegularHours(regularMins / 60.0);
    summary->setOvertimeHours(overtimeMins / 60.0);
    summary->setRegularPay(regularPay);
    summary->setOvertimePay(overtimePay);
    summary->setTotalTips(tips);
    summary->setTotalSales(sales);
    
    return summary;
}

LaborSummary* LaborManager::summaryForPeriod(const QDate& start, const QDate& end) {
    auto* summary = new LaborSummary();
    summary->setStartDate(start);
    summary->setEndDate(end);
    
    double totalMins = 0;
    double regularMins = 0;
    double overtimeMins = 0;
    int regularPay = 0;
    int overtimePay = 0;
    int tips = 0;
    int sales = 0;
    
    auto entries = entriesForPeriod(start, end);
    for (const auto* entry : entries) {
        int mins = entry->duration();
        totalMins += mins;
        
        if (entry->isOvertime()) {
            overtimeMins += mins;
            overtimePay += entry->earnedPay();
        } else {
            regularMins += mins;
            regularPay += entry->earnedPay();
        }
        
        tips += entry->tipsEarned();
        sales += entry->totalSales();
    }
    
    summary->setTotalHours(totalMins / 60.0);
    summary->setRegularHours(regularMins / 60.0);
    summary->setOvertimeHours(overtimeMins / 60.0);
    summary->setRegularPay(regularPay);
    summary->setOvertimePay(overtimePay);
    summary->setTotalTips(tips);
    summary->setTotalSales(sales);
    
    return summary;
}

double LaborManager::totalHoursForDate(const QDate& date) const {
    double mins = 0;
    auto entries = entriesForDate(date);
    for (const auto* entry : entries) {
        mins += entry->duration();
    }
    return mins / 60.0;
}

int LaborManager::totalLaborCostForDate(const QDate& date) const {
    int cost = 0;
    auto entries = entriesForDate(date);
    for (const auto* entry : entries) {
        cost += entry->earnedPay();
    }
    return cost;
}

bool LaborManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextEntryId"] = m_nextEntryId;
    root["overtimeThreshold"] = m_overtimeThreshold;
    root["overtimeMultiplier"] = m_overtimeMultiplier;
    
    QJsonArray entryArray;
    for (const auto* entry : m_entries) {
        entryArray.append(entry->toJson());
    }
    root["entries"] = entryArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool LaborManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextEntryId = root["nextEntryId"].toInt(1);
    m_overtimeThreshold = root["overtimeThreshold"].toInt(40 * 60);
    m_overtimeMultiplier = root["overtimeMultiplier"].toDouble(1.5);
    
    qDeleteAll(m_entries);
    m_entries.clear();
    
    QJsonArray entryArray = root["entries"].toArray();
    for (const auto& ref : entryArray) {
        auto* entry = TimeEntry::fromJson(ref.toObject(), this);
        m_entries.append(entry);
    }
    
    emit laborChanged();
    return true;
}

} // namespace vt2
