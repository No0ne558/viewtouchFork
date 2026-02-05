/**
 * @file logger.cpp
 * @brief Logger implementation
 */

#include "core/logger.hpp"
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>

namespace vt2 {

void Logger::init(const std::string& logDir, spdlog::level::level_enum level) {
    if (initialized_) {
        return;
    }
    
    try {
        // Create log directory if it doesn't exist
        std::filesystem::create_directories(logDir);
        
        // Create sinks
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(level);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
        
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logDir + "/viewtouch.log", 
            1024 * 1024 * 5,  // 5MB max size
            3                  // 3 rotated files
        );
        file_sink->set_level(spdlog::level::trace);  // File gets everything
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
        
        // Create logger with both sinks
        logger_ = std::make_shared<spdlog::logger>(
            "viewtouch", 
            spdlog::sinks_init_list{console_sink, file_sink}
        );
        
        logger_->set_level(level);
        logger_->flush_on(spdlog::level::warn);  // Auto-flush on warnings and above
        
        // Register as default logger
        spdlog::set_default_logger(logger_);
        
        initialized_ = true;
        
        logger_->info("ViewTouch V2 logging initialized");
        logger_->info("Log directory: {}", logDir);
        
    } catch (const spdlog::spdlog_ex& ex) {
        // Fallback to console-only logging
        logger_ = spdlog::stdout_color_mt("viewtouch");
        logger_->error("Failed to initialize file logging: {}", ex.what());
        initialized_ = true;
    }
}

void Logger::setLevel(spdlog::level::level_enum level) {
    if (logger_) {
        logger_->set_level(level);
    }
}

} // namespace vt2
