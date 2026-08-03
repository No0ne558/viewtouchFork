/*
 * sqlite_store.cc - The SQL backend behind the Store seam.
 *
 * This is the first backend that can honestly return true from
 * SupportsAtomicWrites(). A check, its subchecks, its order tree, its payments
 * and its frozen totals all commit or none of them do, which is the property
 * the legacy backend structurally cannot offer: one file per check, no way to
 * group them, so EndDay touching a dozen is interruptible halfway.
 *
 * Scope, deliberately bounded to the write side:
 *
 *   Reading is not here. The Store interface is Save/Remove/Count, and the
 *   query side belongs with the importer and the dual-run diff (PRs 13-14),
 *   which is where the shape of a read is actually determined. Tests verify
 *   what was written by querying the tables directly -- which is stronger than
 *   a round trip through a reader written by the same hand as the writer.
 *
 *   Archived checks return Unsupported. Routing one needs an archive ->
 *   business_day mapping that only the importer establishes. Reporting that
 *   plainly beats writing it to the wrong day.
 *
 *   Nothing freezes automatically. frozen_at_local is always written NULL, and
 *   freezing stays an explicit end-of-day operation. Auto-freezing on close
 *   would look right and would break the reopen path, since a settled subcheck
 *   can go back to open. The guard against rewriting an already-frozen check is
 *   implemented and tested regardless -- the constraint is real even though
 *   nothing in this PR sets it.
 */

#include "store.hh"

#include "check.hh"
#include "check_writer.hh"
#include "labor.hh"
#include "tips.hh"
#include "day_contents.hh"
#include "day_policy.hh"
#include "drawer.hh"

#include "sql/database.hh"
#include "sql/migrations.hh"
#include "sql/sequence.hh"
#include "sql/statement.hh"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vt::store {

namespace {

using vt::sql::Database;
using vt::sql::Statement;
using vt::sql::Status;


/*
 * A transaction over the real database.
 *
 * Unlike the legacy backend's inert one, Commit here is the moment the whole
 * aggregate becomes visible and Rollback genuinely undoes it. The destructor
 * rolls back, so an early return anywhere in Save cannot leave a half-written
 * check behind.
 */
class SqlTransaction final : public Transaction
{
public:
    explicit SqlTransaction(Database &db) : inner_(db) {}

    [[nodiscard]] Status Begin() { return inner_.Begin(); }

    StoreError Commit() override
    {
        if (!inner_.IsActive())
            return StoreError::Ok;
        return Translate(inner_.Commit());
    }

    void Rollback() noexcept override { inner_.Rollback(); }

    [[nodiscard]] bool IsActive() const noexcept override
    {
        return inner_.IsActive();
    }

private:
    vt::sql::Transaction inner_;
};

// Defined below; declared here because EndBusinessDay needs it to open the
// next day and the definition sits with the other factory-time helpers.
StoreError ResolveOpenDay(Database &db, int64_t &out);

class SqliteCheckRepository final : public CheckRepository
{
public:
    // The day id is held by reference, not copied: EndBusinessDay advances it
    // on the store, and a repository holding a stale copy would keep writing
    // into the day that just closed.
    SqliteCheckRepository(Database &db, const int64_t &business_day_id)
        : db_(db), business_day_id_(business_day_id) {}

    StoreError Save(Transaction &tx, Check &check) override
    {
        if (!tx.IsActive())
            return StoreError::Constraint;

        // The same three-case dispatch the legacy backend mirrors from
        // Check::Save(), so a call site behaves identically whichever backend
        // is underneath -- which is the precondition for comparing them.
        if (check.archive != nullptr)
            return StoreError::Unsupported;   // see the file header

        if (check.copy != 0)
            return StoreError::Ok;          // a working copy is never persisted

        if (check.serial_number <= 0)
        {
            int64_t serial = 0;
            if (Status s = vt::sql::NextSequenceValue(
                    db_, vt::sql::kPosSerialSequence, serial); s != Status::Ok)
            {
                return Translate(s);
            }
            check.serial_number = static_cast<int>(serial);
        }

        int64_t check_id = 0;
        const StoreError found =
            FindCheckBySerial(db_, business_day_id_, check.serial_number, check_id);
        if (found != StoreError::Ok && found != StoreError::NotFound)
            return found;

        // Live writes never freeze and record source 0 (computed live). The
        // importer constructs its own writer with the opposite answers.
        CheckWriter writer(db_, business_day_id_, /*freeze=*/false, /*source=*/0);

        if (found == StoreError::NotFound)
            return writer.InsertAggregate(check, /*serial_disambiguator=*/0, check_id);

        // Replacing an existing check means discarding its children and writing
        // them again -- the aggregate is the unit, matching Check::Write, which
        // rewrites the whole file. That is safe only inside the transaction.
        //
        // Except when any of them is frozen. A frozen subcheck's money is what
        // the customer was charged and what was remitted; rewriting deletes and
        // reinserts those rows, which trg_subcheck_total_frozen cannot catch
        // because it fires on UPDATE. Refusing here is what closes that.
        bool frozen = false;
        if (const StoreError e = CheckHasFrozenSubCheck(db_, check_id, frozen);
            e != StoreError::Ok)
        {
            return e;
        }
        if (frozen)
            return StoreError::Constraint;

        return writer.ReplaceChildren(check_id, check);
    }

    StoreError Remove(Transaction &tx, Check &check) override
    {
        if (!tx.IsActive())
            return StoreError::Constraint;

        if (check.archive != nullptr)
            return StoreError::Unsupported;

        if (check.copy != 0)
            return StoreError::Ok;

        // ON DELETE CASCADE carries the subchecks, orders, payments and totals
        // with it, so the aggregate leaves in one statement.
        Statement stmt;
        if (Status s = stmt.Prepare(
                db_, "DELETE FROM pos_check "
                     "WHERE business_day_id = ?1 AND serial_number = ?2;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = stmt.BindInt(1, business_day_id_); s != Status::Ok)
            return Translate(s);
        if (Status s = stmt.BindInt(2, check.serial_number); s != Status::Ok)
            return Translate(s);
        return Translate(stmt.Execute());
    }

    StoreError Count(int &out) override
    {
        int64_t total = 0;
        const std::string sql =
            "SELECT COUNT(*) FROM pos_check WHERE business_day_id = " +
            std::to_string(business_day_id_) + ";";
        if (Status s = db_.QueryInt(sql, total); s != Status::Ok)
            return Translate(s);
        out = static_cast<int>(total);
        return StoreError::Ok;
    }

private:
    Database &db_;
    const int64_t &business_day_id_;
};

/*
 * Drawers in SQL.
 *
 * Whole-drawer replace, matching the check repository and matching what
 * Drawer::Write does to a file: the payments and balances are rewritten as a
 * set, because there is no dirty-tracking to do anything finer with.
 *
 * The expected side of a balance is written only once the drawer has been
 * balanced. Before that the legacy code recomputes `amount` and `count` from
 * whatever checks are in scope on every load, so storing a zero would be
 * indistinguishable from "nothing was owed" when the truth is "nobody has
 * counted yet" -- hence NULL, and hence the columns being nullable.
 */
/*
 * Labor periods. Unlike checks and drawers these hang off no business day: a
 * pay period spans many, and closes on its own schedule.
 *
 * Whole-period upsert. LaborPeriod::Save rewrites its entire file, so the
 * equivalent here replaces the period's work entries rather than appending --
 * an entry edited or removed in memory has to disappear from the database too,
 * and a save is the only signal that happened.
 */
class SqliteLaborRepository final : public LaborRepository
{
public:
    explicit SqliteLaborRepository(Database &db) : db_(db) {}

    StoreError Save(Transaction &tx, LaborPeriod &period) override
    {
        if (!tx.IsActive())
            return StoreError::Constraint;

        int64_t period_id = 0;
        {
            Statement stmt;
            if (Status s = stmt.Prepare(
                    db_, "INSERT INTO labor_period("
                         "  serial_number, end_time_local, end_time_utc,"
                         "  legacy_filename)"
                         " VALUES (?1, ?2, ?3, ?4)"
                         " ON CONFLICT(serial_number) DO UPDATE SET"
                         "  end_time_local = excluded.end_time_local,"
                         "  end_time_utc = excluded.end_time_utc,"
                         "  legacy_filename = excluded.legacy_filename"
                         " RETURNING id;");
                s != Status::Ok)
            {
                return Translate(s);
            }

            Status s = Status::Ok;
            if ((s = stmt.BindInt(1, period.serial_number)) != Status::Ok)
                return Translate(s);
            // NULL while the period is open, which is what CurrentPeriod finds.
            if ((s = stmt.BindOptionalInt(2, LocalSeconds(period.end_time))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindOptionalInt(3, UtcSeconds(period.end_time))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindText(4, TextOf(period.file_name))) != Status::Ok)
                return Translate(s);

            Status step = Status::Ok;
            if (!stmt.Step(step))
                return Fail(db_, step, "save labor period");
            period_id = stmt.ColumnInt(0);
        }

        {
            Statement stmt;
            if (Status s = stmt.Prepare(
                    db_, "DELETE FROM work_entry WHERE labor_period_id = ?1;");
                s != Status::Ok)
            {
                return Translate(s);
            }
            if (Status s = stmt.BindInt(1, period_id); s != Status::Ok)
                return Translate(s);
            if (const StoreError e = Report(db_, stmt.Execute(), "clear work entries");
                e != StoreError::Ok)
            {
                return e;
            }
        }

        Statement stmt;
        if (Status s = stmt.Prepare(
                db_, "INSERT INTO work_entry("
                     "  labor_period_id, user_id, job, pay_rate, pay_amount,"
                     "  tips, overtime, end_shift, start_local, start_utc,"
                     "  end_local, end_utc, sequence)"
                     " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11,"
                     "         ?12, ?13);");
            s != Status::Ok)
        {
            return Translate(s);
        }

        int64_t sequence = 0;
        for (WorkEntry *w = period.WorkList(); w != nullptr; w = w->next)
        {
            Status s = Status::Ok;
            if ((s = stmt.BindInt(1, period_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(2, w->user_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(3, w->job)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(4, w->pay_rate)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(5, w->pay_amount)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(6, w->tips)) != Status::Ok) return Translate(s);
            // Recorded, not authoritative: nothing writes overtime to the
            // legacy file and only LaborPeriod::WorkReport ever assigns it, as
            // a side effect of drawing a report line. See migration 0007.
            if ((s = stmt.BindInt(7, w->overtime)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(8, w->end_shift)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindOptionalInt(9, LocalSeconds(w->start))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindOptionalInt(10, UtcSeconds(w->start))) != Status::Ok)
                return Translate(s);
            // No end means still on the clock, which is different from a shift
            // that ended at the epoch.
            if ((s = stmt.BindOptionalInt(11, LocalSeconds(w->end))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindOptionalInt(12, UtcSeconds(w->end))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindInt(13, sequence)) != Status::Ok) return Translate(s);
            ++sequence;

            if (const StoreError e = Report(db_, stmt.Execute(), "insert work entry");
                e != StoreError::Ok)
            {
                return e;
            }
            if (Status r = stmt.Reset(); r != Status::Ok)
                return Translate(r);
        }

        return StoreError::Ok;
    }

    StoreError Count(int &out) override
    {
        int64_t total = 0;
        if (Status s = db_.QueryInt("SELECT COUNT(*) FROM labor_period;", total);
            s != Status::Ok)
        {
            return Translate(s);
        }
        out = static_cast<int>(total);
        return StoreError::Ok;
    }

private:
    Database &db_;
};

class SqliteDrawerRepository final : public DrawerRepository
{
public:
    SqliteDrawerRepository(Database &db, const int64_t &business_day_id)
        : db_(db), business_day_id_(business_day_id) {}

    StoreError Save(Transaction &tx, Drawer &drawer) override
    {
        if (!tx.IsActive())
            return StoreError::Constraint;

        // Same dispatch as Drawer::Save() and as the legacy backend.
        if (drawer.archive != nullptr)
            return StoreError::Unsupported;   // see the file header

        if (drawer.serial_number <= 0)
        {
            int64_t serial = 0;
            if (Status s = vt::sql::NextSequenceValue(
                    db_, vt::sql::kPosSerialSequence, serial); s != Status::Ok)
            {
                return Translate(s);
            }
            drawer.serial_number = static_cast<int>(serial);
        }

        int64_t drawer_id = 0;
        const StoreError found = FindDrawer(drawer.serial_number, drawer_id);
        if (found != StoreError::Ok && found != StoreError::NotFound)
            return found;

        if (found == StoreError::Ok)
        {
            bool frozen = false;
            if (const StoreError e = IsFrozen(drawer_id, frozen);
                e != StoreError::Ok)
            {
                return e;
            }
            // A balanced drawer is what a manager signed off on. Rewriting it
            // deletes and reinserts the counted amounts, which the frozen
            // trigger cannot catch because it fires on UPDATE.
            if (frozen)
                return StoreError::Constraint;

            if (const StoreError e = UpdateDrawer(drawer_id, drawer);
                e != StoreError::Ok)
            {
                return e;
            }
            if (const StoreError e = DeleteChildren(drawer_id);
                e != StoreError::Ok)
            {
                return e;
            }
        }
        else if (const StoreError e = InsertDrawer(drawer, drawer_id);
                 e != StoreError::Ok)
        {
            return e;
        }

        return WriteChildren(drawer_id, drawer);
    }

    StoreError Count(int &out) override
    {
        int64_t total = 0;
        const std::string sql =
            "SELECT COUNT(*) FROM drawer WHERE business_day_id = " +
            std::to_string(business_day_id_) + ";";
        if (Status s = db_.QueryInt(sql, total); s != Status::Ok)
            return Translate(s);
        out = static_cast<int>(total);
        return StoreError::Ok;
    }

private:
    StoreError FindDrawer(int serial_number, int64_t &out)
    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db_, "SELECT id FROM drawer "
                     "WHERE business_day_id = ?1 AND serial_number = ?2;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = stmt.BindInt(1, business_day_id_); s != Status::Ok)
            return Translate(s);
        if (Status s = stmt.BindInt(2, serial_number); s != Status::Ok)
            return Translate(s);

        Status step = Status::Ok;
        if (stmt.Step(step))
        {
            out = stmt.ColumnInt(0);
            return StoreError::Ok;
        }
        return (step != Status::Ok) ? Translate(step) : StoreError::NotFound;
    }

    StoreError IsFrozen(int64_t drawer_id, bool &out)
    {
        int64_t frozen = 0;
        const std::string sql =
            "SELECT COUNT(*) FROM drawer WHERE id = " +
            std::to_string(drawer_id) + " AND frozen_at_local IS NOT NULL;";
        if (Status s = db_.QueryInt(sql, frozen); s != Status::Ok)
            return Translate(s);
        out = (frozen > 0);
        return StoreError::Ok;
    }

    StoreError BindDrawerColumns(Statement &stmt, const Drawer &drawer, int base)
    {
        Status s = Status::Ok;
        const auto at = [base](int offset) { return base + offset; };

        if ((s = stmt.BindText(at(0), TextOf(drawer.host))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindInt(at(1), drawer.position)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(at(2), drawer.number)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindOptionalInt(at(3), NullableId(drawer.owner_id))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindOptionalInt(at(4), NullableId(drawer.puller_id))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindInt(at(5), drawer.media_balanced)) != Status::Ok)
            return Translate(s);
        // The three timestamps GetStatus() derives from. Nullable, because a
        // drawer that has not been pulled has no pull time -- which is a
        // different fact from a pull time of zero.
        if ((s = stmt.BindOptionalInt(at(6), LocalSeconds(drawer.start_time))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindOptionalInt(at(7), LocalSeconds(drawer.pull_time))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindOptionalInt(at(8), LocalSeconds(drawer.balance_time))) != Status::Ok)
            return Translate(s);
        return StoreError::Ok;
    }

    static constexpr int kDrawerColumnCount = 9;

    StoreError InsertDrawer(const Drawer &drawer, int64_t &out_id)
    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db_,
                "INSERT INTO drawer("
                "  business_day_id, serial_number, host, position, number,"
                "  owner_id, puller_id, media_balanced,"
                "  start_time_local, pull_time_local, balance_time_local)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)"
                " RETURNING id;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = stmt.BindInt(1, business_day_id_); s != Status::Ok)
            return Translate(s);
        if (Status s = stmt.BindInt(2, drawer.serial_number); s != Status::Ok)
            return Translate(s);
        if (const StoreError e = BindDrawerColumns(stmt, drawer, 3);
            e != StoreError::Ok)
        {
            return e;
        }

        Status step = Status::Ok;
        if (!stmt.Step(step))
            return Fail(db_, step, "insert drawer");
        out_id = stmt.ColumnInt(0);
        return StoreError::Ok;
    }

    StoreError UpdateDrawer(int64_t drawer_id, const Drawer &drawer)
    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db_,
                "UPDATE drawer SET host = ?1, position = ?2, number = ?3,"
                "  owner_id = ?4, puller_id = ?5, media_balanced = ?6,"
                "  start_time_local = ?7, pull_time_local = ?8,"
                "  balance_time_local = ?9"
                " WHERE id = ?10;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (const StoreError e = BindDrawerColumns(stmt, drawer, 1);
            e != StoreError::Ok)
        {
            return e;
        }
        if (Status s = stmt.BindInt(kDrawerColumnCount + 1, drawer_id);
            s != Status::Ok)
        {
            return Translate(s);
        }
        return Report(stmt.Execute(), "update drawer");
    }

    StoreError DeleteChildren(int64_t drawer_id)
    {
        for (const char *sql : {"DELETE FROM drawer_payment WHERE drawer_id = ?1;",
                                "DELETE FROM drawer_balance WHERE drawer_id = ?1;"})
        {
            Statement stmt;
            if (Status s = stmt.Prepare(db_, sql); s != Status::Ok)
                return Translate(s);
            if (Status s = stmt.BindInt(1, drawer_id); s != Status::Ok)
                return Translate(s);
            if (const StoreError e = Report(stmt.Execute(), "delete drawer children");
                e != StoreError::Ok)
            {
                return e;
            }
        }
        return StoreError::Ok;
    }

    StoreError WriteChildren(int64_t drawer_id, Drawer &drawer)
    {
        int seq = 0;
        for (const DrawerPayment *payment = drawer.PaymentList();
             payment != nullptr; payment = payment->next, ++seq)
        {
            Statement stmt;
            if (Status s = stmt.Prepare(
                    db_,
                    "INSERT INTO drawer_payment("
                    "  drawer_id, business_day_id, seq, tender_type, amount,"
                    "  user_id, target_id, time_local)"
                    " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);");
                s != Status::Ok)
            {
                return Translate(s);
            }
            Status s = Status::Ok;
            if ((s = stmt.BindInt(1, drawer_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(2, business_day_id_)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(3, seq)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(4, payment->tender_type)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(5, payment->amount)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindOptionalInt(6, NullableId(payment->user_id))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindOptionalInt(7, NullableId(payment->target_id))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindOptionalInt(8, LocalSeconds(payment->time))) != Status::Ok)
                return Translate(s);
            if (const StoreError e = Report(stmt.Execute(), "insert drawer payment");
                e != StoreError::Ok)
            {
                return e;
            }
        }

        // Every balance, not only the ones with a non-zero `entered`.
        // Drawer::Write filters those out, so the legacy file cannot represent
        // "this tender was counted and came to nothing" -- which is a real
        // outcome and different from never having been counted.
        const bool balanced = drawer.balance_time.IsSet();
        seq = 0;
        for (const DrawerBalance *balance = drawer.BalanceList();
             balance != nullptr; balance = balance->next, ++seq)
        {
            Statement stmt;
            if (Status s = stmt.Prepare(
                    db_,
                    "INSERT INTO drawer_balance("
                    "  drawer_id, business_day_id, seq, tender_type,"
                    "  legacy_tender_id, entered, expected_amount, expected_count)"
                    " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);");
                s != Status::Ok)
            {
                return Translate(s);
            }
            Status s = Status::Ok;
            if ((s = stmt.BindInt(1, drawer_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(2, business_day_id_)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(3, seq)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(4, balance->tender_type)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(5, balance->tender_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(6, balance->entered)) != Status::Ok) return Translate(s);

            const std::optional<int64_t> expected_amount =
                balanced ? std::optional<int64_t>(balance->amount) : std::nullopt;
            const std::optional<int64_t> expected_count =
                balanced ? std::optional<int64_t>(balance->count) : std::nullopt;
            if ((s = stmt.BindOptionalInt(7, expected_amount)) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindOptionalInt(8, expected_count)) != Status::Ok)
                return Translate(s);

            if (const StoreError e = Report(stmt.Execute(), "insert drawer balance");
                e != StoreError::Ok)
            {
                return e;
            }
        }
        return StoreError::Ok;
    }

    StoreError Report(Status status, const char *what)
    {
        return (status == Status::Ok) ? StoreError::Ok : Fail(db_, status, what);
    }

    Database &db_;
    const int64_t &business_day_id_;
};

class SqliteStore final : public Store
{
public:
    SqliteStore(Database db, int64_t business_day_id)
        : db_(std::move(db)), business_day_id_(business_day_id),
          // business_day_id_, the member -- NOT the constructor parameter. The
          // repositories hold this by reference so EndBusinessDay's update is
          // visible to them, and binding to the parameter leaves them pointing
          // at a stack slot that dies here. That compiles, and reads correctly
          // right up until the first rollover writes through it.
          checks_(db_, business_day_id_), drawers_(db_, business_day_id_),
          labor_(db_) {}

    [[nodiscard]] std::unique_ptr<Transaction> Begin() override
    {
        auto tx = std::make_unique<SqlTransaction>(db_);
        if (tx->Begin() != Status::Ok)
            return nullptr;   // a caller checking IsActive() would be lying to itself
        return tx;
    }

    [[nodiscard]] CheckRepository &Checks() override { return checks_; }

    [[nodiscard]] DrawerRepository &Drawers() override { return drawers_; }

    [[nodiscard]] LaborRepository &Labor() override { return labor_; }

    // The whole point. Every write in a transaction lands or none does.
    [[nodiscard]] bool SupportsAtomicWrites() const noexcept override { return true; }

    [[nodiscard]] StoreError EndBusinessDay(const Settings &settings,
                                            const DayContents &contents) override
    {
        // Freeze the policy first, while business_day_id_ still names the day
        // that traded. Every money field on a SubCheck is derived and
        // recomputed on read, so without this row a rate edited tomorrow
        // restates every total on every day already closed -- which is what the
        // Archive's frozen rates prevent for the file format, and what this
        // table exists to prevent here.
        //
        // Ordered before the close and inside the caller's transaction so a
        // day is never closed without one. WriteDayPolicy upserts, so a re-run
        // of an interrupted EndDay replaces the row rather than failing.
        if (const StoreError e =
                WriteDayPolicy(db_, business_day_id_, PolicyFromSettings(settings));
            e != StoreError::Ok)
        {
            return e;
        }

        // The rest of what an archive file holds: this day's tips, expenses and
        // exceptions. Written here rather than incrementally because they are
        // only meaningful as a set and the day is the set's boundary -- and
        // because writing them in the transaction that closes the day means a
        // day is complete or absent, never half.
        if (const StoreError e = WriteDayContents(db_, business_day_id_,
                                                  contents.tips,
                                                  contents.expenses,
                                                  contents.exceptions,
                                                  contents.media);
            e != StoreError::Ok)
        {
            return e;
        }

        if (const StoreError e = WriteCreditTransactions(
                db_, business_day_id_, contents.credit_voids,
                contents.credit_refunds, contents.credit_exceptions);
            e != StoreError::Ok)
        {
            return e;
        }

        // Stamp the day closed and open the next. Everything already written
        // stays where it is -- it belongs to the day that just ended, which is
        // the whole reason the container exists.
        //
        // One statement each, and the caller is expected to have a transaction
        // open around EndDay's other work, so a crash between them cannot leave
        // two open days (which ux_business_day_open would reject anyway).
        {
            Statement stmt;
            if (Status s = stmt.Prepare(
                    db_, "UPDATE business_day SET"
                         "  closed_at_local = strftime('%s','now'),"
                         "  end_local = strftime('%s','now'),"
                         "  end_utc = strftime('%s','now')"
                         " WHERE id = ?1 AND closed_at_local IS NULL;");
                s != Status::Ok)
            {
                return Translate(s);
            }
            if (Status s = stmt.BindInt(1, business_day_id_); s != Status::Ok)
                return Translate(s);
            if (Status s = stmt.Execute(); s != Status::Ok)
                return Fail(db_, s, "close business day");
        }

        int64_t next = 0;
        if (const StoreError e = ResolveOpenDay(db_, next); e != StoreError::Ok)
            return e;

        business_day_id_ = next;
        return StoreError::Ok;
    }

    [[nodiscard]] StoreError LoadPreviousDayTips(TipDB &out) override
    {
        /*
         * The most recently closed day's tip balances.
         *
         * "Most recently closed" rather than "the day before business_day_id_"
         * because ids are assigned in import and trading order, and an import
         * of history after a day has already traded would break an id-arithmetic
         * assumption. Ordering by the close timestamp, with the id as the
         * tiebreak, asks the question the caller actually means.
         *
         * Only entries with a non-zero balance are returned. TipDB::Calculate
         * calls TransferTip for each, and TransferTip deletes an entry whose
         * amount and paid both come to nothing -- so returning zeroes would
         * build rows only to discard them.
         */
        out.Purge();

        Statement stmt;
        if (Status s = stmt.Prepare(
                db_, "SELECT t.user_id, t.amount FROM tip_entry t"
                     " WHERE t.business_day_id = ("
                     "   SELECT id FROM business_day"
                     "    WHERE closed_at_local IS NOT NULL"
                     "    ORDER BY closed_at_local DESC, id DESC LIMIT 1)"
                     "   AND t.amount <> 0;");
            s != Status::Ok)
        {
            return Translate(s);
        }

        Status step = Status::Ok;
        while (stmt.Step(step))
        {
            auto *entry = new TipEntry;
            entry->user_id = static_cast<int>(stmt.ColumnInt(0));
            entry->amount = static_cast<int>(stmt.ColumnInt(1));
            if (out.Add(entry) != 0)
            {
                delete entry;
                return StoreError::Io;
            }
        }
        if (step != Status::Ok)
            return Fail(db_, step, "read previous day tips");

        return StoreError::Ok;
    }

    [[nodiscard]] StoreError HealthCheck() override
    {
        if (!db_.IsOpen())
            return StoreError::Io;

        // quick_check rather than integrity_check: it skips the index
        // cross-verification, which on a real day's data is slow enough that
        // nobody would run it at startup, and this has to be cheap enough to.
        int64_t ignored = 0;
        if (Status s = db_.QueryInt("SELECT COUNT(*) FROM pragma_quick_check "
                                    "WHERE quick_check <> 'ok';", ignored);
            s != Status::Ok)
        {
            return Translate(s);
        }
        return (ignored == 0) ? StoreError::Ok : StoreError::Corrupt;
    }

    /*
     * The read side, queried rather than reconstructed into Check objects.
     *
     * Building Checks and snapshotting those would need a full SQL-to-Check
     * reader before anything could be verified, and would compare the two
     * backends through a third piece of code that also has to be right. This
     * reads the columns the comparison is actually about.
     */
    [[nodiscard]] StoreError Snapshot(StoreSnapshot &out) override
    {
        out = StoreSnapshot{};
        out.backend = Name();

        Statement checks;
        if (Status s = checks.Prepare(
                db_, "SELECT id, serial_number, type, flags, guests, label, comment"
                     " FROM pos_check WHERE business_day_id = ?1"
                     " ORDER BY serial_number, serial_disambiguator;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = checks.BindInt(1, business_day_id_); s != Status::Ok)
            return Translate(s);

        Status step = Status::Ok;
        while (checks.Step(step))
        {
            const int64_t check_id = checks.ColumnInt(0);
            CheckSnapshot snap;
            snap.serial_number = static_cast<int>(checks.ColumnInt(1));
            snap.type = static_cast<int>(checks.ColumnInt(2));
            snap.flags = static_cast<int>(checks.ColumnInt(3));
            snap.guests = static_cast<int>(checks.ColumnInt(4));
            snap.label = checks.ColumnText(5);
            snap.comment = checks.ColumnText(6);

            if (const StoreError e = LoadSubChecks(check_id, snap);
                e != StoreError::Ok)
            {
                return e;
            }
            out.checks.push_back(std::move(snap));
        }
        if (step != Status::Ok)
            return Translate(step);

        if (const StoreError e = LoadDrawerSnapshots(out); e != StoreError::Ok)
            return e;
        return LoadLaborSnapshots(out);
    }

    [[nodiscard]] const char *Name() const noexcept override { return "sqlite"; }

private:
    StoreError LoadDrawerSnapshots(StoreSnapshot &out)
    {
        Statement drawers;
        if (Status s = drawers.Prepare(
                db_, "SELECT id, serial_number, host, position, number,"
                     "       owner_id, puller_id, media_balanced,"
                     "       start_time_local, pull_time_local, balance_time_local"
                     " FROM drawer WHERE business_day_id = ?1"
                     " ORDER BY serial_number, serial_disambiguator;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = drawers.BindInt(1, business_day_id_); s != Status::Ok)
            return Translate(s);

        Status step = Status::Ok;
        while (drawers.Step(step))
        {
            const int64_t drawer_id = drawers.ColumnInt(0);
            DrawerSnapshot snap;
            snap.serial_number = static_cast<int>(drawers.ColumnInt(1));
            snap.host = drawers.ColumnText(2);
            snap.position = static_cast<int>(drawers.ColumnInt(3));
            snap.number = static_cast<int>(drawers.ColumnInt(4));
            snap.owner_id = static_cast<int>(drawers.ColumnInt(5));
            snap.puller_id = static_cast<int>(drawers.ColumnInt(6));
            snap.media_balanced = static_cast<int>(drawers.ColumnInt(7));
            snap.has_start = !drawers.ColumnIsNull(8);
            snap.has_pull = !drawers.ColumnIsNull(9);
            snap.has_balance = !drawers.ColumnIsNull(10);

            if (const StoreError e = LoadDrawerChildren(drawer_id, snap);
                e != StoreError::Ok)
            {
                return e;
            }
            out.drawers.push_back(std::move(snap));
        }
        return Translate(step);
    }

    /*
     * Labor periods are NOT scoped to the business day, unlike everything else
     * in this snapshot: a pay period spans many days. All of them are compared,
     * which is what the legacy side does too -- it walks LaborDB's whole period
     * list.
     */
    StoreError LoadLaborSnapshots(StoreSnapshot &out)
    {
        Statement periods;
        if (Status s = periods.Prepare(
                db_, "SELECT id, serial_number, end_time_local FROM labor_period"
                     " ORDER BY serial_number;");
            s != Status::Ok)
        {
            return Translate(s);
        }

        std::vector<std::pair<int64_t, LaborPeriodSnapshot>> found;
        Status step = Status::Ok;
        while (periods.Step(step))
        {
            LaborPeriodSnapshot snap;
            snap.serial_number = static_cast<int>(periods.ColumnInt(1));
            snap.has_end = !periods.ColumnIsNull(2);
            found.emplace_back(periods.ColumnInt(0), std::move(snap));
        }
        if (step != Status::Ok)
            return Translate(step);

        for (auto &[period_id, snap] : found)
        {
            Statement entries;
            if (Status s = entries.Prepare(
                    db_, "SELECT user_id, job, pay_rate, pay_amount, tips,"
                         "       overtime, end_shift, start_local, end_local"
                         " FROM work_entry WHERE labor_period_id = ?1"
                         " ORDER BY sequence;");
                s != Status::Ok)
            {
                return Translate(s);
            }
            if (Status s = entries.BindInt(1, period_id); s != Status::Ok)
                return Translate(s);

            Status entry_step = Status::Ok;
            while (entries.Step(entry_step))
            {
                WorkEntrySnapshot entry;
                entry.user_id = static_cast<int>(entries.ColumnInt(0));
                entry.job = static_cast<int>(entries.ColumnInt(1));
                entry.pay_rate = static_cast<int>(entries.ColumnInt(2));
                entry.pay_amount = static_cast<int>(entries.ColumnInt(3));
                entry.tips = static_cast<int>(entries.ColumnInt(4));
                entry.overtime = static_cast<int>(entries.ColumnInt(5));
                entry.end_shift = static_cast<int>(entries.ColumnInt(6));
                entry.has_start = !entries.ColumnIsNull(7);
                entry.has_end = !entries.ColumnIsNull(8);
                snap.entries.push_back(entry);
            }
            if (entry_step != Status::Ok)
                return Translate(entry_step);

            out.labor.push_back(std::move(snap));
        }
        return StoreError::Ok;
    }

    StoreError LoadDrawerChildren(int64_t drawer_id, DrawerSnapshot &out)
    {
        {
            Statement stmt;
            if (Status s = stmt.Prepare(
                    db_, "SELECT tender_type, amount, user_id, target_id"
                         " FROM drawer_payment WHERE drawer_id = ?1 ORDER BY seq;");
                s != Status::Ok)
            {
                return Translate(s);
            }
            if (Status s = stmt.BindInt(1, drawer_id); s != Status::Ok)
                return Translate(s);

            Status step = Status::Ok;
            while (stmt.Step(step))
            {
                DrawerPaymentSnapshot snap;
                snap.tender_type = static_cast<int>(stmt.ColumnInt(0));
                snap.amount = static_cast<int>(stmt.ColumnInt(1));
                snap.user_id = static_cast<int>(stmt.ColumnInt(2));
                snap.target_id = static_cast<int>(stmt.ColumnInt(3));
                out.payments.push_back(snap);
            }
            if (step != Status::Ok)
                return Translate(step);
        }

        Statement stmt;
        if (Status s = stmt.Prepare(
                db_, "SELECT tender_type, legacy_tender_id, entered"
                     " FROM drawer_balance WHERE drawer_id = ?1 ORDER BY seq;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = stmt.BindInt(1, drawer_id); s != Status::Ok)
            return Translate(s);

        Status step = Status::Ok;
        while (stmt.Step(step))
        {
            DrawerBalanceSnapshot snap;
            snap.tender_type = static_cast<int>(stmt.ColumnInt(0));
            snap.tender_id = static_cast<int>(stmt.ColumnInt(1));
            snap.entered = static_cast<int>(stmt.ColumnInt(2));
            out.balances.push_back(snap);
        }
        return Translate(step);
    }

    StoreError LoadSubChecks(int64_t check_id, CheckSnapshot &out)
    {
        Statement subs;
        if (Status s = subs.Prepare(
                db_, "SELECT id, seq, status, check_type FROM subcheck"
                     " WHERE check_id = ?1 ORDER BY seq;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = subs.BindInt(1, check_id); s != Status::Ok)
            return Translate(s);

        Status step = Status::Ok;
        while (subs.Step(step))
        {
            const int64_t sub_id = subs.ColumnInt(0);
            SubCheckSnapshot snap;
            snap.seq = static_cast<int>(subs.ColumnInt(1));
            snap.status = static_cast<int>(subs.ColumnInt(2));
            snap.check_type = static_cast<int>(subs.ColumnInt(3));

            if (const StoreError e = LoadOrders(sub_id, snap);
                e != StoreError::Ok)
            {
                return e;
            }
            if (const StoreError e = LoadPayments(sub_id, snap);
                e != StoreError::Ok)
            {
                return e;
            }
            out.subchecks.push_back(std::move(snap));
        }
        return Translate(step);
    }

    StoreError LoadOrders(int64_t sub_id, SubCheckSnapshot &out)
    {
        // Flattened to match SnapshotOf(): roots in seq order, each root's
        // modifiers immediately after it. The ORDER BY reproduces that from the
        // stored tree -- COALESCE puts a root before its own children, and the
        // second key orders siblings.
        Statement orders;
        if (Status s = orders.Prepare(
                db_,
                "SELECT id, parent_order_id, item_name, item_type, item_family,"
                "       sales_type, item_cost, count, seat, qualifier, call_order"
                " FROM order_item WHERE subcheck_id = ?1"
                " ORDER BY COALESCE((SELECT p.seq FROM order_item p"
                "                     WHERE p.id = order_item.parent_order_id),"
                "                   order_item.seq),"
                "          parent_order_id IS NOT NULL, seq;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = orders.BindInt(1, sub_id); s != Status::Ok)
            return Translate(s);

        // parent_order_id is a database id; the snapshot uses a position, so
        // ids have to be mapped as rows arrive. Roots always precede their own
        // modifiers under the ORDER BY above, so the lookup is always populated.
        std::map<int64_t, int> position_of;

        Status step = Status::Ok;
        while (orders.Step(step))
        {
            const int64_t id = orders.ColumnInt(0);
            OrderSnapshot snap;
            snap.item_name = orders.ColumnText(2);
            snap.item_type = static_cast<int>(orders.ColumnInt(3));
            snap.item_family = static_cast<int>(orders.ColumnInt(4));
            snap.sales_type = static_cast<int>(orders.ColumnInt(5));
            snap.item_cost = static_cast<int>(orders.ColumnInt(6));
            snap.count = static_cast<int>(orders.ColumnInt(7));
            snap.seat = static_cast<int>(orders.ColumnInt(8));
            snap.qualifier = static_cast<int>(orders.ColumnInt(9));
            snap.call_order = static_cast<int>(orders.ColumnInt(10));

            if (orders.ColumnIsNull(1))
            {
                snap.parent_index = -1;
                position_of[id] = static_cast<int>(out.orders.size());
            }
            else
            {
                const auto it = position_of.find(orders.ColumnInt(1));
                // An unresolvable parent means the ordering assumption above is
                // wrong. Reporting -1 would silently turn a modifier into a
                // root, so it is better to fail loudly.
                if (it == position_of.end())
                    return StoreError::Corrupt;
                snap.parent_index = it->second;
            }
            out.orders.push_back(std::move(snap));
        }
        return Translate(step);
    }

    StoreError LoadPayments(int64_t sub_id, SubCheckSnapshot &out)
    {
        Statement payments;
        if (Status s = payments.Prepare(
                db_, "SELECT tender_type, legacy_tender_id, amount, flags"
                     " FROM payment WHERE subcheck_id = ?1 ORDER BY seq;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = payments.BindInt(1, sub_id); s != Status::Ok)
            return Translate(s);

        Status step = Status::Ok;
        while (payments.Step(step))
        {
            PaymentSnapshot snap;
            snap.tender_type = static_cast<int>(payments.ColumnInt(0));
            snap.tender_id = static_cast<int>(payments.ColumnInt(1));
            snap.amount = static_cast<int>(payments.ColumnInt(2));
            snap.flags = static_cast<int>(payments.ColumnInt(3));
            out.payments.push_back(snap);
        }
        return Translate(step);
    }

public:

    [[nodiscard]] int64_t BusinessDayId() const noexcept { return business_day_id_; }

private:
    Database db_;
    int64_t business_day_id_;
    SqliteCheckRepository checks_;
    SqliteDrawerRepository drawers_;
    SqliteLaborRepository labor_;
};

// Resolve the single open business day, creating it if the database has none.
// The schema permits exactly one (ux_business_day_open), so this cannot return
// an ambiguous answer.
StoreError ResolveOpenDay(Database &db, int64_t &out)
{
    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db, "SELECT id FROM business_day WHERE closed_at_local IS NULL;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        Status step = Status::Ok;
        if (stmt.Step(step))
        {
            out = stmt.ColumnInt(0);
            return StoreError::Ok;
        }
        if (step != Status::Ok)
            return Translate(step);
    }

    // start_local and start_utc are both "now", so they agree by construction
    // here -- unlike an imported day, whose local time came off a file with no
    // zone attached and may not resolve at all.
    Statement insert;
    if (Status s = insert.Prepare(
            db, "INSERT INTO business_day(start_local, start_utc) "
                "VALUES (strftime('%s','now'), strftime('%s','now')) "
                "RETURNING id;");
        s != Status::Ok)
    {
        return Translate(s);
    }
    Status step = Status::Ok;
    if (!insert.Step(step))
        return (step == Status::Ok) ? StoreError::Io : Translate(step);
    out = insert.ColumnInt(0);
    return StoreError::Ok;
}

} // namespace

std::unique_ptr<Store> MakeSqliteStore(const std::string &path, StoreError &error)
{
    Database db;
    if (Status s = db.Open(path); s != Status::Ok)
    {
        error = Translate(s);
        return nullptr;
    }

    if (const auto result = vt::sql::MigrateToLatest(db);
        result.status != Status::Ok)
    {
        error = Translate(result.status);
        return nullptr;
    }

    int64_t business_day_id = 0;
    if (const StoreError e = ResolveOpenDay(db, business_day_id);
        e != StoreError::Ok)
    {
        error = e;
        return nullptr;
    }

    error = StoreError::Ok;
    return std::make_unique<SqliteStore>(std::move(db), business_day_id);
}

} // namespace vt::store
