/*
 * Tests for backend selection and the rollback path.
 *
 * Two properties matter more than the rest here, and both are about what
 * happens when something is wrong rather than when everything works:
 *
 *   An unrecognised or incomplete configuration degrades to legacy files.
 *   Sites that have never heard of this setting are the overwhelming majority
 *   and must keep behaving exactly as they do now.
 *
 *   A configuration that asks for a database it cannot open FAILS. It does not
 *   fall back. A site that asked for SQLite and silently got files would write
 *   a day's takings somewhere nobody is looking for them.
 */

#include <catch2/catch_all.hpp>
#include "main/data/store/backend_config.hh"

#include "main/data/system.hh"
#include "support/vt_test_env.hh"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace vt::store;

namespace {

struct ConfigFixture : vt_test::VtSystemFixture
{
    fs::path config;
    std::string db;

    explicit ConfigFixture(const std::string &name)
        : config(fs::temp_directory_path() / (name + ".conf")),
          db((fs::temp_directory_path() / (name + ".vtdb")).string())
    {
        Clean();
    }
    ~ConfigFixture() { Clean(); }

    void Clean() const
    {
        std::error_code ec;
        fs::remove(config, ec);
        for (const char *suffix : {"", "-wal", "-shm"})
            fs::remove(db + suffix, ec);
    }

    void Write(const std::string &body) const
    {
        std::ofstream out(config);
        out << body;
    }
};

} // namespace

TEST_CASE("Backend mode parsing refuses to guess", "[cutover][config]")
{
    bool recognised = false;

    REQUIRE(ParseBackendMode("legacy", recognised) == BackendMode::Legacy);
    REQUIRE(recognised);
    REQUIRE(ParseBackendMode("dual", recognised) == BackendMode::Dual);
    REQUIRE(recognised);
    REQUIRE(ParseBackendMode("sqlite", recognised) == BackendMode::Sqlite);
    REQUIRE(recognised);

    SECTION("case does not matter")
    {
        REQUIRE(ParseBackendMode("SQLite", recognised) == BackendMode::Sqlite);
        REQUIRE(recognised);
    }

    SECTION("an unknown mode degrades to legacy and says so")
    {
        // Not an enum cast of whatever was typed, and not a hard failure: a
        // typo in a config file must leave the till working.
        REQUIRE(ParseBackendMode("postgres", recognised) == BackendMode::Legacy);
        REQUIRE_FALSE(recognised);

        REQUIRE(ParseBackendMode("", recognised) == BackendMode::Legacy);
        REQUIRE_FALSE(recognised);
    }

    SECTION("every mode has a name that round-trips")
    {
        for (BackendMode mode : {BackendMode::Legacy, BackendMode::Dual,
                                 BackendMode::Sqlite})
        {
            bool ok = false;
            REQUIRE(ParseBackendMode(BackendModeName(mode), ok) == mode);
            REQUIRE(ok);
        }
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "A site that has never configured this keeps its behaviour",
                 "[cutover][config][default]")
{
    // The single most important case. Reading a config file that does not
    // exist, or has no persistence section, must produce legacy files.
    const BackendSettings missing =
        ReadBackendSettings("/nonexistent/vt-does-not-exist.conf");
    REQUIRE(missing.mode == BackendMode::Legacy);

    StoreError error = StoreError::Constraint;
    auto store = MakeConfiguredStore(missing, MasterSystem.get(), error);
    REQUIRE(error == StoreError::Ok);
    REQUIRE(store != nullptr);
    REQUIRE(std::string(store->Name()) == "legacy-file");
}

TEST_CASE("Configuration selects the backend it names", "[cutover][config]")
{
    SECTION("dual mode wraps both, with legacy authoritative")
    {
        ConfigFixture fixture("vt_cutover_dual");
        fixture.Write("[persistence]\nmode = dual\ndatabase_path = " +
                      fixture.db + "\n");

        const BackendSettings settings =
            ReadBackendSettings(fixture.config.string());
        REQUIRE(settings.mode == BackendMode::Dual);
        REQUIRE(settings.database_path == fixture.db);

        StoreError error = StoreError::Constraint;
        auto store = MakeConfiguredStore(settings, MasterSystem.get(), error);
        REQUIRE(error == StoreError::Ok);
        REQUIRE(store != nullptr);
        REQUIRE(std::string(store->Name()) == "dual(legacy-file + sqlite)");

        // Still reports the legacy guarantees. A dual run does not gain
        // atomicity just by having a backend that offers it.
        REQUIRE_FALSE(store->SupportsAtomicWrites());
    }

    SECTION("sqlite mode is authoritative and says so")
    {
        ConfigFixture fixture("vt_cutover_sqlite");
        fixture.Write("[persistence]\nmode = sqlite\ndatabase_path = " +
                      fixture.db + "\n");

        const BackendSettings settings =
            ReadBackendSettings(fixture.config.string());
        REQUIRE(settings.mode == BackendMode::Sqlite);

        StoreError error = StoreError::Constraint;
        auto store = MakeConfiguredStore(settings, MasterSystem.get(), error);
        REQUIRE(error == StoreError::Ok);
        REQUIRE(store != nullptr);
        REQUIRE(std::string(store->Name()) == "sqlite");
        REQUIRE(store->SupportsAtomicWrites());
    }
}

TEST_CASE("A misconfiguration degrades safely or fails loudly, never quietly",
          "[cutover][config][safety]")
{
    SECTION("a database mode with no path degrades to legacy")
    {
        // Asking for a database without saying where is a configuration error,
        // not something to guess a path for. Files keep working.
        ConfigFixture fixture("vt_cutover_nopath");
        fixture.Write("[persistence]\nmode = sqlite\n");

        const BackendSettings settings =
            ReadBackendSettings(fixture.config.string());
        REQUIRE(settings.mode == BackendMode::Legacy);
    }

    SECTION("an unopenable database fails rather than falling back")
    {
        // The case this test exists for. Falling back to files here would mean
        // a site that asked for SQLite writes a day's takings somewhere nobody
        // is looking for them, and finds out at the next reconciliation.
        BackendSettings settings;
        settings.mode = BackendMode::Sqlite;
        settings.database_path = "/nonexistent-directory/vt.db";

        StoreError error = StoreError::Ok;
        auto store = MakeConfiguredStore(settings, MasterSystem.get(), error);
        REQUIRE(store == nullptr);
        REQUIRE(error != StoreError::Ok);
    }

    SECTION("the same is true in dual mode")
    {
        BackendSettings settings;
        settings.mode = BackendMode::Dual;
        settings.database_path = "/nonexistent-directory/vt.db";

        StoreError error = StoreError::Ok;
        auto store = MakeConfiguredStore(settings, MasterSystem.get(), error);
        REQUIRE(store == nullptr);
        REQUIRE(error != StoreError::Ok);
    }
}

TEST_CASE("Rolling back from dual mode leaves nothing to undo",
          "[cutover][rollback]")
{
    // Dual keeps files authoritative, so reverting is a config edit. This is
    // what makes dual the mode a site should sit in for a long time: it is the
    // only one where the new backend is exercised on real data and rollback
    // still costs nothing.
    ConfigFixture fixture("vt_rollback_dual");
    fixture.Write("[persistence]\nmode = dual\ndatabase_path = " +
                  fixture.db + "\n");

    {
        const BackendSettings dual = ReadBackendSettings(fixture.config.string());
        REQUIRE(dual.mode == BackendMode::Dual);

        StoreError error = StoreError::Constraint;
        auto store = MakeConfiguredStore(dual, MasterSystem.get(), error);
        REQUIRE(error == StoreError::Ok);
        REQUIRE(store != nullptr);
    }

    // The rollback: change one line.
    fixture.Write("[persistence]\nmode = legacy\n");

    const BackendSettings back = ReadBackendSettings(fixture.config.string());
    REQUIRE(back.mode == BackendMode::Legacy);

    StoreError error = StoreError::Constraint;
    auto store = MakeConfiguredStore(back, MasterSystem.get(), error);
    REQUIRE(error == StoreError::Ok);
    REQUIRE(std::string(store->Name()) == "legacy-file");

    // The database file is left where it is. Deleting it on rollback would
    // throw away the shadow data that justifies trying again.
    REQUIRE(fs::exists(fixture.db));
}
