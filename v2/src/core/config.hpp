/**
 * @file config.hpp
 * @brief Configuration management for ViewTouch V2
 */

#pragma once

#include "core/types.hpp"
#include <toml++/toml.hpp>
#include <nlohmann/json.hpp>
#include <QString>
#include <filesystem>
#include <optional>

namespace vt2 {

/**
 * @brief Application configuration manager
 * 
 * Handles loading, saving, and accessing configuration values.
 * Supports TOML files for human-readable config and JSON for data interchange.
 */
class Config {
public:
    static Config& instance() {
        static Config instance;
        return instance;
    }
    
    /**
     * @brief Load configuration from file
     * @param path Path to config file (TOML format)
     * @return Success or error message
     */
    Result<void> load(const std::filesystem::path& path);
    
    /**
     * @brief Save configuration to file
     * @param path Path to save to (optional, uses load path if empty)
     * @return Success or error message
     */
    Result<void> save(const std::filesystem::path& path = {});
    
    /**
     * @brief Create default configuration
     */
    void createDefaults();
    
    // ========================================================================
    // General Settings
    // ========================================================================
    
    QString storeName() const;
    void setStoreName(const QString& name);
    
    QString storeAddress() const;
    void setStoreAddress(const QString& address);
    
    QString storePhone() const;
    void setStorePhone(const QString& phone);
    
    // ========================================================================
    // Display Settings
    // ========================================================================
    
    int screenWidth() const;
    int screenHeight() const;
    void setScreenSize(int width, int height);
    
    bool fullscreen() const;
    void setFullscreen(bool enabled);
    
    QString theme() const;
    void setTheme(const QString& themeName);
    
    double scaleFactor() const;
    void setScaleFactor(double factor);
    
    // ========================================================================
    // System Settings
    // ========================================================================
    
    std::filesystem::path dataDirectory() const;
    void setDataDirectory(const std::filesystem::path& path);
    
    std::filesystem::path logDirectory() const;
    void setLogDirectory(const std::filesystem::path& path);
    
    bool debugMode() const;
    void setDebugMode(bool enabled);
    
    // ========================================================================
    // Hardware Settings
    // ========================================================================
    
    QString printerDevice() const;
    void setPrinterDevice(const QString& device);
    
    QString drawerDevice() const;
    void setDrawerDevice(const QString& device);
    
    bool touchscreenEnabled() const;
    void setTouchscreenEnabled(bool enabled);
    
    // ========================================================================
    // Network Settings
    // ========================================================================
    
    QString serverAddress() const;
    void setServerAddress(const QString& address);
    
    int serverPort() const;
    void setServerPort(int port);
    
    // ========================================================================
    // Generic Access
    // ========================================================================
    
    template<typename T>
    std::optional<T> get(const std::string& path) const;
    
    template<typename T>
    void set(const std::string& path, const T& value);
    
    /**
     * @brief Get raw TOML table for advanced access
     */
    const toml::table& raw() const { return config_; }
    
private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    toml::table config_;
    std::filesystem::path configPath_;
};

// Template implementations
template<typename T>
std::optional<T> Config::get(const std::string& path) const {
    if (auto node = config_.at_path(path)) {
        if (auto val = node.value<T>()) {
            return *val;
        }
    }
    return std::nullopt;
}

template<typename T>
void Config::set(const std::string& path, const T& value) {
    // Parse the path and set the value
    // This is a simplified implementation
    config_.insert_or_assign(path, value);
}

} // namespace vt2
