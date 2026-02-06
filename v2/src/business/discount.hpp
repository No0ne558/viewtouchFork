// ViewTouch V2 - Discount/Coupon System
// Handles discounts, coupons, promotions with time-based and family-specific rules

#pragma once

#include <QObject>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QList>
#include <QSet>

namespace vt2 {

//=============================================================================
// Discount Types
//=============================================================================

enum class DiscountType {
    None = 0,
    
    // Amount-based
    FixedAmount,        // Fixed dollar amount off
    Percentage,         // Percentage off
    
    // Item-based
    ItemFree,           // Free item (BOGO)
    ItemDiscount,       // Discount on specific item
    
    // Check-based
    CheckPercentage,    // Percentage off entire check
    CheckAmount,        // Fixed amount off check
    
    // Special
    SeniorDiscount,     // Senior citizen discount
    EmployeeDiscount,   // Employee discount
    ManagerDiscount,    // Manager discretionary
    HappyHour,          // Happy hour pricing
    EarlyBird,          // Early bird special
    
    // Coupon types
    CouponAmount,       // Coupon for fixed amount
    CouponPercentage,   // Coupon for percentage
    CouponBOGO,         // Buy one get one
    
    // Combo/Package
    ComboPrice,         // Fixed combo price
    PackageDiscount     // Package deal discount
};

enum class DiscountScope {
    Item,               // Applies to single item
    Family,             // Applies to menu family
    SubCheck,           // Applies to subcheck
    Check,              // Applies to entire check
    Order               // Applies to order (all subchecks)
};

enum class DiscountStatus {
    Active,
    Inactive,
    Expired,
    Scheduled,
    UsageLimitReached
};

//=============================================================================
// TimeWindow - When discount is valid
//=============================================================================

struct TimeWindow {
    QTime startTime;
    QTime endTime;
    QList<Qt::DayOfWeek> validDays;  // Empty = all days
    QDate validFrom;
    QDate validUntil;

    bool isValidNow() const;
    bool isValidAt(const QDateTime& dt) const;

    QJsonObject toJson() const;
    static TimeWindow fromJson(const QJsonObject& json);
};

//=============================================================================
// DiscountRule - Single discount definition
//=============================================================================

class DiscountRule : public QObject {
    Q_OBJECT

    Q_PROPERTY(int id READ id WRITE setId NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(DiscountType type READ type WRITE setType NOTIFY changed)
    Q_PROPERTY(DiscountStatus status READ status WRITE setStatus NOTIFY changed)

public:
    explicit DiscountRule(QObject* parent = nullptr);

    // Identification
    int id() const { return m_id; }
    void setId(int id) { m_id = id; emit changed(); }

    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; emit changed(); }

    QString code() const { return m_code; }
    void setCode(const QString& code) { m_code = code; }

    QString description() const { return m_description; }
    void setDescription(const QString& desc) { m_description = desc; }

    // Type and scope
    DiscountType type() const { return m_type; }
    void setType(DiscountType t) { m_type = t; emit changed(); }

    DiscountScope scope() const { return m_scope; }
    void setScope(DiscountScope s) { m_scope = s; }

    DiscountStatus status() const { return m_status; }
    void setStatus(DiscountStatus s) { m_status = s; emit changed(); }

    // Discount value
    int amount() const { return m_amount; }          // In cents for fixed
    void setAmount(int cents) { m_amount = cents; }

    int percentage() const { return m_percentage; }  // In basis points (100 = 1%)
    void setPercentage(int bp) { m_percentage = bp; }

    // Limits
    int minPurchase() const { return m_minPurchase; }
    void setMinPurchase(int cents) { m_minPurchase = cents; }

    int maxDiscount() const { return m_maxDiscount; }
    void setMaxDiscount(int cents) { m_maxDiscount = cents; }

    int usageLimit() const { return m_usageLimit; }
    void setUsageLimit(int limit) { m_usageLimit = limit; }

    int usageCount() const { return m_usageCount; }
    void incrementUsage() { m_usageCount++; }

    // Applicability
    QSet<int> applicableFamilies() const { return m_applicableFamilies; }
    void setApplicableFamilies(const QSet<int>& families) { m_applicableFamilies = families; }
    void addApplicableFamily(int familyId) { m_applicableFamilies.insert(familyId); }
    bool appliesToFamily(int familyId) const;

    QSet<int> applicableItems() const { return m_applicableItems; }
    void setApplicableItems(const QSet<int>& items) { m_applicableItems = items; }
    void addApplicableItem(int itemId) { m_applicableItems.insert(itemId); }
    bool appliesToItem(int itemId) const;

    // Time restrictions
    TimeWindow timeWindow() const { return m_timeWindow; }
    void setTimeWindow(const TimeWindow& tw) { m_timeWindow = tw; }
    bool isValidNow() const;

    // Combining rules
    bool stackable() const { return m_stackable; }
    void setStackable(bool s) { m_stackable = s; }

    int priority() const { return m_priority; }  // Higher = applied first
    void setPriority(int p) { m_priority = p; }

    // Requirements
    bool requiresCode() const { return m_requiresCode; }
    void setRequiresCode(bool req) { m_requiresCode = req; }

    bool requiresApproval() const { return m_requiresApproval; }
    void setRequiresApproval(bool req) { m_requiresApproval = req; }

    int requiredSecurityLevel() const { return m_requiredSecurityLevel; }
    void setRequiredSecurityLevel(int level) { m_requiredSecurityLevel = level; }

    // Calculation
    int calculateDiscount(int subtotal, int quantity = 1) const;

    // JSON serialization
    QJsonObject toJson() const;
    static DiscountRule* fromJson(const QJsonObject& json, QObject* parent = nullptr);

signals:
    void changed();

private:
    int m_id = 0;
    QString m_name;
    QString m_code;
    QString m_description;

    DiscountType m_type = DiscountType::None;
    DiscountScope m_scope = DiscountScope::Item;
    DiscountStatus m_status = DiscountStatus::Active;

    int m_amount = 0;
    int m_percentage = 0;

    int m_minPurchase = 0;
    int m_maxDiscount = 0;
    int m_usageLimit = 0;
    int m_usageCount = 0;

    QSet<int> m_applicableFamilies;
    QSet<int> m_applicableItems;

    TimeWindow m_timeWindow;

    bool m_stackable = false;
    int m_priority = 0;

    bool m_requiresCode = false;
    bool m_requiresApproval = false;
    int m_requiredSecurityLevel = 0;
};

//=============================================================================
// Coupon - Single use coupon instance
//=============================================================================

class Coupon : public QObject {
    Q_OBJECT

public:
    explicit Coupon(QObject* parent = nullptr);

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }

    QString code() const { return m_code; }
    void setCode(const QString& code) { m_code = code; }

    int discountRuleId() const { return m_discountRuleId; }
    void setDiscountRuleId(int id) { m_discountRuleId = id; }

    // Validity
    bool isRedeemed() const { return m_redeemed; }
    void setRedeemed(bool r) { m_redeemed = r; }

    QDateTime redeemedAt() const { return m_redeemedAt; }
    void setRedeemedAt(const QDateTime& dt) { m_redeemedAt = dt; }

    int redeemedOnCheck() const { return m_redeemedOnCheck; }
    void setRedeemedOnCheck(int checkId) { m_redeemedOnCheck = checkId; }

    QDate expiresOn() const { return m_expiresOn; }
    void setExpiresOn(const QDate& date) { m_expiresOn = date; }

    bool isExpired() const;
    bool isValid() const;

    // Customer association
    int customerId() const { return m_customerId; }
    void setCustomerId(int id) { m_customerId = id; }

    // JSON serialization
    QJsonObject toJson() const;
    static Coupon* fromJson(const QJsonObject& json, QObject* parent = nullptr);

private:
    int m_id = 0;
    QString m_code;
    int m_discountRuleId = 0;

    bool m_redeemed = false;
    QDateTime m_redeemedAt;
    int m_redeemedOnCheck = 0;

    QDate m_expiresOn;
    int m_customerId = 0;
};

//=============================================================================
// AppliedDiscount - Record of discount applied to transaction
//=============================================================================

struct AppliedDiscount {
    int id = 0;
    int discountRuleId = 0;
    int couponId = 0;
    QString discountName;
    DiscountType type = DiscountType::None;

    int checkId = 0;
    int subCheckId = 0;
    int itemId = 0;

    int originalAmount = 0;      // Original item/check amount
    int discountAmount = 0;      // Discount given
    int finalAmount = 0;         // Final amount after discount

    int appliedBy = 0;           // Employee who applied
    int approvedBy = 0;          // Manager who approved (if needed)
    QDateTime appliedAt;

    QString couponCode;          // If coupon was used
    QString notes;

    QJsonObject toJson() const;
    static AppliedDiscount fromJson(const QJsonObject& json);
};

//=============================================================================
// DiscountManager - Central discount management
//=============================================================================

class DiscountManager : public QObject {
    Q_OBJECT

public:
    static DiscountManager* instance();

    // Discount rule management
    DiscountRule* createDiscount(const QString& name, DiscountType type);
    DiscountRule* findDiscount(int id);
    DiscountRule* findDiscountByCode(const QString& code);
    QList<DiscountRule*> allDiscounts();
    QList<DiscountRule*> activeDiscounts();
    QList<DiscountRule*> discountsForItem(int itemId);
    QList<DiscountRule*> discountsForFamily(int familyId);
    bool deleteDiscount(int id);

    // Pre-configured discounts
    void createSeniorDiscount(int percentage);
    void createEmployeeDiscount(int percentage);
    void createHappyHour(int percentage, const QTime& start, const QTime& end);

    // Coupon management
    Coupon* createCoupon(int discountRuleId, const QString& code = QString());
    Coupon* findCoupon(const QString& code);
    QList<Coupon*> couponsForCustomer(int customerId);
    QString generateCouponCode();
    bool validateCoupon(const QString& code);

    // Apply discounts
    AppliedDiscount applyDiscount(int discountId, int checkId, int itemId = 0,
                                   int employeeId = 0);
    AppliedDiscount applyCoupon(const QString& couponCode, int checkId,
                                 int employeeId = 0);
    bool removeDiscount(int appliedDiscountId);

    // Calculate potential discounts
    int calculateItemDiscount(int discountId, int itemPrice, int quantity = 1);
    int calculateCheckDiscount(int discountId, int checkSubtotal);
    QList<DiscountRule*> availableDiscountsForCheck(int checkId);

    // Auto-apply
    QList<AppliedDiscount> autoApplyDiscounts(int checkId);
    void enableAutoApply(int discountId, bool enable = true);

    // Queries
    QList<AppliedDiscount> discountsOnCheck(int checkId);
    int totalDiscountOnCheck(int checkId);

    // Reporting
    QJsonObject dailyDiscountSummary(const QDate& date);
    QJsonObject discountUsageReport(int discountId, const QDate& from, const QDate& to);

    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

signals:
    void discountCreated(DiscountRule* rule);
    void discountUpdated(int id);
    void discountDeleted(int id);
    void discountApplied(const AppliedDiscount& discount);
    void discountRemoved(int appliedId);
    void couponCreated(Coupon* coupon);
    void couponRedeemed(const QString& code);

private:
    explicit DiscountManager(QObject* parent = nullptr);
    static DiscountManager* s_instance;

    int m_nextDiscountId = 1;
    int m_nextCouponId = 1;
    int m_nextAppliedId = 1;

    QList<DiscountRule*> m_discounts;
    QList<Coupon*> m_coupons;
    QList<AppliedDiscount> m_appliedDiscounts;
    QSet<int> m_autoApplyDiscounts;
};

//=============================================================================
// DiscountZone - UI zone for applying discounts
//=============================================================================

class DiscountZone : public QObject {
    Q_OBJECT

    Q_PROPERTY(int checkId READ checkId WRITE setCheckId NOTIFY checkChanged)
    Q_PROPERTY(int selectedItem READ selectedItem WRITE setSelectedItem NOTIFY selectionChanged)

public:
    explicit DiscountZone(QObject* parent = nullptr);

    int checkId() const { return m_checkId; }
    void setCheckId(int id) { m_checkId = id; emit checkChanged(); }

    int selectedItem() const { return m_selectedItem; }
    void setSelectedItem(int id) { m_selectedItem = id; emit selectionChanged(); }

    // Operations
    Q_INVOKABLE QList<DiscountRule*> availableDiscounts();
    Q_INVOKABLE void selectDiscount(int discountId);
    Q_INVOKABLE void enterCouponCode(const QString& code);
    Q_INVOKABLE void applySelected();
    Q_INVOKABLE void removeDiscount(int appliedId);

    // Quick discounts
    Q_INVOKABLE void applySeniorDiscount();
    Q_INVOKABLE void applyEmployeeDiscount(int employeeId);
    Q_INVOKABLE void applyManagerDiscount(int amount, int managerId);

signals:
    void checkChanged();
    void selectionChanged();
    void discountApplied(int appliedId);
    void discountFailed(const QString& reason);
    void approvalRequired(int discountId);

private:
    int m_checkId = 0;
    int m_selectedItem = 0;
    int m_selectedDiscount = 0;
    QString m_enteredCode;
};

//=============================================================================
// CouponZone - UI zone for coupon management
//=============================================================================

class CouponZone : public QObject {
    Q_OBJECT

public:
    explicit CouponZone(QObject* parent = nullptr);

    Q_INVOKABLE void scanCoupon(const QString& code);
    Q_INVOKABLE void manualEntry();
    Q_INVOKABLE void validateAndApply(const QString& code);
    Q_INVOKABLE void cancel();

signals:
    void couponValid(Coupon* coupon, DiscountRule* discount);
    void couponInvalid(const QString& reason);
    void couponApplied(int appliedId);
    void entryRequested();

private:
    int m_checkId = 0;
};

} // namespace vt2
