/*
 * Drawers through the Store seam.
 *
 * Drawers are the other half of end-of-day reconciliation: checks say what was
 * owed, drawers say what was in the till, and the difference is what a manager
 * signs off on. Until this they were file-only on every mode, so a site running
 * `sqlite` had its checks in a database and its cash counts in files.
 *
 * The same load-bearing claim as for checks applies and is asserted the same
 * way: routing through `legacy` writes byte-identical files.
 */

#include <catch2/catch_all.hpp>

#include "main/data/store/dual_store.hh"
#include "main/data/store/store.hh"
#include "main/data/system.hh"
#include "main/hardware/drawer.hh"
#include "sql/database.hh"
#include "sql/statement.hh"
#include "support/vt_test_env.hh"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vt::store;

namespace {

struct DrawerFixture : vt_test::VtSystemFixture
{
    fs::path dir;
    std::string db;
    std::string previous_path;

    explicit DrawerFixture(const std::string &name)
        : dir(fs::temp_directory_path() / name),
          db((fs::temp_directory_path() / (name + ".vtdb")).string())
    {
        Clean();
        std::error_code ec;
        fs::create_directories(dir, ec);
        PurgeDrawers();

        System *system = MasterSystem.get();
        previous_path = (system->current_path.Value() != nullptr)
                            ? system->current_path.Value() : "";
        system->current_path.Set(dir.string().c_str());
    }

    ~DrawerFixture()
    {
        MasterSystem->SetDataStore(nullptr);
        PurgeDrawers();
        MasterSystem->current_path.Set(previous_path.c_str());
        Clean();
    }

    static void PurgeDrawers()
    {
        System *system = MasterSystem.get();
        while (Drawer *drawer = system->DrawerList())
        {
            if (system->Remove(drawer) != 0)
                break;
            delete drawer;
        }
    }

    void Clean() const
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
        for (const char *suffix : {"", "-wal", "-shm"})
            fs::remove(db + suffix, ec);
    }
};

// A drawer with money out of it and counts against it -- enough shape that a
// mistake in either child table has somewhere to show up.
Drawer *BuildDrawer(int serial, bool balanced)
{
    auto *drawer = new Drawer;
    drawer->serial_number = serial;
    drawer->host.Set("term1");
    drawer->position = 1;
    drawer->number = 2;
    drawer->owner_id = 7;
    drawer->start_time.Set();

    auto *payout = new DrawerPayment;
    payout->tender_type = TENDER_PAID_TIP;
    payout->amount = 500;
    payout->user_id = 7;
    payout->target_id = 9;
    payout->time.Set();
    drawer->Add(payout);

    auto *cash = new DrawerBalance;
    cash->tender_type = TENDER_CASH;
    cash->entered = 12500;
    cash->amount = 12000;    // expected, recomputed on load by the legacy code
    cash->count = 3;
    drawer->Add(cash);

    // A counted tender that came to nothing. The legacy writer drops this row
    // entirely, since it only emits balances whose `entered` is non-zero.
    auto *gift = new DrawerBalance;
    gift->tender_type = TENDER_GIFT;
    gift->entered = 0;
    drawer->Add(gift);

    if (balanced)
    {
        drawer->pull_time.Set();
        drawer->balance_time.Set();
    }
    return drawer;
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

double ScalarDouble(const std::string &db_path, const std::string &sql)
{
    vt::sql::Database db;
    REQUIRE(db.Open(db_path) == vt::sql::Status::Ok);
    vt::sql::Statement stmt;
    REQUIRE(stmt.Prepare(db, sql) == vt::sql::Status::Ok);
    vt::sql::Status status = vt::sql::Status::Ok;
    REQUIRE(stmt.Step(status));
    return stmt.ColumnDouble(0);
}

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

TEST_CASE("Routing a drawer save writes the same bytes as not routing it",
          "[drawer][store][equivalence]")
{
    // Same claim as for checks, and it has to hold for the same reason: a
    // default site's cash records must not change shape on upgrade.
    std::string unrouted;
    std::string routed;

    {
        DrawerFixture fixture("vt_drawer_fallback");
        REQUIRE(MasterSystem->DataStore() == nullptr);

        std::unique_ptr<Drawer> drawer(BuildDrawer(7001, false));
        REQUIRE(drawer->Save() == 0);
        REQUIRE(FilesIn(fixture.dir) == 1);
        unrouted = ReadBytes(fixture.dir / "drawer_7001");
        REQUIRE_FALSE(unrouted.empty());
    }

    {
        DrawerFixture fixture("vt_drawer_legacy");
        MasterSystem->SetDataStore(MakeLegacyFileStore(MasterSystem.get()));
        REQUIRE(MasterSystem->DataStore() != nullptr);

        std::unique_ptr<Drawer> drawer(BuildDrawer(7001, false));
        REQUIRE(drawer->Save() == 0);
        REQUIRE(FilesIn(fixture.dir) == 1);
        routed = ReadBytes(fixture.dir / "drawer_7001");
        REQUIRE_FALSE(routed.empty());
    }

    REQUIRE(routed == unrouted);
}

TEST_CASE("A drawer lands in the right rows", "[drawer][store][sqlite]")
{
    DrawerFixture fixture("vt_drawer_rows");
    StoreError error = StoreError::Io;
    auto store = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);

    std::unique_ptr<Drawer> drawer(BuildDrawer(7100, /*balanced=*/false));

    auto tx = store->Begin();
    REQUIRE(store->Drawers().Save(*tx, *drawer) == StoreError::Ok);
    REQUIRE(tx->Commit() == StoreError::Ok);

    SECTION("the drawer row carries its identity and terminal")
    {
        REQUIRE(Scalar(fixture.db,
                       "SELECT COUNT(*) FROM drawer WHERE serial_number = 7100;") == 1);
        REQUIRE(Scalar(fixture.db, "SELECT number FROM drawer;") == 2);
        REQUIRE(Scalar(fixture.db, "SELECT owner_id FROM drawer;") == 7);
    }

    SECTION("status is derivable from the timestamps, not stored separately")
    {
        // GetStatus() reads status off which timestamps are set. Keeping those
        // and deriving the same way means the status cannot drift from the
        // times that justify it, which a status column would allow.
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer "
                                   "WHERE start_time_local IS NOT NULL;") == 1);
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer "
                                   "WHERE pull_time_local IS NULL;") == 1);
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer "
                                   "WHERE balance_time_local IS NULL;") == 1);
    }

    SECTION("payments out of the till are stored")
    {
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer_payment;") == 1);
        REQUIRE(Scalar(fixture.db, "SELECT amount FROM drawer_payment;") == 500);
        REQUIRE(Scalar(fixture.db, "SELECT tender_type FROM drawer_payment;")
                == TENDER_PAID_TIP);
        REQUIRE(Scalar(fixture.db, "SELECT target_id FROM drawer_payment;") == 9);
    }

    SECTION("a counted tender that came to nothing is still recorded")
    {
        // The legacy writer emits a balance row only when `entered` is
        // non-zero, so a file cannot represent "this was counted and came to
        // nothing" -- a real outcome, and different from never having counted.
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer_balance;") == 2);
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer_balance "
                                   "WHERE entered = 0;") == 1);
    }

    SECTION("the expected side is NULL until the drawer is balanced")
    {
        // Storing a zero would be indistinguishable from "nothing was owed"
        // when the truth is "nobody has counted yet".
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer_balance "
                                   "WHERE expected_amount IS NULL;") == 2);
    }
}

TEST_CASE("A balanced drawer records what it was counted against",
          "[drawer][store][sqlite]")
{
    DrawerFixture fixture("vt_drawer_balanced");
    StoreError error = StoreError::Io;
    auto store = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);

    std::unique_ptr<Drawer> drawer(BuildDrawer(7200, /*balanced=*/true));

    auto tx = store->Begin();
    REQUIRE(store->Drawers().Save(*tx, *drawer) == StoreError::Ok);
    REQUIRE(tx->Commit() == StoreError::Ok);

    REQUIRE(Scalar(fixture.db,
                   "SELECT expected_amount FROM drawer_balance "
                   "WHERE tender_type = " + std::to_string(TENDER_CASH) + ";")
            == 12000);
    REQUIRE(Scalar(fixture.db,
                   "SELECT expected_count FROM drawer_balance "
                   "WHERE tender_type = " + std::to_string(TENDER_CASH) + ";") == 3);

    SECTION("over/short comes out of the view rather than a stored column")
    {
        // A subtraction of two frozen columns. A stored copy could only drift.
        REQUIRE(Scalar(fixture.db, "SELECT counted FROM drawer_difference;") == 12500);
        REQUIRE(Scalar(fixture.db, "SELECT expected FROM drawer_difference;") == 12000);
        REQUIRE(Scalar(fixture.db, "SELECT difference FROM drawer_difference;") == 500);
    }
}

TEST_CASE("Re-saving a drawer replaces its children rather than duplicating",
          "[drawer][store][sqlite]")
{
    DrawerFixture fixture("vt_drawer_resave");
    StoreError error = StoreError::Io;
    auto store = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);

    std::unique_ptr<Drawer> drawer(BuildDrawer(7300, false));

    for (int pass = 0; pass < 2; ++pass)
    {
        auto tx = store->Begin();
        REQUIRE(store->Drawers().Save(*tx, *drawer) == StoreError::Ok);
        REQUIRE(tx->Commit() == StoreError::Ok);
    }

    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer;") == 1);
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer_payment;") == 1);
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer_balance;") == 2);

    SECTION("a rolled back save leaves nothing behind")
    {
        std::unique_ptr<Drawer> other(BuildDrawer(7301, false));
        auto tx = store->Begin();
        REQUIRE(store->Drawers().Save(*tx, *other) == StoreError::Ok);
        tx->Rollback();

        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer;") == 1);
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer_payment;") == 1);
    }
}

TEST_CASE("A dual-mode site writes drawers to both sides from Drawer::Save",
          "[drawer][store][dual]")
{
    DrawerFixture fixture("vt_drawer_dual");

    auto legacy = MakeLegacyFileStore(MasterSystem.get());
    StoreError error = StoreError::Io;
    auto sqlite = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);
    MasterSystem->SetDataStore(
        MakeDualRunStore(std::move(legacy), std::move(sqlite)));

    // Nothing here calls the store directly -- this is Drawer::Save(), the same
    // call every drawer-pull and balance path makes.
    std::unique_ptr<Drawer> drawer(BuildDrawer(7400, true));
    REQUIRE(drawer->Save() == 0);

    REQUIRE(FilesIn(fixture.dir) == 1);
    REQUIRE(Scalar(fixture.db,
                   "SELECT COUNT(*) FROM drawer WHERE serial_number = 7400;") == 1);
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer_balance;") == 2);
}

TEST_CASE("The business day rolls over, so a second day of trading works",
          "[store][sqlite][endday]")
{
    /*
     * The gap this closes, and it is worse than the constraint failure I first
     * assumed. serial_number identifies a drawer WITHIN a business day, so a
     * repeated serial in the same day is an update, not a collision. With
     * nothing closing the day, a database that traded for two days put both
     * days in the same open day -- and day two's drawer #9001 silently
     * OVERWROTE day one's. No error, no constraint, just yesterday's cash count
     * replaced by today's.
     */
    DrawerFixture fixture("vt_endday_rollover");
    StoreError error = StoreError::Io;
    auto store = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);

    const auto save_drawer = [&](int serial) {
        std::unique_ptr<Drawer> drawer(BuildDrawer(serial, true));
        auto tx = store->Begin();
        const StoreError result = store->Drawers().Save(*tx, *drawer);
        if (result != StoreError::Ok)
        {
            tx->Rollback();
            return result;
        }
        return tx->Commit();
    };

    REQUIRE(save_drawer(9001) == StoreError::Ok);
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM business_day "
                               "WHERE closed_at_local IS NULL;") == 1);

    SECTION("the same serial in a new day is fine")
    {
        REQUIRE(store->EndBusinessDay(MasterSystem->settings) == StoreError::Ok);

        // Exactly one open day still, and the previous one is closed rather
        // than deleted -- yesterday's takings do not go anywhere.
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM business_day;") == 2);
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM business_day "
                                   "WHERE closed_at_local IS NULL;") == 1);
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer;") == 1);

        // The serial that would have collided.
        REQUIRE(save_drawer(9001) == StoreError::Ok);
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer "
                                   "WHERE serial_number = 9001;") == 2);
    }

    SECTION("without the rollover the same serial silently overwrites")
    {
        // The failure a site would have hit on its second day, and the reason
        // the rollover is not optional. It reports success -- which is what
        // makes it dangerous rather than merely broken.
        REQUIRE(save_drawer(9001) == StoreError::Ok);
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM drawer;") == 1);
        REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM business_day;") == 1);
    }

    SECTION("yesterday's rows stay attached to yesterday")
    {
        REQUIRE(store->EndBusinessDay(MasterSystem->settings) == StoreError::Ok);
        REQUIRE(save_drawer(9002) == StoreError::Ok);

        // One drawer in the closed day, one in the open one. Reports over a
        // date range depend on this and nothing else.
        REQUIRE(Scalar(fixture.db,
                       "SELECT COUNT(*) FROM drawer d JOIN business_day b "
                       "ON b.id = d.business_day_id "
                       "WHERE b.closed_at_local IS NOT NULL;") == 1);
        REQUIRE(Scalar(fixture.db,
                       "SELECT COUNT(*) FROM drawer d JOIN business_day b "
                       "ON b.id = d.business_day_id "
                       "WHERE b.closed_at_local IS NULL;") == 1);
    }
}

TEST_CASE("Ending the day is a no-op on the legacy backend, not a failure",
          "[store][endday]")
{
    // The archive file IS the day there, and EndDay writes it directly.
    // Returning Unsupported would make EndDay log an error every night on
    // every site that never enabled SQL.
    DrawerFixture fixture("vt_endday_legacy");
    auto store = MakeLegacyFileStore(MasterSystem.get());
    REQUIRE(store->EndBusinessDay(MasterSystem->settings) == StoreError::Ok);
}

TEST_CASE("The divergence report covers drawers, not just checks",
          "[drawer][store][dualrun]")
{
    /*
     * Closing a hole opened by migrating drawer WRITES without migrating drawer
     * COMPARISON. For one commit the report walked both backends, said "no
     * divergence", and silently omitted half the money. That is worse than no
     * coverage: it produces confidence rather than the absence of it.
     */
    DrawerFixture fixture("vt_drawer_diverge");

    auto legacy = MakeLegacyFileStore(MasterSystem.get());
    StoreError error = StoreError::Io;
    auto sqlite = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);
    auto dual = MakeDualRunStore(std::move(legacy), std::move(sqlite));

    Drawer *drawer = BuildDrawer(7500, true);
    REQUIRE(MasterSystem->Add(drawer) == 0);

    auto tx = dual->Begin();
    REQUIRE(dual->Drawers().Save(*tx, *drawer) == StoreError::Ok);
    REQUIRE(tx->Commit() == StoreError::Ok);

    StoreSnapshot legacy_snap;
    StoreSnapshot sqlite_snap;
    REQUIRE(dual->Primary().Snapshot(legacy_snap) == StoreError::Ok);
    REQUIRE(dual->ShadowStore().Snapshot(sqlite_snap) == StoreError::Ok);

    SECTION("both sides report the drawer at all")
    {
        REQUIRE(legacy_snap.drawers.size() == 1);
        REQUIRE(sqlite_snap.drawers.size() == 1);
        REQUIRE(legacy_snap.drawers[0].serial_number == 7500);
        REQUIRE(sqlite_snap.drawers[0].serial_number == 7500);
    }

    SECTION("the zero-balance row the legacy writer drops is reported")
    {
        // Drawer::Write emits a balance only when `entered` is non-zero, so the
        // file cannot say "this tender was counted and came to nothing". The
        // SQL side keeps it, and the diff now names the difference instead of
        // passing over it.
        REQUIRE(legacy_snap.drawers[0].balances.size() == 1);
        REQUIRE(sqlite_snap.drawers[0].balances.size() == 2);

        std::vector<Divergence> found;
        REQUIRE(dual->Compare(found) == StoreError::Ok);

        const auto it = std::find_if(found.begin(), found.end(),
                                     [](const Divergence &d) {
                                         return d.path.find("balance_count") !=
                                                std::string::npos;
                                     });
        REQUIRE(it != found.end());
        REQUIRE(it->left == "1");
        REQUIRE(it->right == "2");
        REQUIRE_FALSE(it->note.empty());
    }

    SECTION("a drawer present on only one side is reported")
    {
        // The most dangerous shape of divergence, and the one a check-only diff
        // could never have seen.
        std::unique_ptr<Drawer> shadow_only(BuildDrawer(7501, false));
        auto only = dual->ShadowStore().Begin();
        REQUIRE(dual->ShadowStore().Drawers().Save(*only, *shadow_only)
                == StoreError::Ok);
        REQUIRE(only->Commit() == StoreError::Ok);

        std::vector<Divergence> found;
        REQUIRE(dual->Compare(found) == StoreError::Ok);

        const auto it = std::find_if(found.begin(), found.end(),
                                     [](const Divergence &d) {
                                         return d.path == "drawer[7501]";
                                     });
        REQUIRE(it != found.end());
        REQUIRE(it->left == "absent");
        REQUIRE(it->right == "present");
    }

    MasterSystem->SetDataStore(nullptr);
}

TEST_CASE("EndDay reports on the day that traded, not the one about to start",
          "[endday][dualrun][report]")
{
    /*
     * Drives the real System::EndDay(), which is what would have caught the
     * ordering this fixes. The divergence report originally ran at the END of
     * EndDay -- after the closed checks were archived and their files unlinked,
     * and after the business day had rolled over -- so both snapshots described
     * the fresh, empty day. It reported no divergence every night, about a day
     * nobody had traded yet.
     *
     * The report has to run at the START of EndDay: that is the only point
     * where both backends still describe the day that just finished.
     */
    DrawerFixture fixture("vt_endday_report");

    System *system = MasterSystem.get();
    const std::string previous_data = (system->data_path.Value() != nullptr)
                                          ? system->data_path.Value() : "";
    const std::string previous_archive = (system->archive_path.Value() != nullptr)
                                             ? system->archive_path.Value() : "";
    system->data_path.Set(fixture.dir.string().c_str());
    system->archive_path.Set(fixture.dir.string().c_str());

    auto legacy = MakeLegacyFileStore(system);
    StoreError error = StoreError::Io;
    auto sqlite = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);
    system->SetDataStore(MakeDualRunStore(std::move(legacy), std::move(sqlite)));

    // A drawer that traded today. Balanced, so AllDrawersPulled() lets the day
    // end, and carrying the zero-`entered` balance the legacy writer drops --
    // which is the divergence this asserts on.
    Drawer *drawer = BuildDrawer(8100, /*balanced=*/true);
    REQUIRE(system->Add(drawer) == 0);
    REQUIRE(drawer->Save() == 0);

    REQUIRE(system->EndDay() == 0);

    // The report exists and describes the drawer that traded.
    fs::path report;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(fixture.dir, ec))
    {
        if (entry.path().filename().string().rfind("divergence_", 0) == 0)
            report = entry.path();
    }
    REQUIRE_FALSE(report.empty());

    std::ifstream in(report);
    std::stringstream body;
    body << in.rdbuf();
    const std::string text = body.str();

    INFO(text);
    REQUIRE(text.find("drawer[8100]") != std::string::npos);

    // And the day did roll over, so tomorrow starts clean.
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM business_day "
                               "WHERE closed_at_local IS NOT NULL;") == 1);

    system->SetDataStore(nullptr);
    system->data_path.Set(previous_data.c_str());
    system->archive_path.Set(previous_archive.c_str());
}

TEST_CASE("A day closed in SQL records the policy it traded under",
          "[endday][policy][sqlite]")
{
    /*
     * The end-to-end test, written before the fix, because that is what the
     * last four defects here had in common: a unit test that passed over an
     * integration that did not work.
     *
     * `day_policy` exists so that changing a tax rate today cannot restate a
     * closed day -- exactly what Archive's frozen rates do for the file format,
     * and exactly what the tax_VAT bug broke when EndDay's copy omitted two
     * fields. The importer writes a row per imported day. Nothing wrote one for
     * a day the site actually traded and closed, so every day closed natively
     * under `sqlite` had no frozen policy at all.
     *
     * This drives System::EndDay(), not the store method, for the same reason
     * as the divergence-report test above it.
     */
    DrawerFixture fixture("vt_endday_policy");

    System *system = MasterSystem.get();
    const std::string previous_data = (system->data_path.Value() != nullptr)
                                          ? system->data_path.Value() : "";
    const std::string previous_archive = (system->archive_path.Value() != nullptr)
                                             ? system->archive_path.Value() : "";
    system->data_path.Set(fixture.dir.string().c_str());
    system->archive_path.Set(fixture.dir.string().c_str());

    // The rates in force while the day trades.
    Settings &settings = system->settings;
    const Flt previous_food = settings.tax_food;
    const Flt previous_vat = settings.tax_VAT;
    settings.tax_food = 0.0825;
    settings.tax_VAT = 0.175;

    StoreError error = StoreError::Io;
    auto sqlite = MakeSqliteStore(fixture.db, error);
    REQUIRE(error == StoreError::Ok);
    system->SetDataStore(std::move(sqlite));

    Drawer *drawer = BuildDrawer(8200, /*balanced=*/true);
    REQUIRE(system->Add(drawer) == 0);
    REQUIRE(drawer->Save() == 0);

    REQUIRE(system->EndDay() == 0);

    // A day closed, and it carries the rates it traded under.
    REQUIRE(Scalar(fixture.db, "SELECT COUNT(*) FROM business_day "
                               "WHERE closed_at_local IS NOT NULL;") == 1);
    REQUIRE(Scalar(fixture.db,
                   "SELECT COUNT(*) FROM day_policy p"
                   " JOIN business_day d ON d.id = p.business_day_id"
                   " WHERE d.closed_at_local IS NOT NULL;") == 1);

    // And they are the day's rates, not zero and not a default. tax_VAT
    // specifically: that is the field EndDay's open-coded copy forgot, and
    // Settings::FigureVAT treats 0 as a real rate rather than "unset", so a
    // missing value is silently a whole day of untaxed sales.
    REQUIRE(ScalarDouble(fixture.db,
                         "SELECT tax_food FROM day_policy p"
                         " JOIN business_day d ON d.id = p.business_day_id"
                         " WHERE d.closed_at_local IS NOT NULL;")
            == Catch::Approx(0.0825));
    REQUIRE(ScalarDouble(fixture.db,
                         "SELECT tax_VAT FROM day_policy p"
                         " JOIN business_day d ON d.id = p.business_day_id"
                         " WHERE d.closed_at_local IS NOT NULL;")
            == Catch::Approx(0.175));

    // Unlike an imported day, this one's policy IS authoritative: it was read
    // from the live Settings at the moment the day closed, not reconstructed.
    REQUIRE(Scalar(fixture.db,
                   "SELECT snapshot_complete FROM day_policy p"
                   " JOIN business_day d ON d.id = p.business_day_id"
                   " WHERE d.closed_at_local IS NOT NULL;") == 1);

    // Changing a rate afterwards must not reach back into the closed day.
    settings.tax_food = 0.15;
    REQUIRE(ScalarDouble(fixture.db,
                         "SELECT tax_food FROM day_policy p"
                         " JOIN business_day d ON d.id = p.business_day_id"
                         " WHERE d.closed_at_local IS NOT NULL;")
            == Catch::Approx(0.0825));

    settings.tax_food = previous_food;
    settings.tax_VAT = previous_vat;
    system->SetDataStore(nullptr);
    system->data_path.Set(previous_data.c_str());
    system->archive_path.Set(previous_archive.c_str());
}
