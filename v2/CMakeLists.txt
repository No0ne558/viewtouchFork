cmake_minimum_required(VERSION 3.20)
project(ViewTouchV2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Quick)

qt_standard_project_setup()

qt_add_executable(ViewTouchV2
    main.cpp
)

qt_add_qml_module(ViewTouchV2
    URI ViewTouch
    VERSION 1.0
    QML_FILES
        Main.qml
        Terminal.qml
        Zone.qml
        EditToolbar.qml
        PagePropertiesDialog.qml
)

target_link_libraries(ViewTouchV2
    PRIVATE Qt6::Core Qt6::Quick
)

# Copy QML files to build directory for development
add_custom_command(TARGET ViewTouchV2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}
    $<TARGET_FILE_DIR:ViewTouchV2>
)