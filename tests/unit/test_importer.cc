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

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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
