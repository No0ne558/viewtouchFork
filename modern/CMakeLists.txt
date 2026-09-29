cmake_minimum_required(VERSION 3.21)

project(ViewTouchModern VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VTM_BUILD_TESTS "Build ViewTouch Modern unit tests" ON)

set(VTM_LEGACY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ---------------------------------------------------------------------------
# core: pure C++ domain library (no Qt). Always buildable.
# ---------------------------------------------------------------------------
add_library(vtm_core INTERFACE)
target_include_directories(vtm_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Qt6 parts. Skipped (with a warning) when Qt6 dev files are missing so the
# core library and its tests can still be built.
#   Fedora: sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel
# ---------------------------------------------------------------------------
find_package(Qt6 6.8 QUIET COMPONENTS Core Gui Quick QuickControls2 Sql)

if(Qt6_FOUND)
    qt_standard_project_setup(REQUIRES 6.8)
    qt_policy(SET QTP0004 NEW)

    # layout + navigation: QtCore only, unit-testable without a GUI
    add_library(vtm_layout STATIC
        layout/style.hh layout/style.cpp
        layout/zone.hh layout/zone.cpp
        layout/page.hh layout/page.cpp
        layout/layout.hh layout/layout.cpp
        layout/json_path.hh layout/json_path.cpp
        layout/schema.hh layout/schema.cpp
        app/navigator.hh app/navigator.cpp
    )
    target_include_directories(vtm_layout PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(vtm_layout PUBLIC vtm_core Qt6::Core)
    set_target_properties(vtm_layout PROPERTIES AUTOMOC OFF)

    # page editor: draft layout + undo stack (QUndoStack lives in QtGui)
    add_library(vtm_editor STATIC app/layout_editor.hh app/layout_editor.cpp)
    target_link_libraries(vtm_editor PUBLIC vtm_layout Qt6::Gui)
    set_target_properties(vtm_editor PROPERTIES AUTOMOC OFF)

    # SQLite persistence
    add_library(vtm_storage STATIC storage/layout_store.hh storage/layout_store.cpp)
    target_link_libraries(vtm_storage PUBLIC vtm_layout Qt6::Sql)
    set_target_properties(vtm_storage PROPERTIES AUTOMOC OFF)

    # UI: the ViewTouch QML module as a static library, so the app and the
    # UI tests load the very same QML, C++ types, and resources.
    qt_add_library(vtm_ui STATIC)
    set_source_files_properties(ui/qml/EditorStyle.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

    qt_add_qml_module(vtm_ui
        URI ViewTouch
        VERSION 1.0
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ViewTouch
        QML_FILES
            ui/qml/Main.qml
            ui/qml/PageView.qml
            ui/qml/ZoneItem.qml
            ui/qml/ZoneShape.qml
            ui/qml/EditorStyle.qml
            ui/qml/EditLayer.qml
            ui/qml/EditorToolbar.qml
            ui/qml/PagePanel.qml
            ui/qml/Inspector.qml
            ui/qml/FieldEditor.qml
            ui/qml/ActionListEditor.qml
        SOURCES
            ui/cpp/zonemodel.hh ui/cpp/zonemodel.cpp
            ui/cpp/layoutcontroller.hh ui/cpp/layoutcontroller.cpp
            ui/cpp/editorcontroller.hh ui/cpp/editorcontroller.cpp
    )
    target_include_directories(vtm_ui PUBLIC ui/cpp)
    target_link_libraries(vtm_ui PUBLIC vtm_layout vtm_editor vtm_storage Qt6::Gui Qt6::Quick Qt6::QuickControls2)

    qt_add_executable(vtmodern main.cpp)
    target_link_libraries(vtmodern PRIVATE vtm_ui vtm_uiplugin)

    # Starter layout shipped with the app (qrc:/seed)
    file(GLOB_RECURSE VTM_SEED_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
         CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/seed/*.json)
    qt_add_resources(vtm_ui "seed" PREFIX "/" FILES ${VTM_SEED_FILES})

    # Legacy ViewTouch textures under short names (qrc:/textures/<name>.xpm)
    set(VTM_TEXTURES
        sand:sand-8 litsand:litsand-6 darksand:darksand-6
        graymarble:graymarble-12 greenmarble:greenmarble-12
        wood:wood-10 darkwood:darkwood-10 litewood:litewood-8 woodfloor:woodfloor-12
        parchment:parchment-6 blueparchment:blueparchment leather:leather-8
        steel:steel-8 metal:metal-8 canvas:canvas-8 velvet:velvet-8 pearl:pearl-8
        stone:stone-8 carbonfiber:carbonfiber-128-6
    )
    set(VTM_TEXTURE_FILES)
    foreach(entry IN LISTS VTM_TEXTURES)
        string(REPLACE ":" ";" parts ${entry})
        list(GET parts 0 alias)
        list(GET parts 1 file)
        set(path ${VTM_LEGACY_ROOT}/assets/images/xpm/${file}.xpm)
        set_source_files_properties(${path} PROPERTIES QT_RESOURCE_ALIAS ${alias}.xpm)
        list(APPEND VTM_TEXTURE_FILES ${path})
    endforeach()
    qt_add_resources(vtm_ui "textures" PREFIX "/textures" FILES ${VTM_TEXTURE_FILES})

    set(VTM_BURGER ${VTM_LEGACY_ROOT}/assets/default_images/burger.png)
    set_source_files_properties(${VTM_BURGER} PROPERTIES QT_RESOURCE_ALIAS burger.png)
    qt_add_resources(vtm_ui "images" PREFIX "/images" FILES ${VTM_BURGER})
else()
    message(WARNING
        "Qt6 (Core Gui Quick QuickControls2 Sql >= 6.8) development files not found: "
        "building core library and tests only. "
        "Install qt6-qtbase-devel and qt6-qtdeclarative-devel to build vtmodern.")
endif()

# ---------------------------------------------------------------------------
# Tests (Catch2 v3, vendored in the legacy tree's external/catch2)
# ---------------------------------------------------------------------------
if(VTM_BUILD_TESTS)
    enable_testing()
    if(NOT TARGET Catch2::Catch2WithMain)
        add_subdirectory(${VTM_LEGACY_ROOT}/external/catch2
                         ${CMAKE_CURRENT_BINARY_DIR}/catch2 EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory(tests)
endif()
