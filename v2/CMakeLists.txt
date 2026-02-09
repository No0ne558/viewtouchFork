cmake_minimum_required(VERSION 3.20)
project(viewtouch2 VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# Core definitions and types
set(CORE_SOURCES
    src/core/types.hpp
    src/core/colors.hpp
    src/core/colors.cpp
    src/core/fonts.hpp
    src/core/fonts.cpp
)

# Zone system
set(ZONE_SOURCES
    src/zone/zone.hpp
    src/zone/zone.cpp
    src/zone/zone_types.hpp
    src/zone/zone_types.cpp
    src/zone/page.hpp
    src/zone/page.cpp
    src/zone/zone_db.hpp
    src/zone/zone_db.cpp
    src/zone/login_zone.hpp
    src/zone/login_zone.cpp
    src/zone/table_zone.hpp
    src/zone/table_zone.cpp
    src/zone/order_zone.hpp
    src/zone/order_zone.cpp
    src/zone/payment_zone.hpp
    src/zone/payment_zone.cpp
)

# Terminal and control
set(TERMINAL_SOURCES
    src/terminal/terminal.hpp
    src/terminal/terminal.cpp
    src/terminal/control.hpp
    src/terminal/control.cpp
)

# Rendering
set(RENDER_SOURCES
    src/render/renderer.hpp
    src/render/renderer.cpp
    src/render/textures.hpp
    src/render/textures.cpp
)

# Editor tools
set(EDITOR_SOURCES
    src/editor/edit_mode.hpp
    src/editor/edit_mode.cpp
    src/editor/edit_toolbar.hpp
    src/editor/edit_toolbar.cpp
    src/editor/zone_properties.hpp
    src/editor/zone_properties.cpp
    src/editor/page_properties.hpp
    src/editor/page_properties.cpp
)



# Main application
set(APP_SOURCES
    src/main.cpp
    src/app/application.hpp
    src/app/application.cpp
    src/app/main_window.hpp
    src/app/main_window.cpp
)

add_executable(viewtouch2
    ${CORE_SOURCES}
    ${ZONE_SOURCES}
    ${TERMINAL_SOURCES}
    ${RENDER_SOURCES}
    ${EDITOR_SOURCES}
    ${APP_SOURCES}
)

target_include_directories(viewtouch2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(viewtouch2 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# Install
install(TARGETS viewtouch2 RUNTIME DESTINATION bin)
