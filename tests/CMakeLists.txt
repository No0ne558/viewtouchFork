# ViewTouch Testing Configuration

# Unit tests
add_executable(vt_tests
    main_test.cc
    unit/test_check.cc
    unit/test_settings.cc
    unit/test_utility.cc
    unit/test_memory_modernization.cc
    unit/test_safe_string_utils.cc
    unit/test_input_validation.cc
    unit/test_enhanced_logging.cc
    unit/test_coupon_calculations.cc
    unit/test_sales_tax_calculations.cc
    unit/test_time_operations.cc
    unit/test_error_handler.cc
    unit/test_list_utility.cc
    unit/test_thread_pool.cc
    unit/test_report_generation.cc
    unit/test_data_persistence.cc
    unit/test_conf_file.cc
    unit/test_safe_math.cc
    unit/test_archive_policy.cc
    unit/test_figure_totals.cc
    unit/test_credit_parsing.cc
    unit/test_persistence_roundtrip.cc
    unit/test_drawer_balance.cc
    unit/test_labor_time.cc
    unit/test_golden_corpus.cc
    unit/test_data_file_durability.cc
    unit/test_sql_migrations.cc
    unit/test_store_seam.cc
    unit/test_sqlite_store.cc
    unit/test_importer.cc
    unit/test_dual_run.cc
    support/vt_test_env.cc
    support/legacy_file_builder.cc
)

# Include directories for tests
target_include_directories(vt_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../main
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/business
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/hardware
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/data
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/../zone
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/network
    ${CMAKE_CURRENT_SOURCE_DIR}/../external
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/../_deps/magic_enum-src/include
    ${CMAKE_CURRENT_BINARY_DIR}/../_deps/spdlog-src/include
    ${CMAKE_CURRENT_BINARY_DIR}/../_deps/json-src/include
)

# Link libraries
target_link_libraries(vt_tests PRIVATE
    vtbusiness
    vtcore
    zone
    Catch2::Catch2WithMain
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    magic_enum::magic_enum
)

# Test discovery
include(Catch)

# Make UndefinedBehaviorSanitizer findings fail the test that triggered them.
#
# UBSan defaults to "recover": it prints a diagnostic and carries on, so a test
# could sail past a genuine out-of-bounds access with a green result. That is
# how an overflow in Credit::ParseTrack2 stayed invisible -- the diagnostic was
# printed on stderr and the run still reported success.
#
# This applies to the test run only. The application keeps the recovering
# default deliberately: aborting a live till on a UBSan diagnostic would be
# worse than continuing, whereas in tests a diagnostic is exactly what we want
# to catch.
catch_discover_tests(vt_tests
    PROPERTIES
        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:abort_on_error=1"
)

# Integration tests (future)
# add_subdirectory(integration)
