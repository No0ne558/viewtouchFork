// ViewTouch V2 - Tax System
// Multi-tax support (GST/PST/HST/VAT etc.)

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QJsonObject>

namespace vt2 {

//=============================================================================
// Tax Type
//=============================================================================

enum class TaxType {
    None = 0,
    Standard,       // Standard sales tax
    GST,            // Canadian Goods & Services Tax
    PST,            // Provincial Sales Tax
    HST,            // Harmonized Sales Tax
    QST,            // Quebec Sales Tax
    VAT,            // Value Added Tax (European)
    Liquor,         // Alcohol tax
    Food,           // Food tax (often lower rate)
    Takeout,        // Takeout/delivery tax
    RoomService,    // Hotel room service tax
    Gratuity,       // Auto-gratuity tax
    Custom1,
    Custom2,
    Custom3,
    Custom4
};

//=============================================================================
// TaxRate - Individual tax rate configuration
//=============================================================================

class TaxRate : public QObject {
    Q_OBJECT

public:
    explicit TaxRate(QObject* parent = nullptr);
    ~TaxRate() override = default;

    // Identification
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }

    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    TaxType type() const { return m_type; }
    void setType(TaxType type) { m_type = type; }

    // Rate (in basis points: 1000 = 10.00%)
    int rateBasisPoints() const { return m_rate; }
    void setRateBasisPoints(int bp) { m_rate = bp; }

    double ratePercent() const { return m_rate / 100.0; }
    void setRatePercent(double pct) { m_rate = static_cast<int>(pct * 100); }

    // Calculate tax
    int calculate(int amountCents) const;

    // Applicability
    bool appliesToFood() const { return m_appliesToFood; }
    void setAppliesToFood(bool applies) { m_appliesToFood = applies; }

    bool appliesToBeverage() const { return m_appliesToBeverage; }
    void setAppliesToBeverage(bool applies) { m_appliesToBeverage = applies; }

    bool appliesToAlcohol() const { return m_appliesToAlcohol; }
    void setAppliesToAlcohol(bool applies) { m_appliesToAlcohol = applies; }

    bool appliesToMerchandise() const { return m_appliesToMerchandise; }
    void setAppliesToMerchandise(bool applies) { m_appliesToMerchandise = applies; }

    bool appliesToService() const { return m_appliesToService; }
    void setAppliesToService(bool applies) { m_appliesToService = applies; }

    // Exemptions
    bool exemptTakeout() const { return m_exemptTakeout; }
    void setExemptTakeout(bool exempt) { m_exemptTakeout = exempt; }

    bool exemptEmployee() const { return m_exemptEmployee; }
    void setExemptEmployee(bool exempt) { m_exemptEmployee = exempt; }

    // Active flag
    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }

    // Tax-on-tax (for compound taxes like GST+PST)
    bool includeInBase() const { return m_includeInBase; }
    void setIncludeInBase(bool include) { m_includeInBase = include; }

    // Display order
    int displayOrder() const { return m_displayOrder; }
    void setDisplayOrder(int order) { m_displayOrder = order; }

    // Serialization
    QJsonObject toJson() const;
    static TaxRate* fromJson(const QJsonObject& json, QObject* parent = nullptr);

private:
    int m_id = 0;
    QString m_name;
    TaxType m_type = TaxType::Standard;
    int m_rate = 0;  // basis points

    bool m_appliesToFood = true;
    bool m_appliesToBeverage = true;
    bool m_appliesToAlcohol = true;
    bool m_appliesToMerchandise = true;
    bool m_appliesToService = true;

    bool m_exemptTakeout = false;
    bool m_exemptEmployee = false;

    bool m_active = true;
    bool m_includeInBase = false;
    int m_displayOrder = 0;
};

//=============================================================================
// TaxResult - Result of tax calculation
//=============================================================================

struct TaxResult {
    int taxRateId = 0;
    QString taxName;
    int taxableAmount = 0;  // cents
    int taxAmount = 0;      // cents
    int rateBasisPoints = 0;
};

//=============================================================================
// TaxBreakdown - Complete tax breakdown for an item/check
//=============================================================================

struct TaxBreakdown {
    int subtotal = 0;           // Pre-tax amount
    int totalTax = 0;           // Sum of all taxes
    int grandTotal = 0;         // Subtotal + tax
    QList<TaxResult> taxes;     // Individual tax amounts

    void clear() {
        subtotal = 0;
        totalTax = 0;
        grandTotal = 0;
        taxes.clear();
    }
};

//=============================================================================
// ItemTaxClass - Tax classification for menu items
//=============================================================================

enum class ItemTaxClass {
    Default = 0,    // Use default tax rules
    Food,           // Food item
    Beverage,       // Non-alcoholic beverage
    Alcohol,        // Alcoholic beverage
    Merchandise,    // Retail merchandise
    Service,        // Service charge
    NonTaxable,     // Exempt from all taxes
    Custom1,
    Custom2,
    Custom3
};

//=============================================================================
// TaxManager - Singleton for tax calculations
//=============================================================================

class TaxManager : public QObject {
    Q_OBJECT

public:
    static TaxManager* instance();

    // Tax rate management
    void addTaxRate(TaxRate* rate);
    void removeTaxRate(int id);
    TaxRate* findTaxRate(int id);
    TaxRate* findTaxRateByType(TaxType type);
    QList<TaxRate*> allTaxRates() const { return m_rates; }
    QList<TaxRate*> activeTaxRates() const;

    // Calculate taxes
    TaxBreakdown calculateTax(int amountCents, ItemTaxClass itemClass,
                              bool isTakeout = false, bool isEmployee = false);

    // Calculate tax for multiple items
    TaxBreakdown calculateTaxForItems(const QList<QPair<int, ItemTaxClass>>& items,
                                       bool isTakeout = false, bool isEmployee = false);

    // Rounding options
    enum class RoundingMode { 
        Standard,       // Normal rounding
        RoundUp,        // Always round up
        RoundDown,      // Always round down
        DropPennies     // Round to nearest nickel
    };

    void setRoundingMode(RoundingMode mode) { m_roundingMode = mode; }
    RoundingMode roundingMode() const { return m_roundingMode; }

    // Tax-inclusive pricing
    void setTaxInclusive(bool inclusive) { m_taxInclusive = inclusive; }
    bool isTaxInclusive() const { return m_taxInclusive; }

    // Extract tax from inclusive price
    TaxBreakdown extractTaxFromInclusive(int inclusivePrice, ItemTaxClass itemClass);

    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

signals:
    void taxRatesChanged();

private:
    TaxManager(QObject* parent = nullptr);
    static TaxManager* s_instance;

    QList<TaxRate*> m_rates;
    int m_nextId = 1;
    RoundingMode m_roundingMode = RoundingMode::Standard;
    bool m_taxInclusive = false;

    int applyRounding(int cents) const;
};

//=============================================================================
// TaxExemption - Tax exemption certificate
//=============================================================================

class TaxExemption : public QObject {
    Q_OBJECT

public:
    explicit TaxExemption(QObject* parent = nullptr);

    int id() const { return m_id; }
    void setId(int id) { m_id = id; }

    QString certificateNumber() const { return m_certNumber; }
    void setCertificateNumber(const QString& num) { m_certNumber = num; }

    QString holderName() const { return m_holderName; }
    void setHolderName(const QString& name) { m_holderName = name; }

    // Which taxes are exempt
    QList<int> exemptTaxIds() const { return m_exemptTaxIds; }
    void setExemptTaxIds(const QList<int>& ids) { m_exemptTaxIds = ids; }

    // Validity
    QDate validFrom() const { return m_validFrom; }
    void setValidFrom(const QDate& date) { m_validFrom = date; }

    QDate validTo() const { return m_validTo; }
    void setValidTo(const QDate& date) { m_validTo = date; }

    bool isValid() const;

    // Serialization
    QJsonObject toJson() const;
    static TaxExemption* fromJson(const QJsonObject& json, QObject* parent = nullptr);

private:
    int m_id = 0;
    QString m_certNumber;
    QString m_holderName;
    QList<int> m_exemptTaxIds;
    QDate m_validFrom;
    QDate m_validTo;
};

} // namespace vt2
