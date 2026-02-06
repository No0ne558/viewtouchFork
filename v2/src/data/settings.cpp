// ViewTouch V2 - Settings System Implementation
// Modern C++/Qt6 reimplementation

#include "settings.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QLocale>

namespace vt2 {

Settings* Settings::s_instance = nullptr;

Settings::Settings(QObject* parent)
    : QObject(parent)
{
    // Initialize default shifts
    m_shifts.append({"Morning", 6, 0, 14, 0, true});
    m_shifts.append({"Afternoon", 14, 0, 22, 0, true});
    m_shifts.append({"Night", 22, 0, 6, 0, true});
    
    // Initialize default meal periods
    m_mealPeriods.append({"Breakfast", 6, 0, 11, 0, true});
    m_mealPeriods.append({"Lunch", 11, 0, 15, 0, true});
    m_mealPeriods.append({"Dinner", 15, 0, 22, 0, true});
    m_mealPeriods.append({"Late Night", 22, 0, 6, 0, true});
}

Settings* Settings::instance() {
    if (!s_instance) {
        s_instance = new Settings();
    }
    return s_instance;
}

void Settings::setStoreName(const QString& name) {
    if (m_storeName != name) {
        m_storeName = name;
        emit storeInfoChanged();
    }
}

void Settings::setShift(int index, const ShiftInfo& info) {
    if (index >= 0 && index < m_shifts.size()) {
        m_shifts[index] = info;
    } else if (index == m_shifts.size()) {
        m_shifts.append(info);
    }
    emit settingsChanged();
}

void Settings::setMealPeriod(int index, const MealPeriodInfo& info) {
    if (index >= 0 && index < m_mealPeriods.size()) {
        m_mealPeriods[index] = info;
    } else if (index == m_mealPeriods.size()) {
        m_mealPeriods.append(info);
    }
    emit settingsChanged();
}

QString Settings::formatMoney(int cents) const {
    QString sign = cents < 0 ? "-" : "";
    cents = qAbs(cents);
    
    int dollars = cents / 100;
    int c = cents % 100;
    
    QString dollarStr;
    if (m_numberFormat == NumberFormat::US) {
        dollarStr = QLocale(QLocale::English).toString(dollars);
        return QString("%1%2%3.%4")
            .arg(sign)
            .arg(m_moneySymbol)
            .arg(dollarStr)
            .arg(c, 2, 10, QChar('0'));
    } else {
        dollarStr = QLocale(QLocale::German).toString(dollars);
        return QString("%1%2%3,%4")
            .arg(sign)
            .arg(m_moneySymbol)
            .arg(dollarStr)
            .arg(c, 2, 10, QChar('0'));
    }
}

QString Settings::formatPercent(double value) const {
    if (m_numberFormat == NumberFormat::US) {
        return QString::number(value * 100, 'f', 2) + "%";
    } else {
        return QString::number(value * 100, 'f', 2).replace('.', ',') + "%";
    }
}

QString Settings::formatDate(const QDateTime& dt) const {
    if (m_dateFormat == DateFormat::US) {
        return dt.toString("MM/dd/yyyy");
    } else {
        return dt.toString("dd/MM/yyyy");
    }
}

QString Settings::formatTime(const QDateTime& dt) const {
    if (m_timeFormat == TimeFormat::Hour12) {
        return dt.toString("h:mm AP");
    } else {
        return dt.toString("HH:mm");
    }
}

QJsonObject Settings::toJson() const {
    QJsonObject json;
    
    // Store info
    json["storeName"] = m_storeName;
    json["storeAddress"] = m_storeAddress;
    json["storeAddress2"] = m_storeAddress2;
    json["storeCity"] = m_storeCity;
    json["storeState"] = m_storeState;
    json["storeZip"] = m_storeZip;
    json["storePhone"] = m_storePhone;
    
    // Formats
    json["dateFormat"] = static_cast<int>(m_dateFormat);
    json["numberFormat"] = static_cast<int>(m_numberFormat);
    json["timeFormat"] = static_cast<int>(m_timeFormat);
    json["measurementSystem"] = static_cast<int>(m_measurementSystem);
    json["moneySymbol"] = m_moneySymbol;
    
    // Tax
    json["taxRate"] = m_taxRate;
    json["foodTaxRate"] = m_foodTaxRate;
    json["alcoholTaxRate"] = m_alcoholTaxRate;
    json["merchandiseTaxRate"] = m_merchandiseTaxRate;
    json["roomTaxRate"] = m_roomTaxRate;
    
    // Gratuity
    json["autoGratuityRate"] = m_autoGratuityRate;
    json["autoGratuityGuests"] = m_autoGratuityGuests;
    
    // Drawer & Receipt
    json["drawerMode"] = static_cast<int>(m_drawerMode);
    json["receiptPrintMode"] = static_cast<int>(m_receiptPrintMode);
    json["receiptHeader"] = QJsonArray::fromStringList(m_receiptHeader);
    json["receiptFooter"] = QJsonArray::fromStringList(m_receiptFooter);
    
    // Rounding
    json["roundingMode"] = static_cast<int>(m_roundingMode);
    
    // Features
    json["useSeatOrdering"] = m_useSeatOrdering;
    json["usePasswords"] = m_usePasswords;
    json["discountAlcohol"] = m_discountAlcohol;
    json["changeForChecks"] = m_changeForChecks;
    json["changeForCredit"] = m_changeForCredit;
    json["changeForGift"] = m_changeForGift;
    json["open24Hours"] = m_open24Hours;
    json["allowMultipleCoupons"] = m_allowMultipleCoupons;
    json["showButtonImages"] = m_showButtonImages;
    
    // Shifts
    QJsonArray shiftArray;
    for (const auto& shift : m_shifts) {
        QJsonObject shiftObj;
        shiftObj["name"] = shift.name;
        shiftObj["startHour"] = shift.startHour;
        shiftObj["startMinute"] = shift.startMinute;
        shiftObj["endHour"] = shift.endHour;
        shiftObj["endMinute"] = shift.endMinute;
        shiftObj["active"] = shift.active;
        shiftArray.append(shiftObj);
    }
    json["shifts"] = shiftArray;
    
    // Meal Periods
    QJsonArray mealArray;
    for (const auto& meal : m_mealPeriods) {
        QJsonObject mealObj;
        mealObj["name"] = meal.name;
        mealObj["startHour"] = meal.startHour;
        mealObj["startMinute"] = meal.startMinute;
        mealObj["endHour"] = meal.endHour;
        mealObj["endMinute"] = meal.endMinute;
        mealObj["active"] = meal.active;
        mealArray.append(mealObj);
    }
    json["mealPeriods"] = mealArray;
    
    return json;
}

void Settings::fromJson(const QJsonObject& json) {
    // Store info
    m_storeName = json["storeName"].toString("ViewTouch Restaurant");
    m_storeAddress = json["storeAddress"].toString();
    m_storeAddress2 = json["storeAddress2"].toString();
    m_storeCity = json["storeCity"].toString();
    m_storeState = json["storeState"].toString();
    m_storeZip = json["storeZip"].toString();
    m_storePhone = json["storePhone"].toString();
    
    // Formats
    m_dateFormat = static_cast<DateFormat>(json["dateFormat"].toInt(1));
    m_numberFormat = static_cast<NumberFormat>(json["numberFormat"].toInt(1));
    m_timeFormat = static_cast<TimeFormat>(json["timeFormat"].toInt(1));
    m_measurementSystem = static_cast<MeasurementSystem>(json["measurementSystem"].toInt(1));
    m_moneySymbol = json["moneySymbol"].toString("$");
    
    // Tax
    m_taxRate = json["taxRate"].toDouble();
    m_foodTaxRate = json["foodTaxRate"].toDouble();
    m_alcoholTaxRate = json["alcoholTaxRate"].toDouble();
    m_merchandiseTaxRate = json["merchandiseTaxRate"].toDouble();
    m_roomTaxRate = json["roomTaxRate"].toDouble();
    
    // Gratuity
    m_autoGratuityRate = json["autoGratuityRate"].toDouble(0.18);
    m_autoGratuityGuests = json["autoGratuityGuests"].toInt(8);
    
    // Drawer & Receipt
    m_drawerMode = static_cast<DrawerMode>(json["drawerMode"].toInt());
    m_receiptPrintMode = static_cast<ReceiptPrintMode>(json["receiptPrintMode"].toInt(2));
    
    m_receiptHeader.clear();
    QJsonArray headerArray = json["receiptHeader"].toArray();
    for (const auto& h : headerArray) {
        m_receiptHeader.append(h.toString());
    }
    
    m_receiptFooter.clear();
    QJsonArray footerArray = json["receiptFooter"].toArray();
    for (const auto& f : footerArray) {
        m_receiptFooter.append(f.toString());
    }
    
    // Rounding
    m_roundingMode = static_cast<RoundingMode>(json["roundingMode"].toInt());
    
    // Features
    m_useSeatOrdering = json["useSeatOrdering"].toBool(false);
    m_usePasswords = json["usePasswords"].toBool(true);
    m_discountAlcohol = json["discountAlcohol"].toBool(false);
    m_changeForChecks = json["changeForChecks"].toBool(true);
    m_changeForCredit = json["changeForCredit"].toBool(false);
    m_changeForGift = json["changeForGift"].toBool(true);
    m_open24Hours = json["open24Hours"].toBool(false);
    m_allowMultipleCoupons = json["allowMultipleCoupons"].toBool(false);
    m_showButtonImages = json["showButtonImages"].toBool(true);
    
    // Shifts
    m_shifts.clear();
    QJsonArray shiftArray = json["shifts"].toArray();
    for (const auto& shiftRef : shiftArray) {
        QJsonObject shiftObj = shiftRef.toObject();
        ShiftInfo shift;
        shift.name = shiftObj["name"].toString();
        shift.startHour = shiftObj["startHour"].toInt();
        shift.startMinute = shiftObj["startMinute"].toInt();
        shift.endHour = shiftObj["endHour"].toInt();
        shift.endMinute = shiftObj["endMinute"].toInt();
        shift.active = shiftObj["active"].toBool(true);
        m_shifts.append(shift);
    }
    
    // Meal Periods
    m_mealPeriods.clear();
    QJsonArray mealArray = json["mealPeriods"].toArray();
    for (const auto& mealRef : mealArray) {
        QJsonObject mealObj = mealRef.toObject();
        MealPeriodInfo meal;
        meal.name = mealObj["name"].toString();
        meal.startHour = mealObj["startHour"].toInt();
        meal.startMinute = mealObj["startMinute"].toInt();
        meal.endHour = mealObj["endHour"].toInt();
        meal.endMinute = mealObj["endMinute"].toInt();
        meal.active = mealObj["active"].toBool(true);
        m_mealPeriods.append(meal);
    }
    
    emit settingsChanged();
}

bool Settings::saveToFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(toJson());
    file.write(doc.toJson());
    return true;
}

bool Settings::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    fromJson(doc.object());
    return true;
}

} // namespace vt2
