/*
 * Tests for the legacy -> SQLite importer.
 *
 * Fixtures are built with the production Archive::SavePacked rather than by
 * hand-encoding bytes. That is on purpose: the archive format is gzip-wrapped,
 * carries per-sub-entity version stamps, and has gates back to 1997. A
 * hand-built fixture would be testing the importer against a second
 * implementation of the format, and the cases it got wrong would be exactly the
 * ones nobody notices.
 *
 * The claim these tests exist to hold to account is the one the rollback plan
 * depends on: originals are never modified. That is asserted on the bytes, not
 * taken on trust.
 */

#include <catch2/catch_all.hpp>
#include "main/data/store/importer.hh"

#include "main/business/check.hh"
#include "main/business/sales.hh"
#include "main/data/archive.hh"
#include "main/data/settings.hh"
#include "sql/database.hh"
#include "sql/sequence.hh"
#include "sql/statement.hh"
#include "support/vt_test_env.hh"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include "support/legacy_file_builder.hh"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vt::store;

namespace {

// A directory that cleans itself up, holding the archives under test.
struct ArchiveDir
{
    fs::path path;
    std::string db;

    explicit ArchiveDir(const std::string &name)
        : path(fs::temp_directory_path() / name),
          db((fs::temp_directory_path() / (name + ".vtdb")).string())
    {
        Clean();
        std::error_code ec;
        fs::create_directories(path, ec);
    }
    ~ArchiveDir() { Clean(); }

    void Clean() const
    {
        std::error_code ec;
        fs::remove_all(path, ec);
        for (const char *suffix : {"", "-wal", "-shm"})
            fs::remove(db + suffix, ec);
    }

    [[nodiscard]] std::string File(const std::string &name) const
    {
        return (path / name).string();
    }
};

std::string ReadBytes(const std::string &path)
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

double ScalarDouble(const std::string &db_path, const std::string &sql)
{
    vt::sql::Database db;
    REQUIRE(db.Open(db_path) == vt::sql::Status::Ok);
    vt::sql::Statement stmt;
    REQUIRE(stmt.Prepare(db, sql) == vt::sql::Status::Ok);
    vt::sql::Status step = vt::sql::Status::Ok;
    REQUIRE(stmt.Step(step));
    // No ColumnDouble on Statement; the text form is exact enough to compare
    // against Approx, and going through it here keeps the accessor surface
    // minimal until something other than a test needs it.
    return std::stod(stmt.ColumnText(0));
}

// A closed check with a modifier tree and a payment, so the importer has real
// shape to move rather than a single row.
Check *MakeClosedCheck(int serial, int burger_price)
{
    auto *check = new Check;
    check->serial_number = serial;
    check->type = CHECK_TAKEOUT;
    check->guests = 2;
    check->time_open.Set();

    SubCheck *sub = check->NewSubCheck();
    sub->status = CHECK_CLOSED;      // Archive::Add refuses an open check
    sub->settle_time.Set();

    auto *burger = new Order("Burger", burger_price);
    burger->item_family = FAMILY_BURGERS;
    sub->Add(burger);

    // ITEM_MODIFIER, not just FAMILY_MODIFIER. Order::IsModifier() keys off
    // item_type (check.cc:6159), and the archive round trip rebuilds the tree
    // from it -- so a modifier tagged only by family comes back as a root order.
    auto *cheese = new Order("Add Cheese", 100);
    cheese->item_type = ITEM_MODIFIER;
    cheese->item_family = FAMILY_MODIFIER;
    burger->Add(cheese);

    auto *payment = new Payment(TENDER_CASH, 0, 0, burger_price + 100);
    sub->Add(payment);

    sub->total_sales = burger_price + 100;
    sub->total_tax_food = 90;
    sub->total_cost = burger_price + 190;

    return check;
}

// Writes one archive file through the production writer, with a frozen policy.
void WriteArchive(const std::string &path, Settings &settings, int id,
                  const std::vector<Check *> &checks)
{
    TimeInfo start;
    start.Set();
    Archive archive(start);
    archive.filename.Set(path.c_str());
    archive.id = id;
    archive.end_time.Set();
    archive.CopyPolicyFrom(settings);

    for (Check *check : checks)
        REQUIRE(archive.Add(check) == 0);

    REQUIRE(archive.SavePacked() == 0);

    // Unload() would call SavePacked() again on the way out because Add() set
    // `changed`. The file is already written; clearing the flag stops the
    // destructor rewriting it under the test.
    archive.changed = 0;
}

Settings &TestSettings()
{
    static Settings settings;
    settings.tax_food = 0.09;
    settings.tax_alcohol = 0.11;
    settings.tax_GST = 0.05;
    settings.tax_takeout_food = 1;
    return settings;
}

} // namespace

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "The importer never modifies the originals",
                 "[importer][readonly]")
{
    // The rollback plan is "reinstall the previous version and point it at the
    // data directory it always had". That is only true if this holds, so it is
    // asserted on the bytes rather than on the absence of a write call.
    ArchiveDir dir("vt_import_readonly");
    Settings &settings = TestSettings();

    const std::string file = dir.File("archive_001");
    WriteArchive(file, settings, 1, {MakeClosedCheck(100, 950)});

    const std::string before = ReadBytes(file);
    REQUIRE_FALSE(before.empty());
    const auto before_time = fs::last_write_time(file);

    const ImportResult result = ImportArchives(dir.path.string(), dir.db, settings);
    REQUIRE(result.Ok());
    REQUIRE(result.stats.archives_read == 1);

    REQUIRE(ReadBytes(file) == before);
    REQUIRE(fs::last_write_time(file) == before_time);
    REQUIRE(fs::exists(file));

    // Worth recording from trying to falsify this: forcing `archive.changed = 1`
    // after the load does NOT produce a write. Archive::LoadPacked sets
    // from_disk, and SavePacked refuses outright when it is set. That
    // write-once behaviour is listed elsewhere as a limitation of the legacy
    // format -- an archive can never be rewritten once read back -- and here it
    // is doing useful work as a second layer under the importer's own
    // discipline. Falsifying this assertion took an explicit ofstream on the
    // path; the assertion does fail then, so it is not vacuous.
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "An archive becomes a closed business day with its policy",
                 "[importer][day]")
{
    ArchiveDir dir("vt_import_day");
    Settings &settings = TestSettings();

    const std::string file = dir.File("archive_001");
    WriteArchive(file, settings, 7, {MakeClosedCheck(100, 950)});

    const ImportResult result = ImportArchives(dir.path.string(), dir.db, settings);
    REQUIRE(result.Ok());
    REQUIRE(result.stats.days == 1);

    SECTION("the day is closed, not left open")
    {
        // ux_business_day_open permits exactly one open day, so importing a
        // second archive would fail the index rather than the data if imported
        // days were left open.
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM business_day "
                               "WHERE closed_at_local IS NOT NULL;") == 1);
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM business_day "
                               "WHERE closed_at_local IS NULL;") == 0);
    }

    SECTION("provenance is recorded so the import is auditable")
    {
        REQUIRE(Scalar(dir.db, "SELECT src_file_version FROM business_day;")
                == ARCHIVE_VERSION);
        REQUIRE(Scalar(dir.db, "SELECT src_check_version FROM business_day;")
                == CHECK_VERSION);
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM business_day "
                               "WHERE legacy_filename IS NOT NULL;") == 1);
    }

    SECTION("tax rates survive as REAL, not as rounded text")
    {
        REQUIRE(ScalarDouble(dir.db, "SELECT tax_food FROM day_policy;")
                == Catch::Approx(0.09));
        REQUIRE(ScalarDouble(dir.db, "SELECT tax_alcohol FROM day_policy;")
                == Catch::Approx(0.11));
        REQUIRE(ScalarDouble(dir.db, "SELECT tax_GST FROM day_policy;")
                == Catch::Approx(0.05));
    }

    SECTION("the policy snapshot is marked incomplete")
    {
        // EndDay never copied tax_VAT or advertise_fund, so every archive ever
        // written holds zero for both. Recording that lets a report distinguish
        // "the rate was zero" from "the rate was never captured" -- which the
        // legacy format could not express at all.
        REQUIRE(Scalar(dir.db, "SELECT snapshot_complete FROM day_policy;") == 0);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "Checks arrive with their tree and their money intact",
                 "[importer][checks]")
{
    ArchiveDir dir("vt_import_checks");
    Settings &settings = TestSettings();

    WriteArchive(dir.File("archive_001"), settings, 1,
                 {MakeClosedCheck(100, 950), MakeClosedCheck(101, 1250)});

    const ImportResult result = ImportArchives(dir.path.string(), dir.db, settings);
    REQUIRE(result.Ok());

    SECTION("every check, subcheck, order and payment is accounted for")
    {
        REQUIRE(result.stats.checks == 2);
        REQUIRE(result.stats.subchecks == 2);
        REQUIRE(result.stats.orders == 2);
        REQUIRE(result.stats.modifiers == 2);
        REQUIRE(result.stats.payments == 2);

        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM pos_check;") == 2);
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM order_item;") == 4);
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM payment;") == 2);
    }

    SECTION("the modifier tree is explicit after the round trip")
    {
        // The archive stored a flat parents-then-modifiers run and the reader
        // rebuilt the tree from adjacency. This is where that inference stops:
        // the relationship is a column from here on.
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM order_item "
                               "WHERE parent_order_id IS NULL;") == 2);
        REQUIRE(Scalar(dir.db,
                       "SELECT COUNT(*) FROM order_item o "
                       "JOIN order_item p ON p.id = o.parent_order_id "
                       "WHERE p.item_name = 'Burger';") == 2);
    }

    SECTION("imported totals are frozen and marked as recomputed")
    {
        // Frozen, so they stop drifting: the legacy code recomputed them on
        // every single load, which is why opening a historical report could
        // restate a closed day.
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM subcheck "
                               "WHERE frozen_at_local IS NOT NULL;") == 2);

        // source = 2, "recomputed during import", not 1, "imported as found".
        // The legacy format stored no totals at all -- SubCheck's money fields
        // are derived and SubCheck::Read calls FigureTotals at the end of every
        // load. What lands here is what today's engine computes, not the figure
        // that was on the receipt. Claiming otherwise would give PR 14's
        // dual-run a baseline that never existed.
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM subcheck_total "
                               "WHERE source = 2;") == 2);
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM subcheck_total "
                               "WHERE source = 1;") == 0);

        // The recomputed total is nonzero and consistent across the two rows,
        // which is all that can honestly be asserted -- the pre-write values
        // set by this fixture are overwritten by that FigureTotals call.
        REQUIRE(Scalar(dir.db, "SELECT SUM(total_cost) FROM subcheck_total;") > 0);
        REQUIRE(Scalar(dir.db, "SELECT SUM(total_sales) FROM subcheck_total;")
                == (950 + 100) + (1250 + 100));
    }

    SECTION("the serial sequence clears everything imported")
    {
        // Without this the first check written after an import collides with a
        // historical one -- the exact failure the legacy boot-time backwards
        // scan had when the newest archive was empty or pruned.
        int64_t next = 0;
        vt::sql::Database db;
        REQUIRE(db.Open(dir.db) == vt::sql::Status::Ok);
        REQUIRE(vt::sql::PeekSequenceValue(db, vt::sql::kPosSerialSequence, next)
                == vt::sql::Status::Ok);
        REQUIRE(next > 101);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "The importer copes with what real data actually contains",
                 "[importer][robustness]")
{
    Settings &settings = TestSettings();

    SECTION("duplicate serials across days are legal")
    {
        ArchiveDir dir("vt_import_dupdays");
        WriteArchive(dir.File("archive_001"), settings, 1, {MakeClosedCheck(143, 950)});
        WriteArchive(dir.File("archive_002"), settings, 2, {MakeClosedCheck(143, 800)});

        const ImportResult result = ImportArchives(dir.path.string(), dir.db, settings);
        REQUIRE(result.Ok());
        REQUIRE(result.stats.days == 2);
        REQUIRE(result.stats.checks == 2);
        // Same serial, different days, no disambiguator needed: serial_number is
        // unique only within a day, which is the only scope it was ever
        // meaningful in.
        REQUIRE(result.stats.serial_collisions == 0);
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM pos_check "
                               "WHERE serial_number = 143;") == 2);
    }

    SECTION("duplicate serials within one day get a disambiguator")
    {
        ArchiveDir dir("vt_import_dupday");
        WriteArchive(dir.File("archive_001"), settings, 1,
                     {MakeClosedCheck(143, 950), MakeClosedCheck(143, 800)});

        const ImportResult result = ImportArchives(dir.path.string(), dir.db, settings);
        REQUIRE(result.Ok());
        REQUIRE(result.stats.checks == 2);
        REQUIRE(result.stats.serial_collisions == 1);
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM pos_check "
                               "WHERE serial_number = 143;") == 2);
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM pos_check "
                               "WHERE serial_disambiguator = 1;") == 1);
    }

    SECTION("an unreadable archive is reported, not fatal")
    {
        // One truncated file out of a decade must not stop the other 3,649 days
        // migrating -- but the import has to say which one, or the gap is
        // invisible in the result.
        ArchiveDir dir("vt_import_bad");
        WriteArchive(dir.File("archive_001"), settings, 1, {MakeClosedCheck(100, 950)});
        {
            std::ofstream bad(dir.File("archive_002"), std::ios::binary);
            bad << "this is not an archive";
        }
        WriteArchive(dir.File("archive_003"), settings, 3, {MakeClosedCheck(300, 700)});

        const ImportResult result = ImportArchives(dir.path.string(), dir.db, settings);
        REQUIRE(result.Ok());
        REQUIRE(result.stats.archives_read == 2);
        REQUIRE(result.stats.archives_failed == 1);
        REQUIRE(result.failures.size() == 1);
        REQUIRE(result.failures.front().find("archive_002") != std::string::npos);
        REQUIRE(result.stats.checks == 2);
    }

    SECTION("re-running skips what is already imported")
    {
        // The first thing anyone does after a partial import is run it again.
        ArchiveDir dir("vt_import_rerun");
        WriteArchive(dir.File("archive_001"), settings, 1, {MakeClosedCheck(100, 950)});

        const ImportResult first = ImportArchives(dir.path.string(), dir.db, settings);
        REQUIRE(first.Ok());
        REQUIRE(first.stats.days == 1);

        const ImportResult second = ImportArchives(dir.path.string(), dir.db, settings);
        REQUIRE(second.Ok());
        REQUIRE(second.stats.days == 0);
        REQUIRE(second.stats.checks == 0);

        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM business_day;") == 1);
        REQUIRE(Scalar(dir.db, "SELECT COUNT(*) FROM pos_check;") == 1);
    }

    SECTION("an empty archive directory is not an error")
    {
        ArchiveDir dir("vt_import_empty");
        const ImportResult result = ImportArchives(dir.path.string(), dir.db, settings);
        REQUIRE(result.Ok());
        REQUIRE(result.stats.days == 0);
    }
}


namespace {

struct ToolRun
{
    int exit_code{-1};
    std::string output;
};

// Run the vt_import binary and capture what an operator would see.
ToolRun RunImportTool(const std::vector<std::string> &args)
{
    int pipe_fds[2] = {-1, -1};
    REQUIRE(::pipe(pipe_fds) == 0);

    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(VT_IMPORT_PATH));
    for (const std::string &arg : args)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid == 0)
    {
        ::close(pipe_fds[0]);
        ::dup2(pipe_fds[1], STDOUT_FILENO);
        ::dup2(pipe_fds[1], STDERR_FILENO);
        ::close(pipe_fds[1]);
        ::execv(VT_IMPORT_PATH, argv.data());
        ::_exit(127);
    }

    ::close(pipe_fds[1]);
    REQUIRE(pid > 0);

    ToolRun run;
    char chunk[4096];
    ssize_t got = 0;
    while ((got = ::read(pipe_fds[0], chunk, sizeof(chunk))) > 0)
        run.output.append(chunk, static_cast<std::size_t>(got));
    ::close(pipe_fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    if (WIFEXITED(status))
        run.exit_code = WEXITSTATUS(status);
    return run;
}

// A data directory shaped the way vt_import expects to find one.
struct DataDir
{
    fs::path root;
    fs::path archives;
    std::string db;

    explicit DataDir(const std::string &name)
        : root(fs::temp_directory_path() / name),
          archives(root / "archive"),
          db((root / "viewtouch.db").string())
    {
        Clean();
        std::error_code ec;
        fs::create_directories(archives, ec);
    }
    ~DataDir() { Clean(); }

    void Clean() const
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void WriteConfig() const
    {
        std::ofstream out(root / "persistence.conf");
        out << "[persistence]\nmode = dual\ndatabase_path = " << db << "\n";
    }
};

} // namespace

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "The vt_import tool migrates a data directory",
                 "[importer][tool]")
{
    // Exercises the binary an operator actually runs, not just the function it
    // wraps. Argument handling, the config lookup and the exit codes are what
    // they meet first, and none of them are covered by calling ImportArchives()
    // directly -- which is how docs/SQL_MIGRATION.md came to describe a step
    // that could not be performed.
    DataDir data("vt_import_tool");
    Settings &settings = TestSettings();

    WriteArchive((data.archives / "archive_001").string(), settings, 1,
                 {MakeClosedCheck(100, 950), MakeClosedCheck(101, 1250)});
    WriteArchive((data.archives / "archive_002").string(), settings, 2,
                 {MakeClosedCheck(200, 700)});

    SECTION("it finds the database path in persistence.conf")
    {
        // So an operator cannot type the path differently from what the running
        // system will open.
        data.WriteConfig();

        const ToolRun run = RunImportTool({"--data-path", data.root.string()});
        INFO(run.output);
        REQUIRE(run.exit_code == 0);
        REQUIRE(run.output.find("business days   2") != std::string::npos);
        REQUIRE(run.output.find("checks          3") != std::string::npos);

        REQUIRE(fs::exists(data.db));
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM pos_check;") == 3);
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM business_day;") == 2);
    }

    SECTION("an explicit --database overrides the config")
    {
        const std::string elsewhere = (data.root / "other.db").string();
        const ToolRun run = RunImportTool(
            {"--data-path", data.root.string(), "--database", elsewhere});
        INFO(run.output);
        REQUIRE(run.exit_code == 0);
        REQUIRE(fs::exists(elsewhere));
        REQUIRE_FALSE(fs::exists(data.db));
    }

    SECTION("--dry-run reports without leaving a database behind")
    {
        data.WriteConfig();

        const ToolRun run = RunImportTool(
            {"--data-path", data.root.string(), "--dry-run"});
        INFO(run.output);
        REQUIRE(run.exit_code == 0);
        REQUIRE(run.output.find("Dry run") != std::string::npos);
        REQUIRE(run.output.find("checks          3") != std::string::npos);

        // The whole point: a site can see what an import would do before
        // committing to one.
        REQUIRE_FALSE(fs::exists(data.db));
        REQUIRE_FALSE(fs::exists(data.db + ".dryrun"));
    }

    SECTION("re-running is safe and reports nothing new")
    {
        data.WriteConfig();
        REQUIRE(RunImportTool({"--data-path", data.root.string()}).exit_code == 0);

        const ToolRun again = RunImportTool({"--data-path", data.root.string()});
        INFO(again.output);
        REQUIRE(again.exit_code == 0);
        REQUIRE(again.output.find("business days   0") != std::string::npos);
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM pos_check;") == 3);
    }

    SECTION("an unreadable archive is named and changes the exit code")
    {
        // Exit 2 rather than 0: the import succeeded, but a day is missing and
        // a script that treats 0 as "nothing to look at" would never find out.
        data.WriteConfig();
        {
            std::ofstream bad(data.archives / "archive_003", std::ios::binary);
            bad << "not an archive";
        }

        const ToolRun run = RunImportTool({"--data-path", data.root.string()});
        INFO(run.output);
        REQUIRE(run.exit_code == 2);
        REQUIRE(run.output.find("archives failed 1") != std::string::npos);
        REQUIRE(run.output.find("archive_003") != std::string::npos);
    }

    SECTION("it refuses a data directory with no archives")
    {
        DataDir empty("vt_import_tool_noarchives");
        std::error_code ec;
        fs::remove_all(empty.archives, ec);

        const ToolRun run = RunImportTool(
            {"--data-path", empty.root.string(), "--database", empty.db});
        REQUIRE(run.exit_code == 1);
        REQUIRE(run.output.find("no archive directory") != std::string::npos);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "A site can import its history and then start trading",
                 "[importer][tool][lifecycle]")
{
    /*
     * The operator's actual sequence, which nothing tested end to end: run
     * vt_import against the archives, then start the POS against the same
     * database and take an order. Every piece of it was covered in isolation
     * and the join between them was not -- which is the shape of the last
     * several defects in this work.
     *
     * Three things have to hold on day one, and none of them are implied by the
     * import and the store each working alone.
     */
    DataDir data("vt_import_lifecycle");
    Settings &settings = TestSettings();

    WriteArchive((data.archives / "archive_001").string(), settings, 1,
                 {MakeClosedCheck(100, 950), MakeClosedCheck(101, 1250)});
    WriteArchive((data.archives / "archive_002").string(), settings, 2,
                 {MakeClosedCheck(200, 700)});
    data.WriteConfig();

    const ToolRun run = RunImportTool({"--data-path", data.root.string()});
    INFO(run.output);
    REQUIRE(run.exit_code == 0);
    REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM pos_check;") == 3);

    // Now the POS starts against the database the import just produced.
    vt::store::StoreError error = vt::store::StoreError::Io;
    auto store = vt::store::MakeSqliteStore(data.db, error);
    REQUIRE(error == vt::store::StoreError::Ok);
    REQUIRE(store->HealthCheck() == vt::store::StoreError::Ok);

    SECTION("today's trading opens a new day rather than reopening an imported one")
    {
        // Every imported archive is a closed day. If opening the store adopted
        // one of them, today's takings would be filed under a date that has
        // already been reported on -- and the frozen totals would refuse the
        // write, so the first check of the day would fail.
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM business_day;") == 3);
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM business_day "
                                "WHERE closed_at_local IS NULL;") == 1);
        REQUIRE(Scalar(data.db,
                       "SELECT COUNT(*) FROM business_day "
                       "WHERE closed_at_local IS NULL AND legacy_filename IS NULL;")
                == 1);
    }

    SECTION("a new check does not collide with imported serials")
    {
        // RaiseSequenceTo after the import is what makes this work. Without it
        // the sequence would still be at 1 and the first check of the day would
        // be handed a serial that already exists in history.
        std::unique_ptr<Check> check(new Check);
        REQUIRE(check->serial_number == 0);

        auto tx = store->Begin();
        REQUIRE(store->Checks().Save(*tx, *check) == vt::store::StoreError::Ok);
        REQUIRE(tx->Commit() == vt::store::StoreError::Ok);

        REQUIRE(check->serial_number > 200);   // clear of everything imported
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM pos_check;") == 4);
    }

    SECTION("imported history stays frozen and is not disturbed")
    {
        // With a subcheck, so it produces a totals row to distinguish from the
        // imported ones. A bare Check has nothing to total.
        std::unique_ptr<Check> check(new Check);
        check->NewSubCheck();
        auto tx = store->Begin();
        REQUIRE(store->Checks().Save(*tx, *check) == vt::store::StoreError::Ok);
        REQUIRE(tx->Commit() == vt::store::StoreError::Ok);

        // The three imported checks are still frozen, still marked recomputed,
        // and still attached to their own days.
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM subcheck "
                                "WHERE frozen_at_local IS NOT NULL;") == 3);
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM subcheck_total "
                                "WHERE source = 2;") == 3);
        // Today's check is live, not imported.
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM subcheck_total "
                                "WHERE source = 0;") == 1);
    }

    SECTION("ending the day rolls forward without touching history")
    {
        REQUIRE(store->EndBusinessDay() == vt::store::StoreError::Ok);

        // Four days now: three imported-and-closed, one just closed, one fresh.
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM business_day;") == 4);
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM business_day "
                                "WHERE closed_at_local IS NULL;") == 1);
        // The imported days keep their provenance.
        REQUIRE(Scalar(data.db, "SELECT COUNT(*) FROM business_day "
                                "WHERE legacy_filename IS NOT NULL;") == 2);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "An archive with no policy of its own is imported and named",
                 "[importer][policy]")
{
    // A day whose frozen tax rates are not in its archive still holds real
    // checks and real money, so it is imported rather than refused. What must
    // not happen is importing it silently: its day_policy row then carries
    // *today's* rates, and a report over that day restates history without
    // saying so.
    //
    // Archive version 11 is where the policy block was introduced, so anything
    // older simply does not have one. The same shape arises from a truncated
    // archive -- writes were neither atomic nor durable until recently and
    // SavePacked rewrites a whole day at once -- and neither reports an error,
    // because InputDataFile::Read(int) cannot return one.
    //
    // The archive is built byte by byte because SavePacked only ever writes
    // ARCHIVE_VERSION; there is no way to produce a version-10 file with the
    // application's own code.
    ArchiveDir dir("vt_import_policy");
    Settings &settings = TestSettings();

    WriteArchive(dir.File("archive_001"), settings, 1, {MakeClosedCheck(100, 950)});

    {
        vt_test::LegacyFileBuilder b;
        b.Int(2);                              // archive id
        b.Time(8 * 3600, 2001);                // start_time (version >= 6)
        b.Time(23 * 3600, 2001, true);         // end_time
        b.Int(DRAWER_VERSION).Int(0);          // drawers
        b.Int(CHECK_VERSION).Int(0);           // checks
        b.Int(1).Int(0);                       // tips
        b.Int(3).Int(0).Int(0).Int(0);         // exceptions: item, table, rebuild
        b.Int(4).Int(0).Int(0);                // expenses: version, entered, count
        b.Int(1);                              // media_version
        for (int i = 0; i < 5; ++i)
            b.Int(0, i == 4);                  // five media counts
        b.WriteTo(dir.File("archive_002"), 10);
    }

    const ImportResult result = ImportArchives(dir.path.string(), dir.db, settings);

    REQUIRE(result.Ok());
    REQUIRE(result.stats.archives_read == 2);
    REQUIRE(result.stats.archives_failed == 0);

    // Exactly one of the two: the version-10 day. The well-formed one carries
    // its own policy and is not counted, which is what makes this a signal
    // rather than a blanket warning on every import.
    REQUIRE(result.stats.policy_not_in_archive == 1);
}
