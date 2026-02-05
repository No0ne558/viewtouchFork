cmake_minimum_required(VERSION 3.16)

project(ViewTouchV2 VERSION 2.0.0 LANGUAGES CXX)

# ============================================================================
# ViewTouch V2 - Modern Qt6-based POS System
# ============================================================================
# This is a complete rewrite of ViewTouch using modern C++ and Qt6
# while preserving the core concepts: Zones, Pages, Buttons, Properties
# ============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt6 setup
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 packages
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Sql
    PrintSupport
)

# Optional Qt6 components
find_package(Qt6 COMPONENTS
    Quick
    Qml
)

message(STATUS "")
message(STATUS "ViewTouch V2 Build Configuration")
message(STATUS "=================================")
message(STATUS "Qt6 Version: ${Qt6_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")

# Modern dependencies via FetchContent
include(FetchContent)

# spdlog - Fast logging
message(STATUS "Fetching spdlog...")
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
    GIT_SHALLOW TRUE
)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# nlohmann/json - JSON handling
message(STATUS "Fetching nlohmann/json...")
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# magic_enum - Enum reflection
message(STATUS "Fetching magic_enum...")
FetchContent_Declare(
    magic_enum
    GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
    GIT_TAG v0.9.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(magic_enum)

# toml++ - Config file parsing
message(STATUS "Fetching toml++...")
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tomlplusplus)

# cpp-httplib - HTTP client (header-only)
message(STATUS "Fetching cpp-httplib...")
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(httplib)

# Catch2 for testing
message(STATUS "Fetching Catch2...")
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

# ============================================================================
# Core Library - Foundation classes
# ============================================================================
add_library(vt2_core STATIC
    src/core/application.cpp
    src/core/application.hpp
    src/core/config.cpp
    src/core/config.hpp
    src/core/logger.cpp
    src/core/logger.hpp
    src/core/types.hpp
)

target_include_directories(vt2_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(vt2_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    magic_enum::magic_enum
    tomlplusplus::tomlplusplus
)

# ============================================================================
# UI Library - Zone and Page system
# ============================================================================
add_library(vt2_ui STATIC
    src/ui/zone.cpp
    src/ui/zone.hpp
    src/ui/page.cpp
    src/ui/page.hpp
    src/ui/main_window.cpp
    src/ui/main_window.hpp
    src/ui/theme.cpp
    src/ui/theme.hpp
    src/ui/property_editor.cpp
    src/ui/property_editor.hpp
)

target_include_directories(vt2_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vt2_ui PUBLIC
    vt2_core
    Qt6::Widgets
)

# ============================================================================
# Zones Library - All zone implementations
# ============================================================================
add_library(vt2_zones STATIC
    src/zones/button_zone.cpp
    src/zones/button_zone.hpp
    src/zones/order_zone.cpp
    src/zones/order_zone.hpp
    src/zones/payment_zone.cpp
    src/zones/payment_zone.hpp
    src/zones/login_zone.cpp
    src/zones/login_zone.hpp
    src/zones/table_zone.cpp
    src/zones/table_zone.hpp
    src/zones/report_zone.cpp
    src/zones/report_zone.hpp
)

target_include_directories(vt2_zones PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vt2_zones PUBLIC
    vt2_ui
)

# ============================================================================
# Network Library - HTTP, sockets, etc.
# ============================================================================
add_library(vt2_network STATIC
    src/network/http_client.cpp
    src/network/http_client.hpp
)

target_include_directories(vt2_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${httplib_SOURCE_DIR}
)

target_link_libraries(vt2_network PUBLIC
    vt2_core
    Qt6::Network
    httplib::httplib
)

# ============================================================================
# Data Library - Business logic, persistence
# ============================================================================
add_library(vt2_data STATIC
    src/data/check.cpp
    src/data/check.hpp
    src/data/order.cpp
    src/data/order.hpp
    src/data/menu_item.cpp
    src/data/menu_item.hpp
    src/data/employee.cpp
    src/data/employee.hpp
    src/data/settings.cpp
    src/data/settings.hpp
)

target_include_directories(vt2_data PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(vt2_data PUBLIC
    vt2_core
    Qt6::Sql
)

# ============================================================================
# Main Application
# ============================================================================
add_executable(viewtouch2
    src/main.cpp
)

target_link_libraries(viewtouch2 PRIVATE
    vt2_core
    vt2_ui
    vt2_zones
    vt2_network
    vt2_data
    Qt6::Widgets
    Qt6::PrintSupport
)

# ============================================================================
# Installation
# ============================================================================
set(VIEWTOUCH_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/viewtouch2")

install(TARGETS viewtouch2
    RUNTIME DESTINATION ${VIEWTOUCH_INSTALL_DIR}/bin
)

install(DIRECTORY resources/
    DESTINATION ${VIEWTOUCH_INSTALL_DIR}/resources
)

# ============================================================================
# Testing
# ============================================================================
option(BUILD_TESTING "Build unit tests" OFF)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Summary
# ============================================================================
message(STATUS "")
message(STATUS "ViewTouch V2 Configuration Complete")
message(STATUS "====================================")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Build tests: ${BUILD_TESTING}")
message(STATUS "")
