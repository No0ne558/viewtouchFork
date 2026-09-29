add_executable(vtm_tests
    test_money.cpp
)
target_link_libraries(vtm_tests PRIVATE vtm_core Catch2::Catch2WithMain)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../external/catch2/extras/Catch.cmake)
catch_discover_tests(vtm_tests)
