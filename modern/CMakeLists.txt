cmake_minimum_required(VERSION 3.21)

project(ViewTouchModern VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VTM_BUILD_TESTS "Build ViewTouch Modern unit tests" ON)

# ---------------------------------------------------------------------------
# core: pure C++ domain library (no Qt). Always buildable.
# ---------------------------------------------------------------------------
add_library(vtm_core INTERFACE)
target_include_directories(vtm_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Qt6 / QML application. Skipped (with a warning) when Qt6 dev files are
# missing so the core library and its tests can still be built.
#   Fedora: sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel
# ---------------------------------------------------------------------------
find_package(Qt6 6.5 QUIET COMPONENTS Core Gui Quick Sql)

if(Qt6_FOUND)
    qt_standard_project_setup(REQUIRES 6.5)

    qt_add_executable(vtmodern main.cpp)

    qt_add_qml_module(vtmodern
        URI ViewTouch
        VERSION 1.0
        QML_FILES
            ui/qml/Main.qml
    )

    target_link_libraries(vtmodern PRIVATE vtm_core Qt6::Core Qt6::Gui Qt6::Quick Qt6::Sql)
else()
    message(WARNING
        "Qt6 (Core Gui Quick Sql >= 6.5) development files not found: "
        "building core library and tests only. "
        "Install qt6-qtbase-devel and qt6-qtdeclarative-devel to build vtmodern.")
endif()

# ---------------------------------------------------------------------------
# Tests (Catch2 v3, vendored in the legacy tree's external/catch2)
# ---------------------------------------------------------------------------
if(VTM_BUILD_TESTS)
    enable_testing()
    if(NOT TARGET Catch2::Catch2WithMain)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../external/catch2
                         ${CMAKE_CURRENT_BINARY_DIR}/catch2 EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory(tests)
endif()
