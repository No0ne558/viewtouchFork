// ViewTouch V2 - Tax System Implementation

#include "tax.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <cmath>

namespace vt2 {

//=============================================================================
// TaxRate Implementation
//=============================================================================

TaxRate::TaxRate(QObject* parent)
    : QObject(parent)
{
}

int TaxRate::calculate(int amountCents) const {
    if (!m_active || m_rate == 0) {
        return 0;
    }
    // rate is in basis points (1000 = 10.00%)
    // amount * rate / 10000 = tax
    return static_cast<int>(std::round(static_cast<double>(amountCents) * m_rate / 10000.0));
}

QJsonObject TaxRate::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["type"] = static_cast<int>(m_type);
    json["rate"] = m_rate;
    json["appliesToFood"] = m_appliesToFood;
    json["appliesToBeverage"] = m_appliesToBeverage;
    json["appliesToAlcohol"] = m_appliesToAlcohol;
    json["appliesToMerchandise"] = m_appliesToMerchandise;
    json["appliesToService"] = m_appliesToService;
    json["exemptTakeout"] = m_exemptTakeout;
    json["exemptEmployee"] = m_exemptEmployee;
    json["active"] = m_active;
    json["includeInBase"] = m_includeInBase;
    json["displayOrder"] = m_displayOrder;
    return json;
}

TaxRate* TaxRate::fromJson(const QJsonObject& json, QObject* parent) {
    auto* rate = new TaxRate(parent);
    rate->m_id = json["id"].toInt();
    rate->m_name = json["name"].toString();
    rate->m_type = static_cast<TaxType>(json["type"].toInt());
    rate->m_rate = json["rate"].toInt();
    rate->m_appliesToFood = json["appliesToFood"].toBool(true);
    rate->m_appliesToBeverage = json["appliesToBeverage"].toBool(true);
    rate->m_appliesToAlcohol = json["appliesToAlcohol"].toBool(true);
    rate->m_appliesToMerchandise = json["appliesToMerchandise"].toBool(true);
    rate->m_appliesToService = json["appliesToService"].toBool(true);
    rate->m_exemptTakeout = json["exemptTakeout"].toBool(false);
    rate->m_exemptEmployee = json["exemptEmployee"].toBool(false);
    rate->m_active = json["active"].toBool(true);
    rate->m_includeInBase = json["includeInBase"].toBool(false);
    rate->m_displayOrder = json["displayOrder"].toInt();
    return rate;
}

//=============================================================================
// TaxManager Implementation
//=============================================================================

TaxManager* TaxManager::s_instance = nullptr;

TaxManager::TaxManager(QObject* parent)
    : QObject(parent)
{
}

TaxManager* TaxManager::instance() {
    if (!s_instance) {
        s_instance = new TaxManager();
    }
    return s_instance;
}

void TaxManager::addTaxRate(TaxRate* rate) {
    if (rate->id() == 0) {
        rate->setId(m_nextId++);
    }
    rate->setParent(this);
    m_rates.append(rate);
    emit taxRatesChanged();
}

void TaxManager::removeTaxRate(int id) {
    for (int i = 0; i < m_rates.size(); ++i) {
        if (m_rates[i]->id() == id) {
            delete m_rates.takeAt(i);
            emit taxRatesChanged();
            return;
        }
    }
}

TaxRate* TaxManager::findTaxRate(int id) {
    for (auto* rate : m_rates) {
        if (rate->id() == id) return rate;
    }
    return nullptr;
}

TaxRate* TaxManager::findTaxRateByType(TaxType type) {
    for (auto* rate : m_rates) {
        if (rate->type() == type && rate->isActive()) return rate;
    }
    return nullptr;
}

QList<TaxRate*> TaxManager::activeTaxRates() const {
    QList<TaxRate*> active;
    for (auto* rate : m_rates) {
        if (rate->isActive()) {
            active.append(rate);
        }
    }
    // Sort by display order
    std::sort(active.begin(), active.end(), [](TaxRate* a, TaxRate* b) {
        return a->displayOrder() < b->displayOrder();
    });
    return active;
}

TaxBreakdown TaxManager::calculateTax(int amountCents, ItemTaxClass itemClass,
                                       bool isTakeout, bool isEmployee) {
    TaxBreakdown breakdown;
    breakdown.subtotal = amountCents;

    if (itemClass == ItemTaxClass::NonTaxable) {
        breakdown.grandTotal = amountCents;
        return breakdown;
    }

    int taxableBase = amountCents;

    for (auto* rate : activeTaxRates()) {
        // Check applicability based on item class
        bool applies = false;
        switch (itemClass) {
        case ItemTaxClass::Food:
            applies = rate->appliesToFood();
            break;
        case ItemTaxClass::Beverage:
            applies = rate->appliesToBeverage();
            break;
        case ItemTaxClass::Alcohol:
            applies = rate->appliesToAlcohol();
            break;
        case ItemTaxClass::Merchandise:
            applies = rate->appliesToMerchandise();
            break;
        case ItemTaxClass::Service:
            applies = rate->appliesToService();
            break;
        default:
            applies = true;  // Default applies to all
            break;
        }

        if (!applies) continue;

        // Check exemptions
        if (isTakeout && rate->exemptTakeout()) continue;
        if (isEmployee && rate->exemptEmployee()) continue;

        // Calculate tax
        int taxAmount = rate->calculate(taxableBase);
        taxAmount = applyRounding(taxAmount);

        if (taxAmount > 0) {
            TaxResult result;
            result.taxRateId = rate->id();
            result.taxName = rate->name();
            result.taxableAmount = taxableBase;
            result.taxAmount = taxAmount;
            result.rateBasisPoints = rate->rateBasisPoints();
            breakdown.taxes.append(result);
            breakdown.totalTax += taxAmount;

            // Tax-on-tax: include this tax in base for subsequent taxes
            if (rate->includeInBase()) {
                taxableBase += taxAmount;
            }
        }
    }

    breakdown.grandTotal = breakdown.subtotal + breakdown.totalTax;
    return breakdown;
}

TaxBreakdown TaxManager::calculateTaxForItems(const QList<QPair<int, ItemTaxClass>>& items,
                                               bool isTakeout, bool isEmployee) {
    TaxBreakdown total;
    QMap<int, TaxResult> taxTotals;  // taxId -> accumulated result

    for (const auto& item : items) {
        int amount = item.first;
        ItemTaxClass itemClass = item.second;

        TaxBreakdown itemBreakdown = calculateTax(amount, itemClass, isTakeout, isEmployee);
        total.subtotal += itemBreakdown.subtotal;

        for (const auto& tax : itemBreakdown.taxes) {
            if (taxTotals.contains(tax.taxRateId)) {
                taxTotals[tax.taxRateId].taxableAmount += tax.taxableAmount;
                taxTotals[tax.taxRateId].taxAmount += tax.taxAmount;
            } else {
                taxTotals[tax.taxRateId] = tax;
            }
        }
    }

    // Flatten map to list
    for (const auto& result : taxTotals) {
        total.taxes.append(result);
        total.totalTax += result.taxAmount;
    }

    total.grandTotal = total.subtotal + total.totalTax;
    return total;
}

TaxBreakdown TaxManager::extractTaxFromInclusive(int inclusivePrice, ItemTaxClass itemClass) {
    TaxBreakdown breakdown;

    // Calculate combined rate for applicable taxes
    int combinedRateBP = 0;
    for (auto* rate : activeTaxRates()) {
        bool applies = false;
        switch (itemClass) {
        case ItemTaxClass::Food: applies = rate->appliesToFood(); break;
        case ItemTaxClass::Beverage: applies = rate->appliesToBeverage(); break;
        case ItemTaxClass::Alcohol: applies = rate->appliesToAlcohol(); break;
        case ItemTaxClass::Merchandise: applies = rate->appliesToMerchandise(); break;
        case ItemTaxClass::Service: applies = rate->appliesToService(); break;
        default: applies = true; break;
        }
        if (applies) {
            combinedRateBP += rate->rateBasisPoints();
        }
    }

    // Extract tax: subtotal = inclusive / (1 + rate)
    double rateMultiplier = 1.0 + (combinedRateBP / 10000.0);
    breakdown.subtotal = static_cast<int>(std::round(inclusivePrice / rateMultiplier));
    breakdown.totalTax = inclusivePrice - breakdown.subtotal;
    breakdown.grandTotal = inclusivePrice;

    // Distribute tax among applicable rates
    for (auto* rate : activeTaxRates()) {
        bool applies = false;
        switch (itemClass) {
        case ItemTaxClass::Food: applies = rate->appliesToFood(); break;
        case ItemTaxClass::Beverage: applies = rate->appliesToBeverage(); break;
        case ItemTaxClass::Alcohol: applies = rate->appliesToAlcohol(); break;
        case ItemTaxClass::Merchandise: applies = rate->appliesToMerchandise(); break;
        case ItemTaxClass::Service: applies = rate->appliesToService(); break;
        default: applies = true; break;
        }

        if (applies) {
            TaxResult result;
            result.taxRateId = rate->id();
            result.taxName = rate->name();
            result.taxableAmount = breakdown.subtotal;
            result.rateBasisPoints = rate->rateBasisPoints();
            result.taxAmount = rate->calculate(breakdown.subtotal);
            breakdown.taxes.append(result);
        }
    }

    return breakdown;
}

int TaxManager::applyRounding(int cents) const {
    switch (m_roundingMode) {
    case RoundingMode::Standard:
        return cents;  // Already rounded by int arithmetic

    case RoundingMode::RoundUp:
        return cents;  // Would need fractional cents to round up

    case RoundingMode::RoundDown:
        return cents;  // Would need fractional cents to round down

    case RoundingMode::DropPennies:
        // Round to nearest 5 cents
        return ((cents + 2) / 5) * 5;
    }
    return cents;
}

bool TaxManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextId"] = m_nextId;
    root["roundingMode"] = static_cast<int>(m_roundingMode);
    root["taxInclusive"] = m_taxInclusive;

    QJsonArray ratesArray;
    for (const auto* rate : m_rates) {
        ratesArray.append(rate->toJson());
    }
    root["rates"] = ratesArray;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool TaxManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();

    m_nextId = root["nextId"].toInt(1);
    m_roundingMode = static_cast<RoundingMode>(root["roundingMode"].toInt());
    m_taxInclusive = root["taxInclusive"].toBool(false);

    qDeleteAll(m_rates);
    m_rates.clear();

    QJsonArray ratesArray = root["rates"].toArray();
    for (const auto& ref : ratesArray) {
        auto* rate = TaxRate::fromJson(ref.toObject(), this);
        m_rates.append(rate);
    }

    emit taxRatesChanged();
    return true;
}

//=============================================================================
// TaxExemption Implementation
//=============================================================================

TaxExemption::TaxExemption(QObject* parent)
    : QObject(parent)
{
}

bool TaxExemption::isValid() const {
    QDate today = QDate::currentDate();
    if (m_validFrom.isValid() && today < m_validFrom) return false;
    if (m_validTo.isValid() && today > m_validTo) return false;
    return true;
}

QJsonObject TaxExemption::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["certificateNumber"] = m_certNumber;
    json["holderName"] = m_holderName;

    QJsonArray idsArray;
    for (int id : m_exemptTaxIds) {
        idsArray.append(id);
    }
    json["exemptTaxIds"] = idsArray;

    if (m_validFrom.isValid()) {
        json["validFrom"] = m_validFrom.toString(Qt::ISODate);
    }
    if (m_validTo.isValid()) {
        json["validTo"] = m_validTo.toString(Qt::ISODate);
    }

    return json;
}

TaxExemption* TaxExemption::fromJson(const QJsonObject& json, QObject* parent) {
    auto* exemption = new TaxExemption(parent);
    exemption->m_id = json["id"].toInt();
    exemption->m_certNumber = json["certificateNumber"].toString();
    exemption->m_holderName = json["holderName"].toString();

    QJsonArray idsArray = json["exemptTaxIds"].toArray();
    for (const auto& ref : idsArray) {
        exemption->m_exemptTaxIds.append(ref.toInt());
    }

    if (json.contains("validFrom")) {
        exemption->m_validFrom = QDate::fromString(json["validFrom"].toString(), Qt::ISODate);
    }
    if (json.contains("validTo")) {
        exemption->m_validTo = QDate::fromString(json["validTo"].toString(), Qt::ISODate);
    }

    return exemption;
}

} // namespace vt2
