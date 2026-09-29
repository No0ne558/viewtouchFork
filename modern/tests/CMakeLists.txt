set(VTM_TEST_SOURCES test_money.cpp)
if(TARGET vtm_layout)
    list(APPEND VTM_TEST_SOURCES
        test_layout.cpp test_navigator.cpp test_seed.cpp test_controller.cpp
        # controller is compiled in directly; the app gets it via the QML module
        ../ui/cpp/zonemodel.hh ../ui/cpp/zonemodel.cpp
        ../ui/cpp/layoutcontroller.hh ../ui/cpp/layoutcontroller.cpp
    )
endif()

add_executable(vtm_tests ${VTM_TEST_SOURCES})
target_link_libraries(vtm_tests PRIVATE vtm_core Catch2::Catch2WithMain)
if(TARGET vtm_layout)
    find_package(Qt6 REQUIRED COMPONENTS Qml Test)
    target_include_directories(vtm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../ui/cpp)
    target_link_libraries(vtm_tests PRIVATE vtm_layout Qt6::Qml Qt6::Test)
    target_compile_definitions(vtm_tests PRIVATE VTM_SEED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../seed")
endif()

include(${VTM_LEGACY_ROOT}/external/catch2/extras/Catch.cmake)
catch_discover_tests(vtm_tests)
