// ViewTouch V2 - Discount/Coupon System Implementation

#include "discount.hpp"
#include <QJsonDocument>
#include <QFile>
#include <QRandomGenerator>

namespace vt2 {

//=============================================================================
// TimeWindow Implementation
//=============================================================================

bool TimeWindow::isValidNow() const {
    return isValidAt(QDateTime::currentDateTime());
}

bool TimeWindow::isValidAt(const QDateTime& dt) const {
    QDate date = dt.date();
    QTime time = dt.time();

    // Check date range
    if (validFrom.isValid() && date < validFrom) return false;
    if (validUntil.isValid() && date > validUntil) return false;

    // Check day of week
    if (!validDays.isEmpty()) {
        Qt::DayOfWeek dayOfWeek = static_cast<Qt::DayOfWeek>(date.dayOfWeek());
        if (!validDays.contains(dayOfWeek)) return false;
    }

    // Check time range
    if (startTime.isValid() && endTime.isValid()) {
        if (startTime <= endTime) {
            // Normal range (e.g., 9am to 5pm)
            if (time < startTime || time > endTime) return false;
        } else {
            // Overnight range (e.g., 10pm to 2am)
            if (time < startTime && time > endTime) return false;
        }
    }

    return true;
}

QJsonObject TimeWindow::toJson() const {
    QJsonObject json;
    json["startTime"] = startTime.toString("HH:mm");
    json["endTime"] = endTime.toString("HH:mm");

    QJsonArray daysArray;
    for (Qt::DayOfWeek day : validDays) {
        daysArray.append(static_cast<int>(day));
    }
    json["validDays"] = daysArray;

    json["validFrom"] = validFrom.toString(Qt::ISODate);
    json["validUntil"] = validUntil.toString(Qt::ISODate);

    return json;
}

TimeWindow TimeWindow::fromJson(const QJsonObject& json) {
    TimeWindow tw;
    tw.startTime = QTime::fromString(json["startTime"].toString(), "HH:mm");
    tw.endTime = QTime::fromString(json["endTime"].toString(), "HH:mm");

    QJsonArray daysArray = json["validDays"].toArray();
    for (const auto& ref : daysArray) {
        tw.validDays.append(static_cast<Qt::DayOfWeek>(ref.toInt()));
    }

    tw.validFrom = QDate::fromString(json["validFrom"].toString(), Qt::ISODate);
    tw.validUntil = QDate::fromString(json["validUntil"].toString(), Qt::ISODate);

    return tw;
}

//=============================================================================
// DiscountRule Implementation
//=============================================================================

DiscountRule::DiscountRule(QObject* parent)
    : QObject(parent)
{
}

bool DiscountRule::appliesToFamily(int familyId) const {
    if (m_applicableFamilies.isEmpty()) return true;  // All families
    return m_applicableFamilies.contains(familyId);
}

bool DiscountRule::appliesToItem(int itemId) const {
    if (m_applicableItems.isEmpty()) return true;  // All items
    return m_applicableItems.contains(itemId);
}

bool DiscountRule::isValidNow() const {
    if (m_status != DiscountStatus::Active) return false;
    if (m_usageLimit > 0 && m_usageCount >= m_usageLimit) return false;
    return m_timeWindow.isValidNow();
}

int DiscountRule::calculateDiscount(int subtotal, int quantity) const {
    int discount = 0;

    switch (m_type) {
    case DiscountType::FixedAmount:
    case DiscountType::CouponAmount:
        discount = m_amount * quantity;
        break;

    case DiscountType::Percentage:
    case DiscountType::CheckPercentage:
    case DiscountType::CouponPercentage:
    case DiscountType::SeniorDiscount:
    case DiscountType::EmployeeDiscount:
    case DiscountType::ManagerDiscount:
    case DiscountType::HappyHour:
    case DiscountType::EarlyBird:
        // Percentage is in basis points (100 = 1%)
        discount = (subtotal * m_percentage) / 10000;
        break;

    case DiscountType::ItemFree:
    case DiscountType::CouponBOGO:
        // Full item price
        discount = subtotal;
        break;

    case DiscountType::ItemDiscount:
        // Could be amount or percentage depending on configuration
        if (m_percentage > 0) {
            discount = (subtotal * m_percentage) / 10000;
        } else {
            discount = m_amount;
        }
        break;

    case DiscountType::CheckAmount:
        discount = m_amount;
        break;

    case DiscountType::ComboPrice:
    case DiscountType::PackageDiscount:
        // Combo: discount is original - combo price
        discount = subtotal - m_amount;
        break;

    default:
        break;
    }

    // Apply maximum discount cap
    if (m_maxDiscount > 0 && discount > m_maxDiscount) {
        discount = m_maxDiscount;
    }

    // Can't discount more than the subtotal
    if (discount > subtotal) {
        discount = subtotal;
    }

    return discount;
}

QJsonObject DiscountRule::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["code"] = m_code;
    json["description"] = m_description;
    json["type"] = static_cast<int>(m_type);
    json["scope"] = static_cast<int>(m_scope);
    json["status"] = static_cast<int>(m_status);
    json["amount"] = m_amount;
    json["percentage"] = m_percentage;
    json["minPurchase"] = m_minPurchase;
    json["maxDiscount"] = m_maxDiscount;
    json["usageLimit"] = m_usageLimit;
    json["usageCount"] = m_usageCount;

    QJsonArray familiesArray;
    for (int fam : m_applicableFamilies) {
        familiesArray.append(fam);
    }
    json["applicableFamilies"] = familiesArray;

    QJsonArray itemsArray;
    for (int item : m_applicableItems) {
        itemsArray.append(item);
    }
    json["applicableItems"] = itemsArray;

    json["timeWindow"] = m_timeWindow.toJson();
    json["stackable"] = m_stackable;
    json["priority"] = m_priority;
    json["requiresCode"] = m_requiresCode;
    json["requiresApproval"] = m_requiresApproval;
    json["requiredSecurityLevel"] = m_requiredSecurityLevel;

    return json;
}

DiscountRule* DiscountRule::fromJson(const QJsonObject& json, QObject* parent) {
    auto* rule = new DiscountRule(parent);
    rule->m_id = json["id"].toInt();
    rule->m_name = json["name"].toString();
    rule->m_code = json["code"].toString();
    rule->m_description = json["description"].toString();
    rule->m_type = static_cast<DiscountType>(json["type"].toInt());
    rule->m_scope = static_cast<DiscountScope>(json["scope"].toInt());
    rule->m_status = static_cast<DiscountStatus>(json["status"].toInt());
    rule->m_amount = json["amount"].toInt();
    rule->m_percentage = json["percentage"].toInt();
    rule->m_minPurchase = json["minPurchase"].toInt();
    rule->m_maxDiscount = json["maxDiscount"].toInt();
    rule->m_usageLimit = json["usageLimit"].toInt();
    rule->m_usageCount = json["usageCount"].toInt();

    QJsonArray familiesArray = json["applicableFamilies"].toArray();
    for (const auto& ref : familiesArray) {
        rule->m_applicableFamilies.insert(ref.toInt());
    }

    QJsonArray itemsArray = json["applicableItems"].toArray();
    for (const auto& ref : itemsArray) {
        rule->m_applicableItems.insert(ref.toInt());
    }

    rule->m_timeWindow = TimeWindow::fromJson(json["timeWindow"].toObject());
    rule->m_stackable = json["stackable"].toBool();
    rule->m_priority = json["priority"].toInt();
    rule->m_requiresCode = json["requiresCode"].toBool();
    rule->m_requiresApproval = json["requiresApproval"].toBool();
    rule->m_requiredSecurityLevel = json["requiredSecurityLevel"].toInt();

    return rule;
}

//=============================================================================
// Coupon Implementation
//=============================================================================

Coupon::Coupon(QObject* parent)
    : QObject(parent)
{
}

bool Coupon::isExpired() const {
    if (!m_expiresOn.isValid()) return false;
    return QDate::currentDate() > m_expiresOn;
}

bool Coupon::isValid() const {
    return !m_redeemed && !isExpired();
}

QJsonObject Coupon::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["code"] = m_code;
    json["discountRuleId"] = m_discountRuleId;
    json["redeemed"] = m_redeemed;
    json["redeemedAt"] = m_redeemedAt.toString(Qt::ISODate);
    json["redeemedOnCheck"] = m_redeemedOnCheck;
    json["expiresOn"] = m_expiresOn.toString(Qt::ISODate);
    json["customerId"] = m_customerId;
    return json;
}

Coupon* Coupon::fromJson(const QJsonObject& json, QObject* parent) {
    auto* coupon = new Coupon(parent);
    coupon->m_id = json["id"].toInt();
    coupon->m_code = json["code"].toString();
    coupon->m_discountRuleId = json["discountRuleId"].toInt();
    coupon->m_redeemed = json["redeemed"].toBool();
    coupon->m_redeemedAt = QDateTime::fromString(json["redeemedAt"].toString(), Qt::ISODate);
    coupon->m_redeemedOnCheck = json["redeemedOnCheck"].toInt();
    coupon->m_expiresOn = QDate::fromString(json["expiresOn"].toString(), Qt::ISODate);
    coupon->m_customerId = json["customerId"].toInt();
    return coupon;
}

//=============================================================================
// AppliedDiscount Implementation
//=============================================================================

QJsonObject AppliedDiscount::toJson() const {
    QJsonObject json;
    json["id"] = id;
    json["discountRuleId"] = discountRuleId;
    json["couponId"] = couponId;
    json["discountName"] = discountName;
    json["type"] = static_cast<int>(type);
    json["checkId"] = checkId;
    json["subCheckId"] = subCheckId;
    json["itemId"] = itemId;
    json["originalAmount"] = originalAmount;
    json["discountAmount"] = discountAmount;
    json["finalAmount"] = finalAmount;
    json["appliedBy"] = appliedBy;
    json["approvedBy"] = approvedBy;
    json["appliedAt"] = appliedAt.toString(Qt::ISODate);
    json["couponCode"] = couponCode;
    json["notes"] = notes;
    return json;
}

AppliedDiscount AppliedDiscount::fromJson(const QJsonObject& json) {
    AppliedDiscount ad;
    ad.id = json["id"].toInt();
    ad.discountRuleId = json["discountRuleId"].toInt();
    ad.couponId = json["couponId"].toInt();
    ad.discountName = json["discountName"].toString();
    ad.type = static_cast<DiscountType>(json["type"].toInt());
    ad.checkId = json["checkId"].toInt();
    ad.subCheckId = json["subCheckId"].toInt();
    ad.itemId = json["itemId"].toInt();
    ad.originalAmount = json["originalAmount"].toInt();
    ad.discountAmount = json["discountAmount"].toInt();
    ad.finalAmount = json["finalAmount"].toInt();
    ad.appliedBy = json["appliedBy"].toInt();
    ad.approvedBy = json["approvedBy"].toInt();
    ad.appliedAt = QDateTime::fromString(json["appliedAt"].toString(), Qt::ISODate);
    ad.couponCode = json["couponCode"].toString();
    ad.notes = json["notes"].toString();
    return ad;
}

//=============================================================================
// DiscountManager Implementation
//=============================================================================

DiscountManager* DiscountManager::s_instance = nullptr;

DiscountManager::DiscountManager(QObject* parent)
    : QObject(parent)
{
}

DiscountManager* DiscountManager::instance() {
    if (!s_instance) {
        s_instance = new DiscountManager();
    }
    return s_instance;
}

DiscountRule* DiscountManager::createDiscount(const QString& name, DiscountType type) {
    auto* rule = new DiscountRule(this);
    rule->setId(m_nextDiscountId++);
    rule->setName(name);
    rule->setType(type);
    rule->setStatus(DiscountStatus::Active);

    m_discounts.append(rule);
    emit discountCreated(rule);

    return rule;
}

DiscountRule* DiscountManager::findDiscount(int id) {
    for (auto* rule : m_discounts) {
        if (rule->id() == id) return rule;
    }
    return nullptr;
}

DiscountRule* DiscountManager::findDiscountByCode(const QString& code) {
    for (auto* rule : m_discounts) {
        if (rule->code().compare(code, Qt::CaseInsensitive) == 0) {
            return rule;
        }
    }
    return nullptr;
}

QList<DiscountRule*> DiscountManager::allDiscounts() {
    return m_discounts;
}

QList<DiscountRule*> DiscountManager::activeDiscounts() {
    QList<DiscountRule*> result;
    for (auto* rule : m_discounts) {
        if (rule->isValidNow()) {
            result.append(rule);
        }
    }
    return result;
}

QList<DiscountRule*> DiscountManager::discountsForItem(int itemId) {
    QList<DiscountRule*> result;
    for (auto* rule : m_discounts) {
        if (rule->isValidNow() && rule->appliesToItem(itemId)) {
            result.append(rule);
        }
    }
    return result;
}

QList<DiscountRule*> DiscountManager::discountsForFamily(int familyId) {
    QList<DiscountRule*> result;
    for (auto* rule : m_discounts) {
        if (rule->isValidNow() && rule->appliesToFamily(familyId)) {
            result.append(rule);
        }
    }
    return result;
}

bool DiscountManager::deleteDiscount(int id) {
    for (int i = 0; i < m_discounts.size(); ++i) {
        if (m_discounts[i]->id() == id) {
            delete m_discounts.takeAt(i);
            emit discountDeleted(id);
            return true;
        }
    }
    return false;
}

void DiscountManager::createSeniorDiscount(int percentage) {
    auto* rule = createDiscount("Senior Discount", DiscountType::SeniorDiscount);
    rule->setPercentage(percentage * 100);  // Convert to basis points
    rule->setScope(DiscountScope::Check);
    rule->setRequiresApproval(false);
    rule->setCode("SENIOR");
}

void DiscountManager::createEmployeeDiscount(int percentage) {
    auto* rule = createDiscount("Employee Discount", DiscountType::EmployeeDiscount);
    rule->setPercentage(percentage * 100);
    rule->setScope(DiscountScope::Check);
    rule->setRequiresApproval(false);
    rule->setCode("EMPLOYEE");
}

void DiscountManager::createHappyHour(int percentage, const QTime& start, const QTime& end) {
    auto* rule = createDiscount("Happy Hour", DiscountType::HappyHour);
    rule->setPercentage(percentage * 100);
    rule->setScope(DiscountScope::Item);

    TimeWindow tw;
    tw.startTime = start;
    tw.endTime = end;
    rule->setTimeWindow(tw);
}

Coupon* DiscountManager::createCoupon(int discountRuleId, const QString& code) {
    auto* coupon = new Coupon(this);
    coupon->setId(m_nextCouponId++);
    coupon->setDiscountRuleId(discountRuleId);
    coupon->setCode(code.isEmpty() ? generateCouponCode() : code);

    // Default expiration: 90 days
    coupon->setExpiresOn(QDate::currentDate().addDays(90));

    m_coupons.append(coupon);
    emit couponCreated(coupon);

    return coupon;
}

Coupon* DiscountManager::findCoupon(const QString& code) {
    for (auto* coupon : m_coupons) {
        if (coupon->code().compare(code, Qt::CaseInsensitive) == 0) {
            return coupon;
        }
    }
    return nullptr;
}

QList<Coupon*> DiscountManager::couponsForCustomer(int customerId) {
    QList<Coupon*> result;
    for (auto* coupon : m_coupons) {
        if (coupon->customerId() == customerId && coupon->isValid()) {
            result.append(coupon);
        }
    }
    return result;
}

QString DiscountManager::generateCouponCode() {
    const QString chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString code;
    for (int i = 0; i < 8; ++i) {
        int idx = QRandomGenerator::global()->bounded(chars.length());
        code += chars[idx];
    }
    return code;
}

bool DiscountManager::validateCoupon(const QString& code) {
    auto* coupon = findCoupon(code);
    if (!coupon) return false;
    if (!coupon->isValid()) return false;

    auto* rule = findDiscount(coupon->discountRuleId());
    if (!rule) return false;
    if (!rule->isValidNow()) return false;

    return true;
}

AppliedDiscount DiscountManager::applyDiscount(int discountId, int checkId, int itemId,
                                                int employeeId) {
    AppliedDiscount ad;

    auto* rule = findDiscount(discountId);
    if (!rule) return ad;

    if (!rule->isValidNow()) return ad;

    ad.id = m_nextAppliedId++;
    ad.discountRuleId = discountId;
    ad.discountName = rule->name();
    ad.type = rule->type();
    ad.checkId = checkId;
    ad.itemId = itemId;
    ad.appliedBy = employeeId;
    ad.appliedAt = QDateTime::currentDateTime();

    // Calculate discount (would need actual amounts from check)
    // For now, just record the rule
    ad.discountAmount = 0;  // Would be calculated from actual check data

    rule->incrementUsage();
    m_appliedDiscounts.append(ad);

    emit discountApplied(ad);
    return ad;
}

AppliedDiscount DiscountManager::applyCoupon(const QString& couponCode, int checkId,
                                              int employeeId) {
    AppliedDiscount ad;

    auto* coupon = findCoupon(couponCode);
    if (!coupon || !coupon->isValid()) return ad;

    auto* rule = findDiscount(coupon->discountRuleId());
    if (!rule || !rule->isValidNow()) return ad;

    ad.id = m_nextAppliedId++;
    ad.discountRuleId = coupon->discountRuleId();
    ad.couponId = coupon->id();
    ad.discountName = rule->name();
    ad.type = rule->type();
    ad.checkId = checkId;
    ad.appliedBy = employeeId;
    ad.appliedAt = QDateTime::currentDateTime();
    ad.couponCode = couponCode;

    // Mark coupon as redeemed
    coupon->setRedeemed(true);
    coupon->setRedeemedAt(QDateTime::currentDateTime());
    coupon->setRedeemedOnCheck(checkId);

    rule->incrementUsage();
    m_appliedDiscounts.append(ad);

    emit couponRedeemed(couponCode);
    emit discountApplied(ad);

    return ad;
}

bool DiscountManager::removeDiscount(int appliedDiscountId) {
    for (int i = 0; i < m_appliedDiscounts.size(); ++i) {
        if (m_appliedDiscounts[i].id == appliedDiscountId) {
            emit discountRemoved(appliedDiscountId);
            m_appliedDiscounts.removeAt(i);
            return true;
        }
    }
    return false;
}

int DiscountManager::calculateItemDiscount(int discountId, int itemPrice, int quantity) {
    auto* rule = findDiscount(discountId);
    if (!rule) return 0;
    return rule->calculateDiscount(itemPrice, quantity);
}

int DiscountManager::calculateCheckDiscount(int discountId, int checkSubtotal) {
    auto* rule = findDiscount(discountId);
    if (!rule) return 0;

    if (rule->minPurchase() > 0 && checkSubtotal < rule->minPurchase()) {
        return 0;  // Doesn't meet minimum
    }

    return rule->calculateDiscount(checkSubtotal);
}

QList<DiscountRule*> DiscountManager::availableDiscountsForCheck(int checkId) {
    Q_UNUSED(checkId)

    // Get all active discounts that apply to checks
    QList<DiscountRule*> result;
    for (auto* rule : m_discounts) {
        if (rule->isValidNow() &&
            (rule->scope() == DiscountScope::Check || rule->scope() == DiscountScope::Order)) {
            result.append(rule);
        }
    }

    // Sort by priority
    std::sort(result.begin(), result.end(), [](const DiscountRule* a, const DiscountRule* b) {
        return a->priority() > b->priority();
    });

    return result;
}

QList<AppliedDiscount> DiscountManager::autoApplyDiscounts(int checkId) {
    QList<AppliedDiscount> applied;

    for (int discountId : m_autoApplyDiscounts) {
        auto* rule = findDiscount(discountId);
        if (rule && rule->isValidNow()) {
            AppliedDiscount ad = applyDiscount(discountId, checkId);
            if (ad.id > 0) {
                applied.append(ad);
            }
        }
    }

    return applied;
}

void DiscountManager::enableAutoApply(int discountId, bool enable) {
    if (enable) {
        m_autoApplyDiscounts.insert(discountId);
    } else {
        m_autoApplyDiscounts.remove(discountId);
    }
}

QList<AppliedDiscount> DiscountManager::discountsOnCheck(int checkId) {
    QList<AppliedDiscount> result;
    for (const auto& ad : m_appliedDiscounts) {
        if (ad.checkId == checkId) {
            result.append(ad);
        }
    }
    return result;
}

int DiscountManager::totalDiscountOnCheck(int checkId) {
    int total = 0;
    for (const auto& ad : m_appliedDiscounts) {
        if (ad.checkId == checkId) {
            total += ad.discountAmount;
        }
    }
    return total;
}

QJsonObject DiscountManager::dailyDiscountSummary(const QDate& date) {
    QJsonObject summary;
    summary["date"] = date.toString(Qt::ISODate);

    QMap<QString, int> discountCounts;
    QMap<QString, int> discountAmounts;
    int totalDiscounts = 0;
    int totalAmount = 0;

    for (const auto& ad : m_appliedDiscounts) {
        if (ad.appliedAt.date() != date) continue;

        discountCounts[ad.discountName]++;
        discountAmounts[ad.discountName] += ad.discountAmount;
        totalDiscounts++;
        totalAmount += ad.discountAmount;
    }

    QJsonObject counts;
    QJsonObject amounts;
    for (auto it = discountCounts.begin(); it != discountCounts.end(); ++it) {
        counts[it.key()] = it.value();
        amounts[it.key()] = discountAmounts[it.key()];
    }

    summary["counts"] = counts;
    summary["amounts"] = amounts;
    summary["totalDiscounts"] = totalDiscounts;
    summary["totalAmount"] = totalAmount;

    return summary;
}

QJsonObject DiscountManager::discountUsageReport(int discountId, const QDate& from,
                                                   const QDate& to) {
    QJsonObject report;
    report["discountId"] = discountId;
    report["dateFrom"] = from.toString(Qt::ISODate);
    report["dateTo"] = to.toString(Qt::ISODate);

    int usageCount = 0;
    int totalAmount = 0;

    for (const auto& ad : m_appliedDiscounts) {
        if (ad.discountRuleId != discountId) continue;

        QDate adDate = ad.appliedAt.date();
        if (adDate < from || adDate > to) continue;

        usageCount++;
        totalAmount += ad.discountAmount;
    }

    report["usageCount"] = usageCount;
    report["totalAmount"] = totalAmount;

    auto* rule = findDiscount(discountId);
    if (rule) {
        report["discountName"] = rule->name();
    }

    return report;
}

bool DiscountManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextDiscountId"] = m_nextDiscountId;
    root["nextCouponId"] = m_nextCouponId;
    root["nextAppliedId"] = m_nextAppliedId;

    QJsonArray discountsArray;
    for (const auto* rule : m_discounts) {
        discountsArray.append(rule->toJson());
    }
    root["discounts"] = discountsArray;

    QJsonArray couponsArray;
    for (const auto* coupon : m_coupons) {
        couponsArray.append(coupon->toJson());
    }
    root["coupons"] = couponsArray;

    QJsonArray appliedArray;
    for (const auto& ad : m_appliedDiscounts) {
        appliedArray.append(ad.toJson());
    }
    root["appliedDiscounts"] = appliedArray;

    QJsonArray autoArray;
    for (int id : m_autoApplyDiscounts) {
        autoArray.append(id);
    }
    root["autoApplyDiscounts"] = autoArray;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool DiscountManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();

    m_nextDiscountId = root["nextDiscountId"].toInt(1);
    m_nextCouponId = root["nextCouponId"].toInt(1);
    m_nextAppliedId = root["nextAppliedId"].toInt(1);

    qDeleteAll(m_discounts);
    m_discounts.clear();

    QJsonArray discountsArray = root["discounts"].toArray();
    for (const auto& ref : discountsArray) {
        auto* rule = DiscountRule::fromJson(ref.toObject(), this);
        m_discounts.append(rule);
    }

    qDeleteAll(m_coupons);
    m_coupons.clear();

    QJsonArray couponsArray = root["coupons"].toArray();
    for (const auto& ref : couponsArray) {
        auto* coupon = Coupon::fromJson(ref.toObject(), this);
        m_coupons.append(coupon);
    }

    m_appliedDiscounts.clear();
    QJsonArray appliedArray = root["appliedDiscounts"].toArray();
    for (const auto& ref : appliedArray) {
        m_appliedDiscounts.append(AppliedDiscount::fromJson(ref.toObject()));
    }

    m_autoApplyDiscounts.clear();
    QJsonArray autoArray = root["autoApplyDiscounts"].toArray();
    for (const auto& ref : autoArray) {
        m_autoApplyDiscounts.insert(ref.toInt());
    }

    return true;
}

//=============================================================================
// DiscountZone Implementation
//=============================================================================

DiscountZone::DiscountZone(QObject* parent)
    : QObject(parent)
{
}

QList<DiscountRule*> DiscountZone::availableDiscounts() {
    auto* manager = DiscountManager::instance();
    return manager->availableDiscountsForCheck(m_checkId);
}

void DiscountZone::selectDiscount(int discountId) {
    m_selectedDiscount = discountId;
}

void DiscountZone::enterCouponCode(const QString& code) {
    m_enteredCode = code;
}

void DiscountZone::applySelected() {
    auto* manager = DiscountManager::instance();

    if (!m_enteredCode.isEmpty()) {
        // Apply coupon
        if (manager->validateCoupon(m_enteredCode)) {
            auto ad = manager->applyCoupon(m_enteredCode, m_checkId, 0);
            if (ad.id > 0) {
                emit discountApplied(ad.id);
                m_enteredCode.clear();
                return;
            }
        }
        emit discountFailed("Invalid or expired coupon");
        return;
    }

    if (m_selectedDiscount > 0) {
        auto* rule = manager->findDiscount(m_selectedDiscount);
        if (rule && rule->requiresApproval()) {
            emit approvalRequired(m_selectedDiscount);
            return;
        }

        auto ad = manager->applyDiscount(m_selectedDiscount, m_checkId, m_selectedItem, 0);
        if (ad.id > 0) {
            emit discountApplied(ad.id);
            m_selectedDiscount = 0;
            return;
        }
        emit discountFailed("Unable to apply discount");
    }
}

void DiscountZone::removeDiscount(int appliedId) {
    auto* manager = DiscountManager::instance();
    manager->removeDiscount(appliedId);
}

void DiscountZone::applySeniorDiscount() {
    auto* manager = DiscountManager::instance();
    auto* rule = manager->findDiscountByCode("SENIOR");
    if (rule) {
        selectDiscount(rule->id());
        applySelected();
    }
}

void DiscountZone::applyEmployeeDiscount(int employeeId) {
    auto* manager = DiscountManager::instance();
    auto* rule = manager->findDiscountByCode("EMPLOYEE");
    if (rule) {
        auto ad = manager->applyDiscount(rule->id(), m_checkId, 0, employeeId);
        if (ad.id > 0) {
            emit discountApplied(ad.id);
        }
    }
}

void DiscountZone::applyManagerDiscount(int amount, int managerId) {
    auto* manager = DiscountManager::instance();

    // Create a one-time manager discount
    auto* rule = manager->createDiscount("Manager Discount", DiscountType::ManagerDiscount);
    rule->setAmount(amount);
    rule->setScope(DiscountScope::Check);
    rule->setUsageLimit(1);

    auto ad = manager->applyDiscount(rule->id(), m_checkId, 0, managerId);
    if (ad.id > 0) {
        emit discountApplied(ad.id);
    }
}

//=============================================================================
// CouponZone Implementation
//=============================================================================

CouponZone::CouponZone(QObject* parent)
    : QObject(parent)
{
}

void CouponZone::scanCoupon(const QString& code) {
    validateAndApply(code);
}

void CouponZone::manualEntry() {
    emit entryRequested();
}

void CouponZone::validateAndApply(const QString& code) {
    auto* manager = DiscountManager::instance();

    auto* coupon = manager->findCoupon(code);
    if (!coupon) {
        emit couponInvalid("Coupon not found");
        return;
    }

    if (!coupon->isValid()) {
        if (coupon->isRedeemed()) {
            emit couponInvalid("Coupon already redeemed");
        } else if (coupon->isExpired()) {
            emit couponInvalid("Coupon expired");
        } else {
            emit couponInvalid("Coupon not valid");
        }
        return;
    }

    auto* rule = manager->findDiscount(coupon->discountRuleId());
    if (!rule || !rule->isValidNow()) {
        emit couponInvalid("Discount not currently available");
        return;
    }

    emit couponValid(coupon, rule);

    // Auto-apply if we have a check
    if (m_checkId > 0) {
        auto ad = manager->applyCoupon(code, m_checkId);
        if (ad.id > 0) {
            emit couponApplied(ad.id);
        }
    }
}

void CouponZone::cancel() {
    // Reset state
}

} // namespace vt2
