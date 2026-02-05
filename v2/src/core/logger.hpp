/**
 * @file logger.hpp
 * @brief Logging system for ViewTouch V2
 */

#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <string>
#include <memory>

namespace vt2 {

/**
 * @brief Centralized logging system
 * 
 * Provides structured logging with multiple sinks (console, file)
 * and various log levels. Uses spdlog under the hood.
 */
class Logger {
public:
    static Logger& instance() {
        static Logger instance;
        return instance;
    }
    
    /**
     * @brief Initialize the logging system
     * @param logDir Directory for log files
     * @param level Minimum log level
     */
    void init(const std::string& logDir = "logs", 
              spdlog::level::level_enum level = spdlog::level::info);
    
    /**
     * @brief Get the main logger
     */
    std::shared_ptr<spdlog::logger> get() { return logger_; }
    
    /**
     * @brief Set the log level
     */
    void setLevel(spdlog::level::level_enum level);
    
    // Convenience methods
    template<typename... Args>
    void trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->trace(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->debug(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->info(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->warn(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->error(fmt, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    void critical(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        logger_->critical(fmt, std::forward<Args>(args)...);
    }
    
private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    std::shared_ptr<spdlog::logger> logger_;
    bool initialized_ = false;
};

// Global logging macros for convenience
#define VT_TRACE(...) vt2::Logger::instance().trace(__VA_ARGS__)
#define VT_DEBUG(...) vt2::Logger::instance().debug(__VA_ARGS__)
#define VT_INFO(...)  vt2::Logger::instance().info(__VA_ARGS__)
#define VT_WARN(...)  vt2::Logger::instance().warn(__VA_ARGS__)
#define VT_ERROR(...) vt2::Logger::instance().error(__VA_ARGS__)
#define VT_CRITICAL(...) vt2::Logger::instance().critical(__VA_ARGS__)

} // namespace vt2
