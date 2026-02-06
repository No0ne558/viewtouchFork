// ViewTouch V2 - Settings System
// Modern C++/Qt6 reimplementation

#ifndef VT2_SETTINGS_HPP
#define VT2_SETTINGS_HPP

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QSettings>
#include <QVariant>
#include <array>

namespace vt2 {

// Date Format
enum class DateFormat {
    US = 1,     // MM/DD/YYYY
    Euro = 2    // DD/MM/YYYY
};

// Number Format
enum class NumberFormat {
    US = 1,     // 1,234.56
    Euro = 2    // 1.234,56
};

// Time Format
enum class TimeFormat {
    Hour12 = 1,
    Hour24 = 2
};

// Drawer Mode
enum class DrawerMode {
    Normal = 0,     // Unrestricted access
    Assigned = 1,   // Drawers must be assigned
    Server = 2      // Each server has their own drawer
};

// Receipt Print Mode
enum class ReceiptPrintMode {
    None = 0,
    OnSend = 1,
    OnFinalize = 2,
    Both = 3
};

// Rounding Mode
enum class RoundingMode {
    None = 0,
    DropPennies = 1,
    UpGratuity = 2
};

// Measurement System
enum class MeasurementSystem {
    Standard = 1,   // Imperial
    Metric = 2
};

//=============================================================================
// TaxInfo - Tax rate configuration
//=============================================================================
struct TaxInfo {
    QString name;
    double rate = 0.0;      // As decimal (0.08 = 8%)
    bool appliesToFood = true;
    bool appliesToAlcohol = true;
    bool appliesToMerchandise = true;
};

//=============================================================================
// ShiftInfo - Shift time configuration
//=============================================================================
struct ShiftInfo {
    QString name;
    int startHour = 0;
    int startMinute = 0;
    int endHour = 23;
    int endMinute = 59;
    bool active = true;
};

//=============================================================================
// MealPeriodInfo - Meal period configuration
//=============================================================================
struct MealPeriodInfo {
    QString name;
    int startHour = 0;
    int startMinute = 0;
    int endHour = 23;
    int endMinute = 59;
    bool active = true;
};

//=============================================================================
// Settings - System configuration
//=============================================================================
class Settings : public QObject {
    Q_OBJECT

public:
    static Settings* instance();
    
    // Store Information
    QString storeName() const { return m_storeName; }
    void setStoreName(const QString& name);
    
    QString storeAddress() const { return m_storeAddress; }
    void setStoreAddress(const QString& addr) { m_storeAddress = addr; }
    
    QString storeAddress2() const { return m_storeAddress2; }
    void setStoreAddress2(const QString& addr) { m_storeAddress2 = addr; }
    
    QString storeCity() const { return m_storeCity; }
    void setStoreCity(const QString& city) { m_storeCity = city; }
    
    QString storeState() const { return m_storeState; }
    void setStoreState(const QString& state) { m_storeState = state; }
    
    QString storeZip() const { return m_storeZip; }
    void setStoreZip(const QString& zip) { m_storeZip = zip; }
    
    QString storePhone() const { return m_storePhone; }
    void setStorePhone(const QString& phone) { m_storePhone = phone; }
    
    // Format Settings
    DateFormat dateFormat() const { return m_dateFormat; }
    void setDateFormat(DateFormat fmt) { m_dateFormat = fmt; }
    
    NumberFormat numberFormat() const { return m_numberFormat; }
    void setNumberFormat(NumberFormat fmt) { m_numberFormat = fmt; }
    
    TimeFormat timeFormat() const { return m_timeFormat; }
    void setTimeFormat(TimeFormat fmt) { m_timeFormat = fmt; }
    
    MeasurementSystem measurementSystem() const { return m_measurementSystem; }
    void setMeasurementSystem(MeasurementSystem sys) { m_measurementSystem = sys; }
    
    QString moneySymbol() const { return m_moneySymbol; }
    void setMoneySymbol(const QString& sym) { m_moneySymbol = sym; }
    
    // Tax Settings
    double taxRate() const { return m_taxRate; }
    void setTaxRate(double rate) { m_taxRate = rate; }
    
    double foodTaxRate() const { return m_foodTaxRate; }
    void setFoodTaxRate(double rate) { m_foodTaxRate = rate; }
    
    double alcoholTaxRate() const { return m_alcoholTaxRate; }
    void setAlcoholTaxRate(double rate) { m_alcoholTaxRate = rate; }
    
    double merchandiseTaxRate() const { return m_merchandiseTaxRate; }
    void setMerchandiseTaxRate(double rate) { m_merchandiseTaxRate = rate; }
    
    double roomTaxRate() const { return m_roomTaxRate; }
    void setRoomTaxRate(double rate) { m_roomTaxRate = rate; }
    
    // Gratuity
    double autoGratuityRate() const { return m_autoGratuityRate; }
    void setAutoGratuityRate(double rate) { m_autoGratuityRate = rate; }
    
    int autoGratuityGuests() const { return m_autoGratuityGuests; }
    void setAutoGratuityGuests(int count) { m_autoGratuityGuests = count; }
    
    // Drawer Settings
    DrawerMode drawerMode() const { return m_drawerMode; }
    void setDrawerMode(DrawerMode mode) { m_drawerMode = mode; }
    
    // Receipt Settings
    ReceiptPrintMode receiptPrintMode() const { return m_receiptPrintMode; }
    void setReceiptPrintMode(ReceiptPrintMode mode) { m_receiptPrintMode = mode; }
    
    QStringList receiptHeader() const { return m_receiptHeader; }
    void setReceiptHeader(const QStringList& header) { m_receiptHeader = header; }
    
    QStringList receiptFooter() const { return m_receiptFooter; }
    void setReceiptFooter(const QStringList& footer) { m_receiptFooter = footer; }
    
    // Rounding
    RoundingMode roundingMode() const { return m_roundingMode; }
    void setRoundingMode(RoundingMode mode) { m_roundingMode = mode; }
    
    // Feature toggles
    bool useSeatOrdering() const { return m_useSeatOrdering; }
    void setUseSeatOrdering(bool use) { m_useSeatOrdering = use; }
    
    bool usePasswords() const { return m_usePasswords; }
    void setUsePasswords(bool use) { m_usePasswords = use; }
    
    bool discountAlcohol() const { return m_discountAlcohol; }
    void setDiscountAlcohol(bool allow) { m_discountAlcohol = allow; }
    
    bool changeForChecks() const { return m_changeForChecks; }
    void setChangeForChecks(bool allow) { m_changeForChecks = allow; }
    
    bool changeForCredit() const { return m_changeForCredit; }
    void setChangeForCredit(bool allow) { m_changeForCredit = allow; }
    
    bool changeForGift() const { return m_changeForGift; }
    void setChangeForGift(bool allow) { m_changeForGift = allow; }
    
    bool open24Hours() const { return m_open24Hours; }
    void setOpen24Hours(bool open) { m_open24Hours = open; }
    
    bool allowMultipleCoupons() const { return m_allowMultipleCoupons; }
    void setAllowMultipleCoupons(bool allow) { m_allowMultipleCoupons = allow; }
    
    bool showButtonImages() const { return m_showButtonImages; }
    void setShowButtonImages(bool show) { m_showButtonImages = show; }
    
    // Shifts
    int shiftCount() const { return m_shifts.size(); }
    ShiftInfo shift(int index) const { return m_shifts.value(index); }
    void setShift(int index, const ShiftInfo& info);
    
    // Meal Periods
    int mealPeriodCount() const { return m_mealPeriods.size(); }
    MealPeriodInfo mealPeriod(int index) const { return m_mealPeriods.value(index); }
    void setMealPeriod(int index, const MealPeriodInfo& info);
    
    // Currency formatting
    QString formatMoney(int cents) const;
    QString formatPercent(double value) const;
    QString formatDate(const QDateTime& dt) const;
    QString formatTime(const QDateTime& dt) const;
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

signals:
    void settingsChanged();
    void storeInfoChanged();

private:
    explicit Settings(QObject* parent = nullptr);
    static Settings* s_instance;
    
    // Store Info
    QString m_storeName = "ViewTouch Restaurant";
    QString m_storeAddress;
    QString m_storeAddress2;
    QString m_storeCity;
    QString m_storeState;
    QString m_storeZip;
    QString m_storePhone;
    
    // Formats
    DateFormat m_dateFormat = DateFormat::US;
    NumberFormat m_numberFormat = NumberFormat::US;
    TimeFormat m_timeFormat = TimeFormat::Hour12;
    MeasurementSystem m_measurementSystem = MeasurementSystem::Standard;
    QString m_moneySymbol = "$";
    
    // Tax
    double m_taxRate = 0.0;
    double m_foodTaxRate = 0.0;
    double m_alcoholTaxRate = 0.0;
    double m_merchandiseTaxRate = 0.0;
    double m_roomTaxRate = 0.0;
    
    // Gratuity
    double m_autoGratuityRate = 0.18;
    int m_autoGratuityGuests = 8;
    
    // Drawer
    DrawerMode m_drawerMode = DrawerMode::Normal;
    
    // Receipt
    ReceiptPrintMode m_receiptPrintMode = ReceiptPrintMode::OnFinalize;
    QStringList m_receiptHeader;
    QStringList m_receiptFooter;
    
    // Rounding
    RoundingMode m_roundingMode = RoundingMode::None;
    
    // Features
    bool m_useSeatOrdering = false;
    bool m_usePasswords = true;
    bool m_discountAlcohol = false;
    bool m_changeForChecks = true;
    bool m_changeForCredit = false;
    bool m_changeForGift = true;
    bool m_open24Hours = false;
    bool m_allowMultipleCoupons = false;
    bool m_showButtonImages = true;
    
    // Shifts & Meal Periods
    QList<ShiftInfo> m_shifts;
    QList<MealPeriodInfo> m_mealPeriods;
};

} // namespace vt2

#endif // VT2_SETTINGS_HPP
