/*
 * Labor periods through the Store seam.
 *
 * Payroll, and the last money-bearing entity that was file-only on every mode.
 * A labor period is a pay period: it spans many business days and closes on its
 * own schedule, so unlike checks and drawers it hangs off nothing in the day
 * tables.
 *
 * A correction the work turned up, recorded here because the header says the
 * opposite. `WorkDB` carries the comment "will replace LaborPeriod & LaborDB"
 * and is wired to nothing at all -- System::work_db and Archive::work_db are
 * members no code outside those classes touches, and Archive::SavePacked never
 * writes work_db while WorkDB::Save() reports success after marking the archive
 * dirty. LaborDB, marked "obsolete", is the live one. So this migrates
 * LaborPeriod.
 *
 * The same load-bearing claim as for checks and drawers: routing through
 * `legacy` writes byte-identical files.
 */

#include <catch2/catch_all.hpp>

#include "main/business/labor.hh"
#include "main/data/store/snapshot.hh"
#include "main/data/store/store.hh"
#include "main/data/system.hh"
#include "sql/database.hh"
#include "sql/statement.hh"
#include "support/vt_test_env.hh"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vt::store;

namespace {

struct LaborFixture : vt_test::VtSystemFixture
{
    fs::path dir;
    std::string db;

    explicit LaborFixture(const std::string &name)
        : dir(fs::temp_directory_path() / name),
          db((fs::temp_directory_path() / (name + ".vtdb")).string())
    {
        Clean();
        std::error_code ec;
        fs::create_directories(dir, ec);
    }

    ~LaborFixture()
    {
        MasterSystem->SetDataStore(nullptr);
        Clean();
    }

    void Clean() const
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
        for (const char *suffix : {"", "-wal", "-shm"})
            fs::remove(db + suffix, ec);
    }
};

// A period with two shifts: one finished, one still on the clock.
void FillPeriod(LaborPeriod &period, int serial)
{
    period.serial_number = serial;
    period.loaded = 1;
    period.end_time.Clear();      // still open

    auto *finished = new WorkEntry;
    finished->user_id = 21;
    finished->job = 3;
    finished->pay_rate = PERIOD_HOUR;
    finished->pay_amount = 1650;
    finished->tips = 400;
    finished->overtime = 30;
    finished->end_shift = 1;
    finished->start.Set(9 * 3600, 2026);
    finished->end.Set(17 * 3600, 2026);
    REQUIRE(period.Add(finished) == 0);

    auto *on_clock = new WorkEntry;
    on_clock->user_id = 22;
    on_clock->job = 1;
    on_clock->pay_rate = PERIOD_HOUR;
    on_clock->pay_amount = 1500;
    on_clock->start.Set(12 * 3600, 2026);
    on_clock->end.Clear();        // has not clocked out
    REQUIRE(period.Add(on_clock) == 0);
}

std::string ReadBytes(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int64_t Scalar(const std::string &db_path, const std::string &sql)
{
    vt::sql::Database db;
    REQUIRE(db.Open(db_path) == vt::sql::Status::Ok);
    int64_t value = -1;
    REQUIRE(db.QueryInt(sql, value) == vt::sql::Status::Ok);
    return value;
}

} // namespace

TEST_CASE("A routed labor period writes the same bytes as an unrouted one",
          "[labor][store][equivalence]")
{
    // The claim the default configuration rests on, asserted on the file
    // contents. One period object saved down both paths, so nothing but the
    // writer differs -- the same shape the check equivalence test settled on
    // after a version that let the clock in and went flaky.
    LaborFixture fixture("vt_labor_equivalence");

    LaborPeriod period;
    FillPeriod(period, 501);
    period.file_name.Set((fixture.dir / "labor_501").string().c_str());

    REQUIRE(MasterSystem->DataStore() == nullptr);   // fallback path
    REQUIRE(period.Save() == 0);
    const std::string unrouted = ReadBytes(fixture.dir / "labor_501");
    REQUIRE_FALSE(unrouted.empty());

    std::error_code ec;
    fs::remove(fixture.dir / "labor_501", ec);

    MasterSystem->SetDataStore(MakeLegacyFileStore(MasterSystem.get()));
    REQUIRE(MasterSystem->DataStore() != nullptr);   // routed path
    REQUIRE(period.Save() == 0);
    const std::string routed = ReadBytes(fixture.dir / "labor_501");
    REQUIRE_FALSE(routed.empty());

    REQUIRE(routed == unrouted);
}

TEST_CASE("A labor period saved to SQL keeps its shifts", "[labor][store][sqlite]")
{
    LaborFixture fixture("vt_labor_sqlite");

    StoreError error = StoreError::Io;
    auto sqlite = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);
    MasterSystem->SetDataStore(std::move(sqlite));

    LaborPeriod period;
    FillPeriod(period, 502);
    period.file_name.Set((fixture.dir / "labor_502").string().c_str());

    REQUIRE(period.Save() == 0);

    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM labor_period;") == 1);
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM work_entry;") == 2);

    // An open period has no end. NULL rather than zero, because a period that
    // ended at the epoch is a different claim from one that has not ended.
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM labor_period "
                               "WHERE end_time_local IS NULL;") == 1);

    // The finished shift, with the figures that decide what it is worth.
    REQUIRE(Scalar(fixture.db,
                   "SELECT pay_amount FROM work_entry WHERE user_id = 21;") == 1650);
    REQUIRE(Scalar(fixture.db,
                   "SELECT tips FROM work_entry WHERE user_id = 21;") == 400);
    // Overtime is stored, not recomputed: WorkEntry::Update figures it against
    // Settings at save time, and a threshold edited next month must not restate
    // a shift worked last month.
    REQUIRE(Scalar(fixture.db,
                   "SELECT overtime FROM work_entry WHERE user_id = 21;") == 30);
    REQUIRE(Scalar(fixture.db,
                   "SELECT COUNT(*) FROM work_entry "
                   "WHERE user_id = 21 AND end_local IS NOT NULL;") == 1);

    // The shift still running has no end, which is how "on the clock" is
    // representable at all.
    REQUIRE(Scalar(fixture.db,
                   "SELECT COUNT(*) FROM work_entry "
                   "WHERE user_id = 22 AND end_local IS NULL;") == 1);

    // Saving again replaces rather than appends. LaborPeriod::Save rewrites its
    // whole file, so anything else would leave a removed shift behind -- and an
    // edited timesheet is exactly when that matters.
    REQUIRE(period.Save() == 0);
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM labor_period;") == 1);
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM work_entry;") == 2);

    // And a shift removed in memory disappears from the database.
    WorkEntry *first = period.WorkList();
    REQUIRE(first != nullptr);
    REQUIRE(period.Remove(first) == 0);
    delete first;
    REQUIRE(period.Save() == 0);
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM work_entry;") == 1);

    // Closing the period records when.
    period.end_time.Set(23 * 3600, 2026);
    REQUIRE(period.Save() == 0);
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM labor_period "
                               "WHERE end_time_local IS NOT NULL;") == 1);
}

TEST_CASE("The divergence report covers labor periods", "[labor][store][dualrun]")
{
    /*
     * Written at the same time as the writer, not after.
     *
     * Drawers were dual-written for a while before anything compared them, and
     * the report said "no divergence" every night about half the money. That is
     * worse than no coverage: it manufactures confidence. So the rule this test
     * enforces is simply that a dual-written entity appears in the diff.
     *
     * It is asserted by making the two sides genuinely differ and checking the
     * report names it -- a test that only ran a clean comparison would pass just
     * as happily against a diff that never looked at labor at all.
     */
    LaborFixture fixture("vt_labor_dualrun");

    auto legacy = MakeLegacyFileStore(MasterSystem.get());
    StoreError error = StoreError::Io;
    auto sqlite = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);

    Store &legacy_ref = *legacy;
    Store &sqlite_ref = *sqlite;

    LaborPeriod period;
    FillPeriod(period, 503);
    period.file_name.Set((fixture.dir / "labor_503").string().c_str());
    REQUIRE(MasterSystem->labor_db.Add(&period) == 0);

    // Write both sides, then change one shift on the SQL side only.
    {
        auto tx = legacy_ref.Begin();
        REQUIRE(tx != nullptr);
        REQUIRE(legacy_ref.Labor().Save(*tx, period) == StoreError::Ok);
        REQUIRE(tx->Commit() == StoreError::Ok);
    }
    {
        auto tx = sqlite_ref.Begin();
        REQUIRE(tx != nullptr);
        REQUIRE(sqlite_ref.Labor().Save(*tx, period) == StoreError::Ok);
        REQUIRE(tx->Commit() == StoreError::Ok);
    }

    StoreSnapshot left;
    StoreSnapshot right;
    REQUIRE(legacy_ref.Snapshot(left) == StoreError::Ok);
    REQUIRE(sqlite_ref.Snapshot(right) == StoreError::Ok);

    // Both sides describe the period, which is the coverage claim.
    REQUIRE(left.labor.size() == 1);
    REQUIRE(right.labor.size() == 1);

    // Exactly one divergence, and it is a known one: WorkEntry::Write never
    // emitted `overtime`, so the legacy side reads back zero. Finding it here
    // is the point -- an entity nobody compares cannot tell you this.
    //
    // Worth stating plainly because it is payroll: overtime is not persisted
    // by the file format at all, and the only code that assigns it is
    // LaborPeriod::WorkReport, as a side effect of drawing a report line. So a
    // reloaded timesheet reports no overtime until somebody opens that report.
    const std::vector<Divergence> known = Diff(left, right);
    INFO(DescribeDivergence(left, right, known));
    REQUIRE(known.size() == 1);
    REQUIRE(known.front().path == "labor[503].entry[0].overtime");
    REQUIRE_FALSE(known.front().note.empty());

    // Now make them differ, and check the report says where.
    WorkEntry *entry = period.WorkList();
    REQUIRE(entry != nullptr);
    entry->pay_amount = 9999;
    {
        auto tx = sqlite_ref.Begin();
        REQUIRE(tx != nullptr);
        REQUIRE(sqlite_ref.Labor().Save(*tx, period) == StoreError::Ok);
        REQUIRE(tx->Commit() == StoreError::Ok);
    }

    StoreSnapshot changed;
    REQUIRE(sqlite_ref.Snapshot(changed) == StoreError::Ok);
    const std::vector<Divergence> found = Diff(left, changed);

    REQUIRE_FALSE(found.empty());
    bool named = false;
    for (const Divergence &d : found)
    {
        if (d.path.find("labor[503]") != std::string::npos &&
            d.path.find("pay_amount") != std::string::npos)
        {
            named = true;
        }
    }
    INFO(DescribeDivergence(left, changed, found));
    REQUIRE(named);

    REQUIRE(MasterSystem->labor_db.Remove(&period) == 0);
}
