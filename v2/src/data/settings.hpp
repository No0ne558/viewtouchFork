/**
 * @file settings.hpp
 * @brief System settings data
 */

#pragma once

#include "core/types.hpp"
#include <QString>

namespace vt2 {

/**
 * @brief System settings for the POS
 */
class Settings {
public:
    static Settings& instance();
    
    // Tax settings
    double taxRate() const { return taxRate_; }
    void setTaxRate(double rate) { taxRate_ = rate; }
    
    // Currency
    QString currencySymbol() const { return currencySymbol_; }
    void setCurrencySymbol(const QString& sym) { currencySymbol_ = sym; }
    
    // Tips
    bool tipsEnabled() const { return tipsEnabled_; }
    void setTipsEnabled(bool enabled) { tipsEnabled_ = enabled; }
    
private:
    Settings() = default;
    
    double taxRate_ = 0.08;
    QString currencySymbol_ = "$";
    bool tipsEnabled_ = true;
};

} // namespace vt2
