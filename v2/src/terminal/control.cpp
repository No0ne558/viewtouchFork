/*
 * ViewTouch V2 - Control Implementation
 */

#include "terminal/control.hpp"
#include "terminal/terminal.hpp"
#include "zone/zone_db.hpp"

#include <QDir>
#include <QFile>
#include <QSettings>

namespace vt {

Control::Control(QObject* parent)
    : QObject(parent)
    , zoneDb_(std::make_unique<ZoneDB>())
{
    // Initialize system pages
    zoneDb_->initSystemPages();
}

Control::~Control() = default;

bool Control::initialize() {
    // Ensure data path exists
    if (!dataPath_.isEmpty()) {
        QDir dir(dataPath_);
        if (!dir.exists()) {
            return false;
        }
        // Set data directory for ZoneDB
        zoneDb_->setDataDir(dataPath_);
    }
    
    // Try to load UI data
    if (!loadUi()) {
        // If no UI file exists, we start with just system pages
        // which are already initialized in the constructor
    }
    
    return true;
}

void Control::shutdown() {
    terminals_.clear();
}

QString Control::imagesPath() const {
    return dataPath_.isEmpty() ? QString() : dataPath_ + QStringLiteral("/images");
}

QString Control::fontsPath() const {
    return dataPath_.isEmpty() ? QString() : dataPath_ + QStringLiteral("/fonts");
}

QString Control::soundsPath() const {
    return dataPath_.isEmpty() ? QString() : dataPath_ + QStringLiteral("/sounds");
}

Terminal* Control::createTerminal() {
    auto term = std::make_unique<Terminal>(this);
    term->setId(nextTerminalId_++);
    term->setControl(this);
    term->setZoneDb(zoneDb_.get());
    
    Terminal* ptr = term.get();
    terminals_.push_back(std::move(term));
    
    emit terminalAdded(ptr);
    return ptr;
}

void Control::removeTerminal(Terminal* term) {
    auto it = std::find_if(terminals_.begin(), terminals_.end(),
        [term](const auto& t) { return t.get() == term; });
    
    if (it != terminals_.end()) {
        emit terminalRemoved(term);
        terminals_.erase(it);
    }
}

Terminal* Control::terminal(int index) {
    if (index >= 0 && static_cast<size_t>(index) < terminals_.size()) {
        return terminals_[index].get();
    }
    return nullptr;
}

const Terminal* Control::terminal(int index) const {
    if (index >= 0 && static_cast<size_t>(index) < terminals_.size()) {
        return terminals_[index].get();
    }
    return nullptr;
}

Terminal* Control::terminalById(int id) {
    for (auto& t : terminals_) {
        if (t->id() == id) {
            return t.get();
        }
    }
    return nullptr;
}

bool Control::loadConfig(const QString& filename) {
    QSettings settings(filename, QSettings::IniFormat);
    
    if (settings.status() != QSettings::NoError) {
        return false;
    }
    
    systemName_ = settings.value(QStringLiteral("System/Name"), systemName_).toString();
    storeName_ = settings.value(QStringLiteral("Store/Name")).toString();
    storeNumber_ = settings.value(QStringLiteral("Store/Number"), 0).toInt();
    currencySymbol_ = settings.value(QStringLiteral("Currency/Symbol"), currencySymbol_).toString();
    taxRate_ = settings.value(QStringLiteral("Tax/Rate"), 0.0).toDouble();
    
    emit configChanged();
    return true;
}

bool Control::saveConfig(const QString& filename) const {
    QSettings settings(filename, QSettings::IniFormat);
    
    settings.setValue(QStringLiteral("System/Name"), systemName_);
    settings.setValue(QStringLiteral("Store/Name"), storeName_);
    settings.setValue(QStringLiteral("Store/Number"), storeNumber_);
    settings.setValue(QStringLiteral("Currency/Symbol"), currencySymbol_);
    settings.setValue(QStringLiteral("Tax/Rate"), taxRate_);
    
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool Control::loadZoneDb(const QString& filename) {
    if (zoneDb_) {
        return zoneDb_->loadUi(filename);
    }
    return false;
}

bool Control::loadUi(const QString& filename) {
    if (zoneDb_) {
        QString file = filename.isEmpty() ? QStringLiteral("Ui") : filename;
        return zoneDb_->loadUi(file);
    }
    return false;
}

bool Control::saveUi(const QString& filename) const {
    if (zoneDb_) {
        QString file = filename.isEmpty() ? QStringLiteral("Ui") : filename;
        return zoneDb_->saveUi(file);
    }
    return false;
}

} // namespace vt
