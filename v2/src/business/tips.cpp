// ViewTouch V2 - Tips Management Implementation
// Modern C++/Qt6 reimplementation

#include "tips.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>

namespace vt2 {

//=============================================================================
// TipEntry Implementation
//=============================================================================

TipEntry::TipEntry(QObject* parent)
    : QObject(parent)
    , m_timestamp(QDateTime::currentDateTime())
{
}

QJsonObject TipEntry::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["checkId"] = m_checkId;
    json["employeeId"] = m_employeeId;
    json["amount"] = m_amount;
    json["tipType"] = static_cast<int>(m_tipType);
    json["timestamp"] = m_timestamp.toString(Qt::ISODate);
    json["isPooled"] = m_isPooled;
    json["note"] = m_note;
    return json;
}

TipEntry* TipEntry::fromJson(const QJsonObject& json, QObject* parent) {
    auto* tip = new TipEntry(parent);
    tip->m_id = json["id"].toInt();
    tip->m_checkId = json["checkId"].toInt();
    tip->m_employeeId = json["employeeId"].toInt();
    tip->m_amount = json["amount"].toInt();
    tip->m_tipType = static_cast<TipType>(json["tipType"].toInt());
    tip->m_timestamp = QDateTime::fromString(json["timestamp"].toString(), Qt::ISODate);
    tip->m_isPooled = json["isPooled"].toBool();
    tip->m_note = json["note"].toString();
    return tip;
}

//=============================================================================
// TipDistribution Implementation
//=============================================================================

TipDistribution::TipDistribution(QObject* parent)
    : QObject(parent)
{
}

int TipDistribution::calculateShare(int empId) const {
    if (m_employeePercentages.contains(empId)) {
        double pct = m_employeePercentages[empId];
        return static_cast<int>(m_totalPoolAmount * pct / 100.0);
    }
    return 0;
}

QJsonObject TipDistribution::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["date"] = m_date.toString(Qt::ISODate);
    json["totalPoolAmount"] = m_totalPoolAmount;
    json["isDistributed"] = m_isDistributed;
    
    // Shares
    QJsonObject sharesObj;
    for (auto it = m_employeeShares.constBegin(); it != m_employeeShares.constEnd(); ++it) {
        sharesObj[QString::number(it.key())] = it.value();
    }
    json["employeeShares"] = sharesObj;
    
    // Percentages
    QJsonObject pctObj;
    for (auto it = m_employeePercentages.constBegin(); it != m_employeePercentages.constEnd(); ++it) {
        pctObj[QString::number(it.key())] = it.value();
    }
    json["employeePercentages"] = pctObj;
    
    return json;
}

TipDistribution* TipDistribution::fromJson(const QJsonObject& json, QObject* parent) {
    auto* dist = new TipDistribution(parent);
    dist->m_id = json["id"].toInt();
    dist->m_date = QDate::fromString(json["date"].toString(), Qt::ISODate);
    dist->m_totalPoolAmount = json["totalPoolAmount"].toInt();
    dist->m_isDistributed = json["isDistributed"].toBool();
    
    QJsonObject sharesObj = json["employeeShares"].toObject();
    for (auto it = sharesObj.constBegin(); it != sharesObj.constEnd(); ++it) {
        dist->m_employeeShares[it.key().toInt()] = it.value().toInt();
    }
    
    QJsonObject pctObj = json["employeePercentages"].toObject();
    for (auto it = pctObj.constBegin(); it != pctObj.constEnd(); ++it) {
        dist->m_employeePercentages[it.key().toInt()] = it.value().toDouble();
    }
    
    return dist;
}

//=============================================================================
// TipSummary Implementation
//=============================================================================

TipSummary::TipSummary(QObject* parent)
    : QObject(parent)
{
}

QJsonObject TipSummary::toJson() const {
    QJsonObject json;
    json["employeeId"] = m_employeeId;
    json["startDate"] = m_startDate.toString(Qt::ISODate);
    json["endDate"] = m_endDate.toString(Qt::ISODate);
    json["cashTips"] = m_cashTips;
    json["creditCardTips"] = m_creditCardTips;
    json["pooledTips"] = m_pooledTips;
    json["autoGratuity"] = m_autoGratuity;
    json["totalSales"] = m_totalSales;
    return json;
}

TipSummary* TipSummary::fromJson(const QJsonObject& json, QObject* parent) {
    auto* summary = new TipSummary(parent);
    summary->m_employeeId = json["employeeId"].toInt();
    summary->m_startDate = QDate::fromString(json["startDate"].toString(), Qt::ISODate);
    summary->m_endDate = QDate::fromString(json["endDate"].toString(), Qt::ISODate);
    summary->m_cashTips = json["cashTips"].toInt();
    summary->m_creditCardTips = json["creditCardTips"].toInt();
    summary->m_pooledTips = json["pooledTips"].toInt();
    summary->m_autoGratuity = json["autoGratuity"].toInt();
    summary->m_totalSales = json["totalSales"].toInt();
    return summary;
}

//=============================================================================
// TipsManager Implementation
//=============================================================================

TipsManager* TipsManager::s_instance = nullptr;

TipsManager::TipsManager(QObject* parent)
    : QObject(parent)
{
}

TipsManager* TipsManager::instance() {
    if (!s_instance) {
        s_instance = new TipsManager();
    }
    return s_instance;
}

TipEntry* TipsManager::addTip(int checkId, int employeeId, int amount, TipEntry::TipType type) {
    auto* tip = new TipEntry(this);
    tip->setId(m_nextTipId++);
    tip->setCheckId(checkId);
    tip->setEmployeeId(employeeId);
    tip->setAmount(amount);
    tip->setTipType(type);
    tip->setTimestamp(QDateTime::currentDateTime());
    
    // If pooling is enabled and this tip type qualifies
    if (m_poolingEnabled && m_poolPercentage > 0) {
        tip->setPooled(true);
    }
    
    m_tips.append(tip);
    
    emit tipAdded(tip);
    emit tipsChanged();
    
    return tip;
}

TipEntry* TipsManager::findTip(int id) {
    for (auto* tip : m_tips) {
        if (tip->id() == id) return tip;
    }
    return nullptr;
}

void TipsManager::editTip(TipEntry* tip) {
    if (tip) {
        emit tipModified(tip);
        emit tipsChanged();
    }
}

void TipsManager::deleteTip(TipEntry* tip) {
    if (tip && m_tips.removeOne(tip)) {
        int id = tip->id();
        delete tip;
        emit tipRemoved(id);
        emit tipsChanged();
    }
}

QList<TipEntry*> TipsManager::tipsForEmployee(int employeeId) const {
    QList<TipEntry*> result;
    for (auto* tip : m_tips) {
        if (tip->employeeId() == employeeId) {
            result.append(tip);
        }
    }
    return result;
}

QList<TipEntry*> TipsManager::tipsForDate(const QDate& date) const {
    QList<TipEntry*> result;
    for (auto* tip : m_tips) {
        if (tip->timestamp().date() == date) {
            result.append(tip);
        }
    }
    return result;
}

QList<TipEntry*> TipsManager::tipsForPeriod(const QDate& start, const QDate& end) const {
    QList<TipEntry*> result;
    for (auto* tip : m_tips) {
        QDate d = tip->timestamp().date();
        if (d >= start && d <= end) {
            result.append(tip);
        }
    }
    return result;
}

QList<TipEntry*> TipsManager::tipsForCheck(int checkId) const {
    QList<TipEntry*> result;
    for (auto* tip : m_tips) {
        if (tip->checkId() == checkId) {
            result.append(tip);
        }
    }
    return result;
}

int TipsManager::totalTipsForEmployee(int employeeId, const QDate& date) const {
    int total = 0;
    for (const auto* tip : m_tips) {
        if (tip->employeeId() == employeeId && tip->timestamp().date() == date) {
            total += tip->amount();
        }
    }
    return total;
}

int TipsManager::totalTipsForDate(const QDate& date) const {
    int total = 0;
    for (const auto* tip : m_tips) {
        if (tip->timestamp().date() == date) {
            total += tip->amount();
        }
    }
    return total;
}

TipDistribution* TipsManager::createDistribution(const QDate& date) {
    auto* dist = new TipDistribution(this);
    dist->setId(m_nextDistId++);
    dist->setDate(date);
    
    // Calculate total pool from tips for the date
    int totalPool = 0;
    auto tips = tipsForDate(date);
    for (const auto* tip : tips) {
        if (tip->isPooled()) {
            totalPool += static_cast<int>(tip->amount() * m_poolPercentage / 100.0);
        }
    }
    dist->setTotalPoolAmount(totalPool);
    
    m_distributions.append(dist);
    
    emit distributionCreated(dist);
    return dist;
}

TipDistribution* TipsManager::findDistribution(int id) {
    for (auto* dist : m_distributions) {
        if (dist->id() == id) return dist;
    }
    return nullptr;
}

TipDistribution* TipsManager::distributionForDate(const QDate& date) {
    for (auto* dist : m_distributions) {
        if (dist->date() == date) return dist;
    }
    return nullptr;
}

void TipsManager::executeDistribution(TipDistribution* dist) {
    if (!dist || dist->isDistributed()) return;
    
    // Calculate final shares based on percentages
    for (auto it = dist->employeePercentages().constBegin(); 
         it != dist->employeePercentages().constEnd(); ++it) {
        int share = dist->calculateShare(it.key());
        dist->setEmployeeShare(it.key(), share);
    }
    
    dist->setDistributed(true);
    emit tipsChanged();
}

int TipsManager::calculateAutoGratuity(int subtotal) const {
    if (!m_autoGratuityEnabled) return 0;
    return static_cast<int>(subtotal * m_autoGratuityPercent / 100.0);
}

TipSummary* TipsManager::summaryForEmployee(int employeeId, const QDate& start, const QDate& end) {
    auto* summary = new TipSummary();
    summary->setEmployeeId(employeeId);
    summary->setStartDate(start);
    summary->setEndDate(end);
    
    int cash = 0, credit = 0, pooled = 0, autoGrat = 0;
    
    auto tips = tipsForEmployee(employeeId);
    for (const auto* tip : tips) {
        QDate d = tip->timestamp().date();
        if (d >= start && d <= end) {
            switch (tip->tipType()) {
                case TipEntry::TipType::Cash:
                    cash += tip->amount();
                    break;
                case TipEntry::TipType::CreditCard:
                    credit += tip->amount();
                    break;
                case TipEntry::TipType::Automatic:
                    autoGrat += tip->amount();
                    break;
                default:
                    break;
            }
        }
    }
    
    // Add pooled tips from distributions
    for (const auto* dist : m_distributions) {
        if (dist->isDistributed() && dist->date() >= start && dist->date() <= end) {
            auto shares = dist->employeeShares();
            if (shares.contains(employeeId)) {
                pooled += shares[employeeId];
            }
        }
    }
    
    summary->setCashTips(cash);
    summary->setCreditCardTips(credit);
    summary->setPooledTips(pooled);
    summary->setAutoGratuity(autoGrat);
    
    return summary;
}

TipSummary* TipsManager::summaryForDate(const QDate& date) {
    auto* summary = new TipSummary();
    summary->setStartDate(date);
    summary->setEndDate(date);
    
    int cash = 0, credit = 0, pooled = 0, autoGrat = 0;
    
    auto tips = tipsForDate(date);
    for (const auto* tip : tips) {
        switch (tip->tipType()) {
            case TipEntry::TipType::Cash:
                cash += tip->amount();
                break;
            case TipEntry::TipType::CreditCard:
                credit += tip->amount();
                break;
            case TipEntry::TipType::Automatic:
                autoGrat += tip->amount();
                break;
            default:
                break;
        }
        
        if (tip->isPooled()) {
            pooled += static_cast<int>(tip->amount() * m_poolPercentage / 100.0);
        }
    }
    
    summary->setCashTips(cash);
    summary->setCreditCardTips(credit);
    summary->setPooledTips(pooled);
    summary->setAutoGratuity(autoGrat);
    
    return summary;
}

bool TipsManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextTipId"] = m_nextTipId;
    root["nextDistId"] = m_nextDistId;
    root["poolingEnabled"] = m_poolingEnabled;
    root["poolPercentage"] = m_poolPercentage;
    root["autoGratuityEnabled"] = m_autoGratuityEnabled;
    root["autoGratuityPercent"] = m_autoGratuityPercent;
    root["autoGratuityThreshold"] = m_autoGratuityThreshold;
    
    QJsonArray tipsArray;
    for (const auto* tip : m_tips) {
        tipsArray.append(tip->toJson());
    }
    root["tips"] = tipsArray;
    
    QJsonArray distArray;
    for (const auto* dist : m_distributions) {
        distArray.append(dist->toJson());
    }
    root["distributions"] = distArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool TipsManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextTipId = root["nextTipId"].toInt(1);
    m_nextDistId = root["nextDistId"].toInt(1);
    m_poolingEnabled = root["poolingEnabled"].toBool();
    m_poolPercentage = root["poolPercentage"].toDouble();
    m_autoGratuityEnabled = root["autoGratuityEnabled"].toBool();
    m_autoGratuityPercent = root["autoGratuityPercent"].toDouble(18.0);
    m_autoGratuityThreshold = root["autoGratuityThreshold"].toInt(8);
    
    qDeleteAll(m_tips);
    m_tips.clear();
    
    QJsonArray tipsArray = root["tips"].toArray();
    for (const auto& ref : tipsArray) {
        auto* tip = TipEntry::fromJson(ref.toObject(), this);
        m_tips.append(tip);
    }
    
    qDeleteAll(m_distributions);
    m_distributions.clear();
    
    QJsonArray distArray = root["distributions"].toArray();
    for (const auto& ref : distArray) {
        auto* dist = TipDistribution::fromJson(ref.toObject(), this);
        m_distributions.append(dist);
    }
    
    emit tipsChanged();
    return true;
}

} // namespace vt2
