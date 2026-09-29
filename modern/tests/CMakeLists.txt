set(VTM_TEST_SOURCES test_money.cpp test_check.cpp)
if(TARGET vtm_layout)
    list(APPEND VTM_TEST_SOURCES
        main.cpp   # QCoreApplication for Qt plugins
        test_layout.cpp test_navigator.cpp test_seed.cpp test_controller.cpp
        test_editor.cpp test_store.cpp test_edit_mode.cpp test_editor_ui.cpp
        test_pos_service.cpp test_pos_store.cpp test_pos_flow.cpp
        test_print.cpp test_pos_m4.cpp test_pages_ui.cpp
    )
endif()

add_executable(vtm_tests ${VTM_TEST_SOURCES})
target_link_libraries(vtm_tests PRIVATE vtm_core)
if(NOT TARGET vtm_layout)
    target_link_libraries(vtm_tests PRIVATE Catch2::Catch2WithMain)
else()
    target_link_libraries(vtm_tests PRIVATE Catch2::Catch2)
    find_package(Qt6 REQUIRED COMPONENTS Qml Test)
    target_link_libraries(vtm_tests PRIVATE vtm_ui vtm_uiplugin Qt6::Test)
    target_compile_definitions(vtm_tests PRIVATE VTM_SEED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../seed")
endif()

include(${VTM_LEGACY_ROOT}/external/catch2/extras/Catch.cmake)
catch_discover_tests(vtm_tests)
