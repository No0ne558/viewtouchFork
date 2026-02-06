// ViewTouch V2 - Exception/Audit System Implementation

#include "exception.hpp"
#include <QJsonDocument>
#include <QFile>

namespace vt2 {

//=============================================================================
// ExceptionRecord Implementation
//=============================================================================

ExceptionRecord::ExceptionRecord(QObject* parent)
    : QObject(parent)
    , m_requestedAt(QDateTime::currentDateTime())
{
}

QJsonObject ExceptionRecord::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["type"] = static_cast<int>(m_type);
    json["reason"] = static_cast<int>(m_reason);
    json["status"] = static_cast<int>(m_status);
    json["checkId"] = m_checkId;
    json["subCheckId"] = m_subCheckId;
    json["itemId"] = m_itemId;
    json["paymentId"] = m_paymentId;
    json["requestedBy"] = m_requestedBy;
    json["approvedBy"] = m_approvedBy;
    json["requestedAt"] = m_requestedAt.toString(Qt::ISODate);
    json["approvedAt"] = m_approvedAt.toString(Qt::ISODate);
    json["appliedAt"] = m_appliedAt.toString(Qt::ISODate);
    json["originalAmount"] = m_originalAmount;
    json["adjustedAmount"] = m_adjustedAmount;
    json["description"] = m_description;
    json["comment"] = m_comment;
    json["managerCode"] = m_managerCode;
    json["requiresApproval"] = m_requiresApproval;
    json["terminalId"] = m_terminalId;
    json["ipAddress"] = m_ipAddress;
    return json;
}

ExceptionRecord* ExceptionRecord::fromJson(const QJsonObject& json, QObject* parent) {
    auto* record = new ExceptionRecord(parent);
    record->m_id = json["id"].toInt();
    record->m_type = static_cast<ExceptionType>(json["type"].toInt());
    record->m_reason = static_cast<ExceptionReason>(json["reason"].toInt());
    record->m_status = static_cast<ExceptionStatus>(json["status"].toInt());
    record->m_checkId = json["checkId"].toInt();
    record->m_subCheckId = json["subCheckId"].toInt();
    record->m_itemId = json["itemId"].toInt();
    record->m_paymentId = json["paymentId"].toInt();
    record->m_requestedBy = json["requestedBy"].toInt();
    record->m_approvedBy = json["approvedBy"].toInt();
    record->m_requestedAt = QDateTime::fromString(json["requestedAt"].toString(), Qt::ISODate);
    record->m_approvedAt = QDateTime::fromString(json["approvedAt"].toString(), Qt::ISODate);
    record->m_appliedAt = QDateTime::fromString(json["appliedAt"].toString(), Qt::ISODate);
    record->m_originalAmount = json["originalAmount"].toInt();
    record->m_adjustedAmount = json["adjustedAmount"].toInt();
    record->m_description = json["description"].toString();
    record->m_comment = json["comment"].toString();
    record->m_managerCode = json["managerCode"].toString();
    record->m_requiresApproval = json["requiresApproval"].toBool();
    record->m_terminalId = json["terminalId"].toString();
    record->m_ipAddress = json["ipAddress"].toString();
    return record;
}

QString ExceptionRecord::typeToString(ExceptionType type) {
    switch (type) {
    case ExceptionType::None: return "None";
    case ExceptionType::VoidCheck: return "Void Check";
    case ExceptionType::RebuildCheck: return "Rebuild Check";
    case ExceptionType::ReOpenCheck: return "Reopen Check";
    case ExceptionType::CancelCheck: return "Cancel Check";
    case ExceptionType::VoidItem: return "Void Item";
    case ExceptionType::CompItem: return "Comp Item";
    case ExceptionType::DiscountItem: return "Discount Item";
    case ExceptionType::ReturnItem: return "Return Item";
    case ExceptionType::ChangePrice: return "Price Override";
    case ExceptionType::ChangeQuantity: return "Quantity Override";
    case ExceptionType::VoidPayment: return "Void Payment";
    case ExceptionType::RefundPayment: return "Refund Payment";
    case ExceptionType::AdjustTip: return "Adjust Tip";
    case ExceptionType::CashBack: return "Cash Back";
    case ExceptionType::PaidOut: return "Paid Out";
    case ExceptionType::PaidIn: return "Paid In";
    case ExceptionType::NoSale: return "No Sale";
    case ExceptionType::DrawerCount: return "Drawer Count";
    case ExceptionType::ClockAdjust: return "Clock Adjust";
    case ExceptionType::BreakAdjust: return "Break Adjust";
    case ExceptionType::PayRateOverride: return "Pay Rate Override";
    case ExceptionType::OverrideTotal: return "Override Total";
    case ExceptionType::TaxExempt: return "Tax Exempt";
    case ExceptionType::GratuityOverride: return "Gratuity Override";
    case ExceptionType::PriceOverride: return "Price Override";
    default: return "Unknown";
    }
}

QString ExceptionRecord::reasonToString(ExceptionReason reason) {
    switch (reason) {
    case ExceptionReason::None: return "None";
    case ExceptionReason::CustomerRequest: return "Customer Request";
    case ExceptionReason::ManagerDecision: return "Manager Decision";
    case ExceptionReason::ItemQuality: return "Item Quality";
    case ExceptionReason::ServiceIssue: return "Service Issue";
    case ExceptionReason::OrderError: return "Order Error";
    case ExceptionReason::SystemError: return "System Error";
    case ExceptionReason::EmployeeError: return "Employee Error";
    case ExceptionReason::PolicyException: return "Policy Exception";
    case ExceptionReason::Promotion: return "Promotion";
    case ExceptionReason::VIP: return "VIP";
    case ExceptionReason::Training: return "Training";
    case ExceptionReason::Other: return "Other";
    default: return "Unknown";
    }
}

QString ExceptionRecord::statusToString(ExceptionStatus status) {
    switch (status) {
    case ExceptionStatus::Pending: return "Pending";
    case ExceptionStatus::Approved: return "Approved";
    case ExceptionStatus::Denied: return "Denied";
    case ExceptionStatus::Applied: return "Applied";
    case ExceptionStatus::Reversed: return "Reversed";
    default: return "Unknown";
    }
}

//=============================================================================
// ExceptionPolicy Implementation
//=============================================================================

QJsonObject ExceptionPolicy::toJson() const {
    QJsonObject json;
    json["type"] = static_cast<int>(type);
    json["requiresApproval"] = requiresApproval;
    json["maxAmountWithoutApproval"] = maxAmountWithoutApproval;
    json["requiredSecurityLevel"] = requiredSecurityLevel;
    json["requiresComment"] = requiresComment;
    json["printReceipt"] = printReceipt;
    json["trackInReports"] = trackInReports;
    json["receiptMessage"] = receiptMessage;
    return json;
}

ExceptionPolicy ExceptionPolicy::fromJson(const QJsonObject& json) {
    ExceptionPolicy policy;
    policy.type = static_cast<ExceptionType>(json["type"].toInt());
    policy.requiresApproval = json["requiresApproval"].toBool();
    policy.maxAmountWithoutApproval = json["maxAmountWithoutApproval"].toInt();
    policy.requiredSecurityLevel = json["requiredSecurityLevel"].toInt();
    policy.requiresComment = json["requiresComment"].toBool();
    policy.printReceipt = json["printReceipt"].toBool(true);
    policy.trackInReports = json["trackInReports"].toBool(true);
    policy.receiptMessage = json["receiptMessage"].toString();
    return policy;
}

//=============================================================================
// AuditEntry Implementation
//=============================================================================

QJsonObject AuditEntry::toJson() const {
    QJsonObject json;
    json["id"] = id;
    json["timestamp"] = timestamp.toString(Qt::ISODate);
    json["action"] = action;
    json["category"] = category;
    json["employeeId"] = employeeId;
    json["checkId"] = checkId;
    json["terminalId"] = terminalId;
    json["details"] = details;
    json["ipAddress"] = ipAddress;
    json["beforeValue"] = beforeValue;
    json["afterValue"] = afterValue;
    return json;
}

AuditEntry AuditEntry::fromJson(const QJsonObject& json) {
    AuditEntry entry;
    entry.id = json["id"].toInt();
    entry.timestamp = QDateTime::fromString(json["timestamp"].toString(), Qt::ISODate);
    entry.action = json["action"].toString();
    entry.category = json["category"].toString();
    entry.employeeId = json["employeeId"].toInt();
    entry.checkId = json["checkId"].toInt();
    entry.terminalId = json["terminalId"].toString();
    entry.details = json["details"].toString();
    entry.ipAddress = json["ipAddress"].toString();
    entry.beforeValue = json["beforeValue"].toString();
    entry.afterValue = json["afterValue"].toString();
    return entry;
}

//=============================================================================
// ExceptionManager Implementation
//=============================================================================

ExceptionManager* ExceptionManager::s_instance = nullptr;

ExceptionManager::ExceptionManager(QObject* parent)
    : QObject(parent)
{
    initializeDefaultPolicies();
}

ExceptionManager* ExceptionManager::instance() {
    if (!s_instance) {
        s_instance = new ExceptionManager();
    }
    return s_instance;
}

void ExceptionManager::initializeDefaultPolicies() {
    // Void check - always requires manager
    ExceptionPolicy voidCheck;
    voidCheck.type = ExceptionType::VoidCheck;
    voidCheck.requiresApproval = true;
    voidCheck.requiredSecurityLevel = 3;
    voidCheck.requiresComment = true;
    voidCheck.printReceipt = true;
    m_policies[ExceptionType::VoidCheck] = voidCheck;

    // Void item
    ExceptionPolicy voidItem;
    voidItem.type = ExceptionType::VoidItem;
    voidItem.requiresApproval = true;
    voidItem.maxAmountWithoutApproval = 1000; // $10
    voidItem.requiredSecurityLevel = 2;
    m_policies[ExceptionType::VoidItem] = voidItem;

    // Comp item
    ExceptionPolicy compItem;
    compItem.type = ExceptionType::CompItem;
    compItem.requiresApproval = true;
    compItem.requiredSecurityLevel = 2;
    compItem.requiresComment = true;
    m_policies[ExceptionType::CompItem] = compItem;

    // Rebuild check
    ExceptionPolicy rebuild;
    rebuild.type = ExceptionType::RebuildCheck;
    rebuild.requiresApproval = true;
    rebuild.requiredSecurityLevel = 3;
    rebuild.requiresComment = true;
    m_policies[ExceptionType::RebuildCheck] = rebuild;

    // Paid out
    ExceptionPolicy paidOut;
    paidOut.type = ExceptionType::PaidOut;
    paidOut.requiresApproval = true;
    paidOut.maxAmountWithoutApproval = 2000; // $20
    paidOut.requiredSecurityLevel = 2;
    paidOut.requiresComment = true;
    m_policies[ExceptionType::PaidOut] = paidOut;

    // No sale
    ExceptionPolicy noSale;
    noSale.type = ExceptionType::NoSale;
    noSale.requiresApproval = false;
    noSale.trackInReports = true;
    m_policies[ExceptionType::NoSale] = noSale;
}

ExceptionRecord* ExceptionManager::createException(ExceptionType type, int checkId) {
    auto* record = new ExceptionRecord(this);
    record->setId(m_nextExceptionId++);
    record->setType(type);
    record->setCheckId(checkId);
    record->setRequestedAt(QDateTime::currentDateTime());

    // Check policy
    auto policy = m_policies.value(type);
    record->setRequiresApproval(policy.requiresApproval);

    m_exceptions.append(record);
    emit exceptionCreated(record);

    return record;
}

bool ExceptionManager::requestApproval(int exceptionId, int managerId, const QString& code) {
    ExceptionRecord* record = nullptr;
    for (auto* r : m_exceptions) {
        if (r->id() == exceptionId) {
            record = r;
            break;
        }
    }

    if (!record) return false;

    if (validateManagerCode(code)) {
        record->setApprovedBy(managerId);
        record->setManagerCode(code);
        emit approvalRequired(record);
        return true;
    }

    return false;
}

bool ExceptionManager::approveException(int exceptionId, int managerId, const QString& code) {
    ExceptionRecord* record = nullptr;
    for (auto* r : m_exceptions) {
        if (r->id() == exceptionId) {
            record = r;
            break;
        }
    }

    if (!record) return false;
    if (record->status() != ExceptionStatus::Pending) return false;

    if (!validateManagerCode(code)) return false;

    record->setStatus(ExceptionStatus::Approved);
    record->setApprovedBy(managerId);
    record->setApprovedAt(QDateTime::currentDateTime());
    record->setManagerCode(code);

    logAudit("Exception Approved", "exception", managerId, record->checkId(),
             QString("Exception ID: %1, Type: %2")
                 .arg(exceptionId)
                 .arg(ExceptionRecord::typeToString(record->type())));

    emit exceptionApproved(exceptionId);
    return true;
}

bool ExceptionManager::denyException(int exceptionId, int managerId, const QString& comment) {
    ExceptionRecord* record = nullptr;
    for (auto* r : m_exceptions) {
        if (r->id() == exceptionId) {
            record = r;
            break;
        }
    }

    if (!record) return false;
    if (record->status() != ExceptionStatus::Pending) return false;

    record->setStatus(ExceptionStatus::Denied);
    record->setApprovedBy(managerId);
    record->setApprovedAt(QDateTime::currentDateTime());
    record->setComment(record->comment() + " [DENIED: " + comment + "]");

    logAudit("Exception Denied", "exception", managerId, record->checkId(),
             QString("Exception ID: %1, Reason: %2").arg(exceptionId).arg(comment));

    emit exceptionDenied(exceptionId);
    return true;
}

bool ExceptionManager::applyException(int exceptionId) {
    ExceptionRecord* record = nullptr;
    for (auto* r : m_exceptions) {
        if (r->id() == exceptionId) {
            record = r;
            break;
        }
    }

    if (!record) return false;

    // Must be approved (or not require approval)
    if (record->requiresApproval() && record->status() != ExceptionStatus::Approved) {
        return false;
    }

    record->setStatus(ExceptionStatus::Applied);
    record->setAppliedAt(QDateTime::currentDateTime());

    // TODO: Actually apply the exception to the check/item/payment

    logAudit("Exception Applied", "exception", record->approvedBy(), record->checkId(),
             QString("Exception ID: %1, Type: %2, Amount: %3")
                 .arg(exceptionId)
                 .arg(ExceptionRecord::typeToString(record->type()))
                 .arg(record->impactAmount()));

    emit exceptionApplied(exceptionId);
    return true;
}

bool ExceptionManager::reverseException(int exceptionId, int managerId) {
    ExceptionRecord* record = nullptr;
    for (auto* r : m_exceptions) {
        if (r->id() == exceptionId) {
            record = r;
            break;
        }
    }

    if (!record) return false;
    if (record->status() != ExceptionStatus::Applied) return false;

    record->setStatus(ExceptionStatus::Reversed);

    // TODO: Actually reverse the exception effects

    logAudit("Exception Reversed", "exception", managerId, record->checkId(),
             QString("Exception ID: %1").arg(exceptionId));

    emit exceptionReversed(exceptionId);
    return true;
}

ExceptionRecord* ExceptionManager::voidCheck(int checkId, ExceptionReason reason,
                                              int requestedBy, int managerId) {
    auto* record = createException(ExceptionType::VoidCheck, checkId);
    record->setReason(reason);
    record->setRequestedBy(requestedBy);

    if (managerId > 0) {
        record->setApprovedBy(managerId);
        record->setApprovedAt(QDateTime::currentDateTime());
        record->setStatus(ExceptionStatus::Approved);
    }

    return record;
}

ExceptionRecord* ExceptionManager::voidItem(int checkId, int itemId, ExceptionReason reason,
                                             int requestedBy, int managerId) {
    auto* record = createException(ExceptionType::VoidItem, checkId);
    record->setItemId(itemId);
    record->setReason(reason);
    record->setRequestedBy(requestedBy);

    if (managerId > 0) {
        record->setApprovedBy(managerId);
        record->setApprovedAt(QDateTime::currentDateTime());
        record->setStatus(ExceptionStatus::Approved);
    }

    return record;
}

ExceptionRecord* ExceptionManager::compItem(int checkId, int itemId, int amount,
                                             ExceptionReason reason, int requestedBy) {
    auto* record = createException(ExceptionType::CompItem, checkId);
    record->setItemId(itemId);
    record->setOriginalAmount(amount);
    record->setAdjustedAmount(0);
    record->setReason(reason);
    record->setRequestedBy(requestedBy);

    return record;
}

ExceptionRecord* ExceptionManager::rebuildCheck(int checkId, int requestedBy, int managerId) {
    auto* record = createException(ExceptionType::RebuildCheck, checkId);
    record->setRequestedBy(requestedBy);
    record->setApprovedBy(managerId);

    if (managerId > 0) {
        record->setApprovedAt(QDateTime::currentDateTime());
        record->setStatus(ExceptionStatus::Approved);
    }

    return record;
}

ExceptionRecord* ExceptionManager::paidOut(int amount, const QString& description, int employeeId) {
    auto* record = createException(ExceptionType::PaidOut, 0);
    record->setOriginalAmount(amount);
    record->setDescription(description);
    record->setRequestedBy(employeeId);

    return record;
}

ExceptionRecord* ExceptionManager::paidIn(int amount, const QString& description, int employeeId) {
    auto* record = createException(ExceptionType::PaidIn, 0);
    record->setOriginalAmount(amount);
    record->setDescription(description);
    record->setRequestedBy(employeeId);

    return record;
}

ExceptionRecord* ExceptionManager::noSale(int employeeId, const QString& reason) {
    auto* record = createException(ExceptionType::NoSale, 0);
    record->setRequestedBy(employeeId);
    record->setDescription(reason);
    record->setRequiresApproval(false);
    record->setStatus(ExceptionStatus::Applied);
    record->setAppliedAt(QDateTime::currentDateTime());

    return record;
}

void ExceptionManager::setPolicy(ExceptionType type, const ExceptionPolicy& policy) {
    m_policies[type] = policy;
}

ExceptionPolicy ExceptionManager::policy(ExceptionType type) const {
    return m_policies.value(type);
}

bool ExceptionManager::requiresApproval(ExceptionType type, int amount) const {
    auto pol = m_policies.value(type);
    if (!pol.requiresApproval) return false;
    if (pol.maxAmountWithoutApproval > 0 && amount <= pol.maxAmountWithoutApproval) {
        return false;
    }
    return true;
}

int ExceptionManager::requiredSecurityLevel(ExceptionType type) const {
    return m_policies.value(type).requiredSecurityLevel;
}

QList<ExceptionRecord*> ExceptionManager::exceptionsForCheck(int checkId) {
    QList<ExceptionRecord*> result;
    for (auto* record : m_exceptions) {
        if (record->checkId() == checkId) {
            result.append(record);
        }
    }
    return result;
}

QList<ExceptionRecord*> ExceptionManager::exceptionsForEmployee(int employeeId) {
    QList<ExceptionRecord*> result;
    for (auto* record : m_exceptions) {
        if (record->requestedBy() == employeeId || record->approvedBy() == employeeId) {
            result.append(record);
        }
    }
    return result;
}

QList<ExceptionRecord*> ExceptionManager::pendingExceptions() {
    QList<ExceptionRecord*> result;
    for (auto* record : m_exceptions) {
        if (record->status() == ExceptionStatus::Pending) {
            result.append(record);
        }
    }
    return result;
}

QList<ExceptionRecord*> ExceptionManager::exceptionsByType(ExceptionType type) {
    QList<ExceptionRecord*> result;
    for (auto* record : m_exceptions) {
        if (record->type() == type) {
            result.append(record);
        }
    }
    return result;
}

QList<ExceptionRecord*> ExceptionManager::exceptionsInDateRange(const QDate& from, const QDate& to) {
    QList<ExceptionRecord*> result;
    for (auto* record : m_exceptions) {
        QDate recordDate = record->requestedAt().date();
        if (recordDate >= from && recordDate <= to) {
            result.append(record);
        }
    }
    return result;
}

void ExceptionManager::logAudit(const QString& action, const QString& category,
                                 int employeeId, int checkId, const QString& details) {
    AuditEntry entry;
    entry.id = m_nextAuditId++;
    entry.timestamp = QDateTime::currentDateTime();
    entry.action = action;
    entry.category = category;
    entry.employeeId = employeeId;
    entry.checkId = checkId;
    entry.details = details;

    m_auditLog.append(entry);
    emit auditLogged(entry);
}

QList<AuditEntry> ExceptionManager::auditLog(const QDate& from, const QDate& to) {
    QList<AuditEntry> result;
    for (const auto& entry : m_auditLog) {
        QDate entryDate = entry.timestamp.date();
        if (entryDate >= from && entryDate <= to) {
            result.append(entry);
        }
    }
    return result;
}

QList<AuditEntry> ExceptionManager::auditLogForEmployee(int employeeId) {
    QList<AuditEntry> result;
    for (const auto& entry : m_auditLog) {
        if (entry.employeeId == employeeId) {
            result.append(entry);
        }
    }
    return result;
}

QList<AuditEntry> ExceptionManager::auditLogForCheck(int checkId) {
    QList<AuditEntry> result;
    for (const auto& entry : m_auditLog) {
        if (entry.checkId == checkId) {
            result.append(entry);
        }
    }
    return result;
}

QJsonObject ExceptionManager::dailyExceptionSummary(const QDate& date) {
    QJsonObject summary;
    summary["date"] = date.toString(Qt::ISODate);

    int totalVoids = 0;
    int totalComps = 0;
    int voidCount = 0;
    int compCount = 0;
    int paidOutTotal = 0;
    int paidInTotal = 0;

    for (auto* record : m_exceptions) {
        if (record->requestedAt().date() != date) continue;
        if (record->status() != ExceptionStatus::Applied) continue;

        switch (record->type()) {
        case ExceptionType::VoidCheck:
        case ExceptionType::VoidItem:
            totalVoids += record->impactAmount();
            voidCount++;
            break;
        case ExceptionType::CompItem:
            totalComps += record->impactAmount();
            compCount++;
            break;
        case ExceptionType::PaidOut:
            paidOutTotal += record->originalAmount();
            break;
        case ExceptionType::PaidIn:
            paidInTotal += record->originalAmount();
            break;
        default:
            break;
        }
    }

    summary["totalVoids"] = totalVoids;
    summary["voidCount"] = voidCount;
    summary["totalComps"] = totalComps;
    summary["compCount"] = compCount;
    summary["paidOut"] = paidOutTotal;
    summary["paidIn"] = paidInTotal;

    return summary;
}

QJsonObject ExceptionManager::employeeExceptionSummary(int employeeId,
                                                        const QDate& from, const QDate& to) {
    QJsonObject summary;
    summary["employeeId"] = employeeId;
    summary["dateFrom"] = from.toString(Qt::ISODate);
    summary["dateTo"] = to.toString(Qt::ISODate);

    QMap<QString, int> typeCounts;
    QMap<QString, int> typeAmounts;

    for (auto* record : m_exceptions) {
        if (record->requestedBy() != employeeId) continue;

        QDate recordDate = record->requestedAt().date();
        if (recordDate < from || recordDate > to) continue;

        QString typeStr = ExceptionRecord::typeToString(record->type());
        typeCounts[typeStr]++;
        typeAmounts[typeStr] += record->impactAmount();
    }

    QJsonObject counts;
    QJsonObject amounts;
    for (auto it = typeCounts.begin(); it != typeCounts.end(); ++it) {
        counts[it.key()] = it.value();
        amounts[it.key()] = typeAmounts[it.key()];
    }

    summary["counts"] = counts;
    summary["amounts"] = amounts;

    return summary;
}

bool ExceptionManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextExceptionId"] = m_nextExceptionId;
    root["nextAuditId"] = m_nextAuditId;

    QJsonArray exceptionsArray;
    for (const auto* record : m_exceptions) {
        exceptionsArray.append(record->toJson());
    }
    root["exceptions"] = exceptionsArray;

    QJsonArray auditArray;
    for (const auto& entry : m_auditLog) {
        auditArray.append(entry.toJson());
    }
    root["auditLog"] = auditArray;

    QJsonArray policiesArray;
    for (auto it = m_policies.begin(); it != m_policies.end(); ++it) {
        policiesArray.append(it.value().toJson());
    }
    root["policies"] = policiesArray;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool ExceptionManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();

    m_nextExceptionId = root["nextExceptionId"].toInt(1);
    m_nextAuditId = root["nextAuditId"].toInt(1);

    qDeleteAll(m_exceptions);
    m_exceptions.clear();

    QJsonArray exceptionsArray = root["exceptions"].toArray();
    for (const auto& ref : exceptionsArray) {
        auto* record = ExceptionRecord::fromJson(ref.toObject(), this);
        m_exceptions.append(record);
    }

    m_auditLog.clear();
    QJsonArray auditArray = root["auditLog"].toArray();
    for (const auto& ref : auditArray) {
        m_auditLog.append(AuditEntry::fromJson(ref.toObject()));
    }

    m_policies.clear();
    QJsonArray policiesArray = root["policies"].toArray();
    for (const auto& ref : policiesArray) {
        ExceptionPolicy policy = ExceptionPolicy::fromJson(ref.toObject());
        m_policies[policy.type] = policy;
    }

    return true;
}

bool ExceptionManager::validateManagerCode(const QString& code) {
    // TODO: Actually validate against employee records
    return !code.isEmpty() && code.length() >= 4;
}

//=============================================================================
// VoidZone Implementation
//=============================================================================

VoidZone::VoidZone(QObject* parent)
    : QObject(parent)
{
}

void VoidZone::voidSelectedItem() {
    m_voidEntireCheck = false;
}

void VoidZone::voidEntireCheck() {
    m_voidEntireCheck = true;
}

void VoidZone::cancelOperation() {
    m_selectedItem = 0;
    m_selectedReason = ExceptionReason::None;
    m_comment.clear();
    m_voidEntireCheck = false;
    emit voidCancelled();
}

void VoidZone::selectReason(ExceptionReason reason) {
    m_selectedReason = reason;
}

void VoidZone::setComment(const QString& comment) {
    m_comment = comment;
}

void VoidZone::submitVoid() {
    auto* manager = ExceptionManager::instance();

    ExceptionRecord* record = nullptr;

    if (m_voidEntireCheck) {
        record = manager->voidCheck(m_checkId, m_selectedReason, 0);
    } else if (m_selectedItem > 0) {
        record = manager->voidItem(m_checkId, m_selectedItem, m_selectedReason, 0);
    }

    if (record) {
        record->setComment(m_comment);

        if (record->requiresApproval()) {
            emit approvalRequired(record->id());
        } else {
            manager->applyException(record->id());
            emit voidComplete(record->id());
        }
    }
}

//=============================================================================
// CompZone Implementation
//=============================================================================

CompZone::CompZone(QObject* parent)
    : QObject(parent)
{
}

void CompZone::compFullItem() {
    // Full comp - amount will be set when applying
    m_compAmount = -1; // Indicates full comp
}

void CompZone::compPartialAmount(int cents) {
    m_compAmount = cents;
}

void CompZone::compPercentage(int percent) {
    // Will be calculated when applying
    m_compAmount = -percent; // Negative indicates percentage
}

void CompZone::selectReason(ExceptionReason reason) {
    m_reason = reason;
}

void CompZone::setComment(const QString& comment) {
    m_comment = comment;
}

void CompZone::submitComp() {
    auto* manager = ExceptionManager::instance();

    // Get actual amount (for now, use comp amount directly)
    int amount = m_compAmount > 0 ? m_compAmount : 0;

    auto* record = manager->compItem(m_checkId, m_selectedItem, amount, m_reason, 0);
    record->setComment(m_comment);

    if (record->requiresApproval()) {
        emit approvalRequired(record->id());
    } else {
        manager->applyException(record->id());
        emit compComplete(record->id());
    }
}

} // namespace vt2
