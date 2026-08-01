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

#include "sql/database.hh"
#include "sql/migrations.hh"
#include "sql/sequence.hh"
#include "sql/statement.hh"

#include <memory>
#include <optional>
#include <string>

namespace vt::store {

namespace {

using vt::sql::Database;
using vt::sql::Statement;
using vt::sql::Status;

StoreError Translate(Status status) noexcept
{
    switch (status)
    {
    case Status::Ok:         return StoreError::None;
    case Status::CannotOpen: return StoreError::Io;
    case Status::Busy:       return StoreError::Busy;
    case Status::Constraint: return StoreError::Constraint;
    case Status::Corrupt:    return StoreError::Corrupt;
    case Status::SqlError:   return StoreError::Io;
    case Status::NotOpen:    return StoreError::Io;
    }
    return StoreError::Io;
}

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
            return StoreError::None;
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

class SqliteCheckRepository final : public CheckRepository
{
public:
    SqliteCheckRepository(Database &db, int64_t business_day_id)
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
            return StoreError::None;          // a working copy is never persisted

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
        if (found != StoreError::None && found != StoreError::NotFound)
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
            e != StoreError::None)
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
            return StoreError::None;

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
        return StoreError::None;
    }

private:
    Database &db_;
    int64_t business_day_id_;
};

class SqliteStore final : public Store
{
public:
    SqliteStore(Database db, int64_t business_day_id)
        : db_(std::move(db)), business_day_id_(business_day_id),
          checks_(db_, business_day_id) {}

    [[nodiscard]] std::unique_ptr<Transaction> Begin() override
    {
        auto tx = std::make_unique<SqlTransaction>(db_);
        if (tx->Begin() != Status::Ok)
            return nullptr;   // a caller checking IsActive() would be lying to itself
        return tx;
    }

    [[nodiscard]] CheckRepository &Checks() override { return checks_; }

    // The whole point. Every write in a transaction lands or none does.
    [[nodiscard]] bool SupportsAtomicWrites() const noexcept override { return true; }

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
        return (ignored == 0) ? StoreError::None : StoreError::Corrupt;
    }

    [[nodiscard]] const char *Name() const noexcept override { return "sqlite"; }

    [[nodiscard]] int64_t BusinessDayId() const noexcept { return business_day_id_; }

private:
    Database db_;
    int64_t business_day_id_;
    SqliteCheckRepository checks_;
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
            return StoreError::None;
        }
        if (step != Status::Ok)
            return Translate(step);
    }

    Statement insert;
    if (Status s = insert.Prepare(
            db, "INSERT INTO business_day(start_local) "
                "VALUES (strftime('%s','now')) RETURNING id;");
        s != Status::Ok)
    {
        return Translate(s);
    }
    Status step = Status::Ok;
    if (!insert.Step(step))
        return (step == Status::Ok) ? StoreError::Io : Translate(step);
    out = insert.ColumnInt(0);
    return StoreError::None;
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
        e != StoreError::None)
    {
        error = e;
        return nullptr;
    }

    error = StoreError::None;
    return std::make_unique<SqliteStore>(std::move(db), business_day_id);
}

} // namespace vt::store
