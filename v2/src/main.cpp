/**
 * @file main.cpp
 * @brief ViewTouch V2 Application Entry Point
 * 
 * This is the main entry point for ViewTouch V2, a modern rewrite of
 * the classic ViewTouch POS system using Qt6 and C++23.
 */

#include "core/application.hpp"
#include "core/logger.hpp"
#include <QApplication>
#include <iostream>

int main(int argc, char* argv[]) {
    // Initialize application
    auto& app = vt2::Application::instance();
    
    if (auto result = app.init(argc, argv); !result) {
        std::cerr << "Failed to initialize application: " << result.error() << std::endl;
        return 1;
    }
    
    // Run main event loop
    int exitCode = app.run();
    
    // Cleanup
    app.shutdown();
    
    return exitCode;
}
