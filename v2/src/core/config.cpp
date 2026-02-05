/**
 * @file config.cpp
 * @brief Configuration implementation
 */

#include "core/config.hpp"
#include "core/logger.hpp"
#include <fstream>

namespace vt2 {

Result<void> Config::load(const std::filesystem::path& path) {
    try {
        config_ = toml::parse_file(path.string());
        configPath_ = path;
        VT_INFO("Configuration loaded from: {}", path.string());
        return {};
    } catch (const toml::parse_error& err) {
        std::string error = std::format("Failed to parse config: {}", err.what());
        VT_ERROR("{}", error);
        return std::unexpected(error);
    } catch (const std::exception& ex) {
        std::string error = std::format("Failed to load config: {}", ex.what());
        VT_ERROR("{}", error);
        return std::unexpected(error);
    }
}

Result<void> Config::save(const std::filesystem::path& path) {
    auto savePath = path.empty() ? configPath_ : path;
    
    if (savePath.empty()) {
        return std::unexpected("No config path specified");
    }
    
    try {
        std::ofstream file(savePath);
        if (!file) {
            return std::unexpected("Failed to open config file for writing");
        }
        file << config_;
        VT_INFO("Configuration saved to: {}", savePath.string());
        return {};
    } catch (const std::exception& ex) {
        std::string error = std::format("Failed to save config: {}", ex.what());
        VT_ERROR("{}", error);
        return std::unexpected(error);
    }
}

void Config::createDefaults() {
    config_ = toml::table{{
        {"store", toml::table{{
            {"name", "My Restaurant"},
            {"address", "123 Main Street"},
            {"phone", "(555) 123-4567"}
        }}},
        {"display", toml::table{{
            {"width", 1024},
            {"height", 768},
            {"fullscreen", false},
            {"theme", "modern-dark"},
            {"scale_factor", 1.0}
        }}},
        {"system", toml::table{{
            {"data_directory", "/usr/viewtouch2/dat"},
            {"log_directory", "/var/log/viewtouch2"},
            {"debug_mode", false}
        }}},
        {"hardware", toml::table{{
            {"printer_device", "/dev/usb/lp0"},
            {"drawer_device", "/dev/ttyUSB0"},
            {"touchscreen_enabled", true}
        }}},
        {"network", toml::table{{
            {"server_address", "localhost"},
            {"server_port", 8080}
        }}}
    }};
    
    VT_INFO("Default configuration created");
}

// ============================================================================
// Store Settings
// ============================================================================

QString Config::storeName() const {
    if (auto val = config_["store"]["name"].value<std::string>()) {
        return QString::fromStdString(*val);
    }
    return "ViewTouch";
}

void Config::setStoreName(const QString& name) {
    config_["store"].as_table()->insert_or_assign("name", name.toStdString());
}

QString Config::storeAddress() const {
    if (auto val = config_["store"]["address"].value<std::string>()) {
        return QString::fromStdString(*val);
    }
    return "";
}

void Config::setStoreAddress(const QString& address) {
    config_["store"].as_table()->insert_or_assign("address", address.toStdString());
}

QString Config::storePhone() const {
    if (auto val = config_["store"]["phone"].value<std::string>()) {
        return QString::fromStdString(*val);
    }
    return "";
}

void Config::setStorePhone(const QString& phone) {
    config_["store"].as_table()->insert_or_assign("phone", phone.toStdString());
}

// ============================================================================
// Display Settings
// ============================================================================

int Config::screenWidth() const {
    return config_["display"]["width"].value_or(1024);
}

int Config::screenHeight() const {
    return config_["display"]["height"].value_or(768);
}

void Config::setScreenSize(int width, int height) {
    auto* display = config_["display"].as_table();
    display->insert_or_assign("width", width);
    display->insert_or_assign("height", height);
}

bool Config::fullscreen() const {
    return config_["display"]["fullscreen"].value_or(false);
}

void Config::setFullscreen(bool enabled) {
    config_["display"].as_table()->insert_or_assign("fullscreen", enabled);
}

QString Config::theme() const {
    if (auto val = config_["display"]["theme"].value<std::string>()) {
        return QString::fromStdString(*val);
    }
    return "modern-dark";
}

void Config::setTheme(const QString& themeName) {
    config_["display"].as_table()->insert_or_assign("theme", themeName.toStdString());
}

double Config::scaleFactor() const {
    return config_["display"]["scale_factor"].value_or(1.0);
}

void Config::setScaleFactor(double factor) {
    config_["display"].as_table()->insert_or_assign("scale_factor", factor);
}

// ============================================================================
// System Settings
// ============================================================================

std::filesystem::path Config::dataDirectory() const {
    if (auto val = config_["system"]["data_directory"].value<std::string>()) {
        return *val;
    }
    return "/usr/viewtouch2/dat";
}

void Config::setDataDirectory(const std::filesystem::path& path) {
    config_["system"].as_table()->insert_or_assign("data_directory", path.string());
}

std::filesystem::path Config::logDirectory() const {
    if (auto val = config_["system"]["log_directory"].value<std::string>()) {
        return *val;
    }
    return "/var/log/viewtouch2";
}

void Config::setLogDirectory(const std::filesystem::path& path) {
    config_["system"].as_table()->insert_or_assign("log_directory", path.string());
}

bool Config::debugMode() const {
    return config_["system"]["debug_mode"].value_or(false);
}

void Config::setDebugMode(bool enabled) {
    config_["system"].as_table()->insert_or_assign("debug_mode", enabled);
}

// ============================================================================
// Hardware Settings
// ============================================================================

QString Config::printerDevice() const {
    if (auto val = config_["hardware"]["printer_device"].value<std::string>()) {
        return QString::fromStdString(*val);
    }
    return "";
}

void Config::setPrinterDevice(const QString& device) {
    config_["hardware"].as_table()->insert_or_assign("printer_device", device.toStdString());
}

QString Config::drawerDevice() const {
    if (auto val = config_["hardware"]["drawer_device"].value<std::string>()) {
        return QString::fromStdString(*val);
    }
    return "";
}

void Config::setDrawerDevice(const QString& device) {
    config_["hardware"].as_table()->insert_or_assign("drawer_device", device.toStdString());
}

bool Config::touchscreenEnabled() const {
    return config_["hardware"]["touchscreen_enabled"].value_or(true);
}

void Config::setTouchscreenEnabled(bool enabled) {
    config_["hardware"].as_table()->insert_or_assign("touchscreen_enabled", enabled);
}

// ============================================================================
// Network Settings
// ============================================================================

QString Config::serverAddress() const {
    if (auto val = config_["network"]["server_address"].value<std::string>()) {
        return QString::fromStdString(*val);
    }
    return "localhost";
}

void Config::setServerAddress(const QString& address) {
    config_["network"].as_table()->insert_or_assign("server_address", address.toStdString());
}

int Config::serverPort() const {
    return config_["network"]["server_port"].value_or(8080);
}

void Config::setServerPort(int port) {
    config_["network"].as_table()->insert_or_assign("server_port", port);
}

} // namespace vt2
