/*
 * Unit tests for DataPersistenceManager (src/core/data_persistence_manager.cc)
 *
 * 859 lines at 0% coverage, and the subject of the June 2026 fixes for printing
 * and saving freezing under heavy load. This file covers the parts reachable
 * without a live System: dirty tracking, the critical-data registry, save and
 * validation callbacks, configuration, logging, and file integrity checks.
 *
 * The save/validate paths that walk real checks and archives (SaveAllChecks,
 * ValidateChecks, ...) need a System, which lives in the vt_main executable and
 * therefore cannot be linked by a test yet. Those are deliberately not covered
 * here; they become reachable once the business logic is extracted into a
 * library.
 *
 * DataPersistenceManager is a process-wide singleton with no deregistration
 * API, so registrations accumulate across test cases. Every test therefore uses
 * uniquely-named data items and asserts only on its own counters, never on the
 * total number of registered items.
 */

#include <catch2/catch_all.hpp>
#include "src/core/data_persistence_manager.hh"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

namespace {

// Unique suffix per registration so accumulated singleton state cannot make one
// test observe another's data items.
std::string UniqueName(const char* prefix)
{
    static std::atomic<int> counter{0};
    return std::string(prefix) + "_" + std::to_string(counter.fetch_add(1));
}

// Writes a temp file and removes it on destruction.
struct TempFile
{
    fs::path path;

    explicit TempFile(const std::string& name, const std::string& contents)
        : path(fs::temp_directory_path() / name)
    {
        std::ofstream out(path, std::ios::binary);
        out << contents;
    }

    ~TempFile()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

} // namespace

TEST_CASE("DataPersistenceManager dirty tracking", "[persistence][dirty]")
{
    auto& dpm = DataPersistenceManager::GetInstance();

    SECTION("registered data starts clean")
    {
        const std::string name = UniqueName("checks");
        dpm.RegisterCriticalData(
            name,
            [] { return DataPersistenceManager::VALIDATION_SUCCESS; },
            [] { return DataPersistenceManager::SAVE_SUCCESS; });

        REQUIRE_FALSE(dpm.IsDataDirty(name));
    }

    SECTION("MarkDataDirty and MarkDataClean round-trip")
    {
        const std::string name = UniqueName("checks");
        dpm.RegisterCriticalData(
            name,
            [] { return DataPersistenceManager::VALIDATION_SUCCESS; },
            [] { return DataPersistenceManager::SAVE_SUCCESS; });

        dpm.MarkDataDirty(name);
        REQUIRE(dpm.IsDataDirty(name));

        dpm.MarkDataClean(name);
        REQUIRE_FALSE(dpm.IsDataDirty(name));
    }

    SECTION("marking dirty is idempotent")
    {
        const std::string name = UniqueName("checks");
        dpm.RegisterCriticalData(
            name,
            [] { return DataPersistenceManager::VALIDATION_SUCCESS; },
            [] { return DataPersistenceManager::SAVE_SUCCESS; });

        dpm.MarkDataDirty(name);
        dpm.MarkDataDirty(name);
        REQUIRE(dpm.IsDataDirty(name));

        dpm.MarkDataClean(name);
        REQUIRE_FALSE(dpm.IsDataDirty(name));
    }

    SECTION("unknown data names are handled without crashing")
    {
        // Check::Save() calls MarkDataDirty("checks") unconditionally, including
        // before Initialize() has registered anything, so these must be no-ops
        // rather than faults.
        REQUIRE_FALSE(dpm.IsDataDirty("no_such_data_item"));
        dpm.MarkDataDirty("no_such_data_item");
        dpm.MarkDataClean("no_such_data_item");
        REQUIRE_FALSE(dpm.IsDataDirty("no_such_data_item"));
    }

    SECTION("data items are tracked independently")
    {
        const std::string a = UniqueName("alpha");
        const std::string b = UniqueName("beta");
        auto ok_validate = [] { return DataPersistenceManager::VALIDATION_SUCCESS; };
        auto ok_save = [] { return DataPersistenceManager::SAVE_SUCCESS; };

        dpm.RegisterCriticalData(a, ok_validate, ok_save);
        dpm.RegisterCriticalData(b, ok_validate, ok_save);

        dpm.MarkDataDirty(a);
        REQUIRE(dpm.IsDataDirty(a));
        REQUIRE_FALSE(dpm.IsDataDirty(b));
    }
}

TEST_CASE("DataPersistenceManager invokes registered savers", "[persistence][save]")
{
    auto& dpm = DataPersistenceManager::GetInstance();

    SECTION("SaveCriticalData runs each registered saver")
    {
        auto calls = std::make_shared<std::atomic<int>>(0);
        const std::string name = UniqueName("counted");

        dpm.RegisterCriticalData(
            name,
            [] { return DataPersistenceManager::VALIDATION_SUCCESS; },
            [calls] {
                calls->fetch_add(1);
                return DataPersistenceManager::SAVE_SUCCESS;
            });

        const auto before = calls->load();
        dpm.SaveCriticalData();
        REQUIRE(calls->load() == before + 1);
    }

    SECTION("SaveCriticalData saves clean data too")
    {
        // Documents current behaviour rather than endorsing it: the save loop
        // iterates every registered item and does not consult the dirty flag,
        // so a clean item is still written. Worth revisiting when saves become
        // transactional, but changing it now would be a silent behaviour change.
        auto calls = std::make_shared<std::atomic<int>>(0);
        const std::string name = UniqueName("clean");

        dpm.RegisterCriticalData(
            name,
            [] { return DataPersistenceManager::VALIDATION_SUCCESS; },
            [calls] {
                calls->fetch_add(1);
                return DataPersistenceManager::SAVE_SUCCESS;
            });

        dpm.MarkDataClean(name);
        const auto before = calls->load();
        dpm.SaveCriticalData();
        REQUIRE(calls->load() == before + 1);
    }

    SECTION("the worst saver result wins")
    {
        const std::string ok = UniqueName("ok");
        const std::string bad = UniqueName("bad");

        dpm.RegisterCriticalData(
            ok,
            [] { return DataPersistenceManager::VALIDATION_SUCCESS; },
            [] { return DataPersistenceManager::SAVE_SUCCESS; });
        dpm.RegisterCriticalData(
            bad,
            [] { return DataPersistenceManager::VALIDATION_SUCCESS; },
            [] { return DataPersistenceManager::SAVE_FAILED; });

        REQUIRE(dpm.SaveCriticalData() == DataPersistenceManager::SAVE_FAILED);
    }
}

TEST_CASE("DataPersistenceManager configuration round-trips", "[persistence][config]")
{
    auto& dpm = DataPersistenceManager::GetInstance();
    const auto original = dpm.GetConfiguration();

    SECTION("auto-save interval and enable flag are stored")
    {
        dpm.SetAutoSaveInterval(std::chrono::seconds(17));
        REQUIRE(dpm.GetConfiguration().auto_save_interval == std::chrono::seconds(17));

        dpm.EnableAutoSave(false);
        REQUIRE_FALSE(dpm.GetConfiguration().enable_auto_save);

        dpm.EnableAutoSave(true);
        REQUIRE(dpm.GetConfiguration().enable_auto_save);
    }

    SECTION("CUPS check interval is stored")
    {
        dpm.SetCUPSCheckInterval(std::chrono::seconds(23));
        REQUIRE(dpm.GetConfiguration().cups_check_interval == std::chrono::seconds(23));
    }

    SECTION("SetConfiguration replaces the whole config")
    {
        DataPersistenceManager::Configuration cfg = original;
        cfg.auto_save_interval = std::chrono::seconds(45);
        cfg.enable_cups_monitoring = false;
        dpm.SetConfiguration(cfg);

        REQUIRE(dpm.GetConfiguration().auto_save_interval == std::chrono::seconds(45));
        REQUIRE_FALSE(dpm.GetConfiguration().enable_cups_monitoring);
    }

    // Leave the singleton as we found it for any later test.
    dpm.SetConfiguration(original);
}

TEST_CASE("DataPersistenceManager file integrity checks", "[persistence][integrity]")
{
    auto& dpm = DataPersistenceManager::GetInstance();

    SECTION("a normal file passes")
    {
        TempFile file("vt_dpm_ok.dat", "vtpos 0 25\nsome content\n");
        REQUIRE(dpm.VerifyFileIntegrity(file.path.string()));
    }

    SECTION("a missing file fails")
    {
        const auto missing = (fs::temp_directory_path() / "vt_dpm_definitely_missing.dat").string();
        REQUIRE_FALSE(dpm.VerifyFileIntegrity(missing));
    }

    SECTION("an empty file fails")
    {
        // An empty data file is the signature of a truncated write, which is the
        // failure mode the current non-atomic writer produces on a crash.
        TempFile file("vt_dpm_empty.dat", "");
        REQUIRE_FALSE(dpm.VerifyFileIntegrity(file.path.string()));
    }

    SECTION("a directory is not a valid data file")
    {
        REQUIRE_FALSE(dpm.VerifyFileIntegrity(fs::temp_directory_path().string()));
    }
}

TEST_CASE("DataPersistenceManager logging", "[persistence][logging]")
{
    auto& dpm = DataPersistenceManager::GetInstance();

    SECTION("ClearLogs empties both logs")
    {
        dpm.ClearLogs();
        REQUIRE(dpm.GetErrorLog().empty());
        REQUIRE(dpm.GetWarningLog().empty());
        REQUIRE(dpm.GetDetailedErrorLog().empty());
        REQUIRE(dpm.GetDetailedWarningLog().empty());
    }

    SECTION("an integrity report is always produced")
    {
        // Used by operators during incidents; it must never return empty, even
        // with nothing registered and no errors recorded.
        REQUIRE_FALSE(dpm.GenerateIntegrityReport().empty());
    }
}
