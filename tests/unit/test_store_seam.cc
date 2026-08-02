/*
 * Tests for the persistence seam (main/data/store/store.hh).
 *
 * These characterize the interface every backend has to honour, using the
 * legacy file backend as the first implementation. When the SQL backend lands,
 * the same expectations should hold for it -- with the deliberate exception of
 * SupportsAtomicWrites(), which is the one place the two genuinely differ and
 * the reason the flag exists.
 *
 * The legacy backend is a pure delegation to System, so a behaviour change here
 * would mean the seam itself altered what gets written, which would invalidate
 * the dual-run comparison it exists to enable.
 */

#include <catch2/catch_all.hpp>
#include "main/data/store/store.hh"

#include "main/business/check.hh"
#include "main/data/archive.hh"
#include "main/data/system.hh"
#include "support/vt_test_env.hh"

#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace vt::store;

namespace {

/*
 * Points System::current_path at an empty directory for the duration of a test
 * and puts it back afterwards.
 *
 * MasterSystem is process-wide and shared between test cases (see
 * vt_test_env.hh), so a test that redirects where checks are written has to
 * restore it or the next case inherits a path that no longer exists.
 */
struct ScopedCurrentPath
{
    fs::path dir;
    std::string previous;

    explicit ScopedCurrentPath(System *system, const std::string &name)
        : dir(fs::temp_directory_path() / name), system_(system)
    {
        previous = system_->current_path.Value() ? system_->current_path.Value() : "";
        std::error_code ec;
        fs::create_directories(dir, ec);
        system_->current_path.Set(dir.string().c_str());
    }

    ~ScopedCurrentPath()
    {
        system_->current_path.Set(previous.c_str());
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

private:
    System *system_;
};

// Counts everything the store could have written, so "nothing was written" is
// an assertion about the directory rather than about one predicted filename.
int FilesIn(const fs::path &dir)
{
    int count = 0;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec))
    {
        (void)entry;
        ++count;
    }
    return count;
}

} // namespace

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "The legacy store reports its own guarantees honestly",
                 "[store][seam]")
{
    auto store = MakeLegacyFileStore(MasterSystem.get());
    REQUIRE(store != nullptr);

    SECTION("it names itself for logs and divergence reports")
    {
        REQUIRE(std::string(store->Name()) == "legacy-file");
    }

    SECTION("it does not claim atomic writes")
    {
        // The legacy format writes one file per check with no way to group
        // them, so EndDay touching a dozen of them can still be interrupted
        // halfway. Modelling that honestly is what keeps the two backends
        // comparable instead of implicitly assumed equivalent -- gaining this
        // guarantee is the single largest thing the migration buys.
        REQUIRE_FALSE(store->SupportsAtomicWrites());
    }

    SECTION("health check passes with a live system")
    {
        REQUIRE(store->HealthCheck() == StoreError::Ok);
    }

    SECTION("health check fails without one")
    {
        auto orphan = MakeLegacyFileStore(nullptr);
        REQUIRE(orphan->HealthCheck() == StoreError::Io);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Transactions exist on every backend, even where they are inert",
                 "[store][seam][transaction]")
{
    auto store = MakeLegacyFileStore(MasterSystem.get());

    SECTION("a transaction can be begun and committed")
    {
        auto tx = store->Begin();
        REQUIRE(tx != nullptr);
        REQUIRE(tx->IsActive());
        REQUIRE(tx->Commit() == StoreError::Ok);
        REQUIRE_FALSE(tx->IsActive());
    }

    SECTION("rollback is accepted even though it cannot undo anything here")
    {
        // Callers are written against the interface, so rollback has to be
        // callable on every backend. On this one it clears the transaction and
        // does nothing else, because the writes already reached disk
        // individually -- which is exactly what SupportsAtomicWrites() reports.
        auto tx = store->Begin();
        REQUIRE(tx->IsActive());
        tx->Rollback();
        REQUIRE_FALSE(tx->IsActive());
    }

    SECTION("transactions are independent")
    {
        auto first = store->Begin();
        auto second = store->Begin();
        REQUIRE(first->Commit() == StoreError::Ok);
        REQUIRE(second->IsActive());
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "The check repository round-trips through the seam",
                 "[store][seam][checks]")
{
    System *system = MasterSystem.get();
    auto store = MakeLegacyFileStore(system);
    auto tx = store->Begin();

    SECTION("a saved check reaches disk and Count sees it")
    {
        ScopedCurrentPath current(system, "vt_store_roundtrip");
        REQUIRE(FilesIn(current.dir) == 0);

        int before = -1;
        REQUIRE(store->Checks().Count(before) == StoreError::Ok);
        REQUIRE(before >= 0);

        // Heap-allocated and handed to System, because Remove() destroys it --
        // see the ownership note on CheckRepository::Remove.
        Check *check = new Check;
        REQUIRE(system->Add(check) == 0);

        REQUIRE(store->Checks().Save(*tx, *check) == StoreError::Ok);
        REQUIRE(FilesIn(current.dir) == 1);
        REQUIRE(fs::exists(check->filename.Value()));

        int after = -1;
        REQUIRE(store->Checks().Count(after) == StoreError::Ok);
        REQUIRE(after == before + 1);

        // Remove unlinks the file and destroys the object, so nothing may touch
        // `check` past this point.
        REQUIRE(store->Checks().Remove(*tx, *check) == StoreError::Ok);
        REQUIRE(FilesIn(current.dir) == 0);

        int removed = -1;
        REQUIRE(store->Checks().Count(removed) == StoreError::Ok);
        REQUIRE(removed == before);
    }

    SECTION("a copy is accepted but not written, matching Check::Save")
    {
        // Check::Save returns success without writing when `copy` is set, and a
        // copy shares the original's serial number -- so writing one would
        // clobber the original with an uncommitted edit. System::SaveCheck does
        // NOT check this itself; delegating straight to it was the first
        // version of this backend and this assertion is what caught it.
        ScopedCurrentPath current(system, "vt_store_copy");

        Check check;
        check.copy = 1;
        REQUIRE(store->Checks().Save(*tx, check) == StoreError::Ok);
        REQUIRE(FilesIn(current.dir) == 0);
    }

    SECTION("an archived check marks its archive dirty instead of writing")
    {
        // Archived checks belong to a whole-day file that gets rewritten as a
        // unit. Writing one individually would produce a stray current/ file
        // that the next load would resurrect as an open check.
        ScopedCurrentPath current(system, "vt_store_archived");

        TimeInfo when;
        when.Set();
        Archive archive(when);
        archive.changed = 0;

        Check check;
        check.archive = &archive;

        REQUIRE(store->Checks().Save(*tx, check) == StoreError::Ok);
        REQUIRE(archive.changed == 1);
        REQUIRE(FilesIn(current.dir) == 0);

        check.archive = nullptr;   // do not leave a dangling pointer behind

        // ~Archive() calls Unload(), which calls SavePacked() when `changed` is
        // set. This archive was never loaded from a file and so has no filename
        // to save to; clearing the flag keeps teardown from attempting a write
        // that would only fail. The assertion above already recorded the flag.
        archive.changed = 0;
    }

    SECTION("a store with no system reports an I/O error rather than crashing")
    {
        auto orphan = MakeLegacyFileStore(nullptr);
        auto orphan_tx = orphan->Begin();

        Check check;
        REQUIRE(orphan->Checks().Save(*orphan_tx, check) == StoreError::Io);
        REQUIRE(orphan->Checks().Remove(*orphan_tx, check) == StoreError::Io);

        int count = -1;
        REQUIRE(orphan->Checks().Count(count) == StoreError::Io);
    }
}

TEST_CASE("Store errors all have names", "[store][seam][errors]")
{
    // The dual-run divergence report prints these, so a missing case would show
    // up as "unknown" in exactly the output someone is relying on to diagnose a
    // mismatch.
    REQUIRE(std::string(StoreErrorName(StoreError::Ok)) == "ok");
    REQUIRE(std::string(StoreErrorName(StoreError::NotFound)) != "unknown");
    REQUIRE(std::string(StoreErrorName(StoreError::Io)) != "unknown");
    REQUIRE(std::string(StoreErrorName(StoreError::Corrupt)) != "unknown");
    REQUIRE(std::string(StoreErrorName(StoreError::Constraint)) != "unknown");
    REQUIRE(std::string(StoreErrorName(StoreError::Busy)) != "unknown");
    REQUIRE(std::string(StoreErrorName(StoreError::Unsupported)) != "unknown");
}
