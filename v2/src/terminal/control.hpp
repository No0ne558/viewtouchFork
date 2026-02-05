/*
 * ViewTouch V2 - Control Class
 * Central manager for the POS system
 */

#pragma once

#include "core/types.hpp"

#include <QObject>
#include <QString>
#include <vector>
#include <memory>

namespace vt {

class Terminal;
class ZoneDB;

/*************************************************************
 * Control - Central System Manager
 * 
 * Manages:
 * - All terminals
 * - Zone database
 * - System configuration
 * - Data paths
 *************************************************************/
class Control : public QObject {
    Q_OBJECT
    
public:
    explicit Control(QObject* parent = nullptr);
    ~Control();
    
    // Initialization
    bool initialize();
    void shutdown();
    
    // Data paths
    QString dataPath() const { return dataPath_; }
    void setDataPath(const QString& path) { dataPath_ = path; }
    
    QString imagesPath() const;
    QString fontsPath() const;
    QString soundsPath() const;
    
    // Zone database
    ZoneDB* zoneDb() const { return zoneDb_.get(); }
    
    // Terminal management
    Terminal* createTerminal();
    void removeTerminal(Terminal* term);
    Terminal* terminal(int index);
    const Terminal* terminal(int index) const;
    size_t terminalCount() const { return terminals_.size(); }
    
    // Find terminal by ID
    Terminal* terminalById(int id);
    
    // Terminal iteration
    auto begin() { return terminals_.begin(); }
    auto end() { return terminals_.end(); }
    auto begin() const { return terminals_.begin(); }
    auto end() const { return terminals_.end(); }
    
    // System name
    QString systemName() const { return systemName_; }
    void setSystemName(const QString& name) { systemName_ = name; }
    
    // Store information
    QString storeName() const { return storeName_; }
    void setStoreName(const QString& name) { storeName_ = name; }
    
    int storeNumber() const { return storeNumber_; }
    void setStoreNumber(int num) { storeNumber_ = num; }
    
    // Currency
    QString currencySymbol() const { return currencySymbol_; }
    void setCurrencySymbol(const QString& sym) { currencySymbol_ = sym; }
    
    // Tax settings
    double taxRate() const { return taxRate_; }
    void setTaxRate(double rate) { taxRate_ = rate; }
    
    // Load configuration
    bool loadConfig(const QString& filename);
    bool saveConfig(const QString& filename) const;
    
    // Load zone database
    bool loadZoneDb(const QString& filename);
    
signals:
    void terminalAdded(Terminal* term);
    void terminalRemoved(Terminal* term);
    void configChanged();
    
private:
    // Data paths
    QString dataPath_;
    
    // Zone database
    std::unique_ptr<ZoneDB> zoneDb_;
    
    // Terminals
    std::vector<std::unique_ptr<Terminal>> terminals_;
    int nextTerminalId_ = 1;
    
    // System info
    QString systemName_ = QStringLiteral("ViewTouch");
    QString storeName_;
    int storeNumber_ = 0;
    
    // Currency
    QString currencySymbol_ = QStringLiteral("$");
    
    // Tax
    double taxRate_ = 0.0;
};

} // namespace vt
