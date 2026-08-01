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

#include "sql/database.hh"
#include "sql/migrations.hh"
#include "sql/sequence.hh"
#include "sql/statement.hh"
#include "vt_logger.hh"

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
 * Log why a statement failed, then translate.
 *
 * StoreError is a seven-value enum, so a Constraint return says a rule was
 * broken but not which one -- and the schema has foreign keys, partial unique
 * indexes, CHECK constraints and three triggers that can all produce it.
 * Diagnosing one without SQLite's own message means bisecting the insert, which
 * is exactly how the NOT NULL on computed_at_local was found. The message costs
 * a log line on a path that already failed.
 */
StoreError Fail(Database &db, Status status, const char *what)
{
    // Step() returning false with Ok means "no row" where a RETURNING clause
    // guaranteed one, which is not a SQLite-level error and has no message.
    const std::string detail =
        (status == Status::Ok) ? "statement returned no row" : db.LastError();
    ::vt::Logger::error("sqlite store: {} failed ({}): {}", what,
                        vt::sql::StatusName(status), detail);
    return (status == Status::Ok) ? StoreError::Io : Translate(status);
}

// TimeInfo is date::local_time, with no zone attached, so the only value that
// can be written without inventing information is the local one. The matching
// *_utc columns stay NULL until TimeInfo records a zone; guessing one here
// would be wrong by an hour twice a year and silently so.
std::optional<int64_t> LocalSeconds(const TimeInfo &time)
{
    if (!time.IsSet())
        return std::nullopt;
    return static_cast<int64_t>(
        time.get_local_time().time_since_epoch().count());
}

std::optional<int64_t> NullableId(int id)
{
    return (id > 0) ? std::optional<int64_t>(id) : std::nullopt;
}

std::string TextOf(const Str &value)
{
    const char *text = value.Value();
    return (text != nullptr) ? std::string(text) : std::string();
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
        const StoreError found = FindCheckId(check.serial_number, check_id);
        if (found != StoreError::None && found != StoreError::NotFound)
            return found;

        if (found == StoreError::None)
        {
            // Replacing an existing check means discarding its children and
            // writing them again -- the aggregate is the unit, matching
            // Check::Write, which rewrites the whole file. That is safe here
            // only because it happens inside the caller's transaction.
            if (const StoreError frozen = RejectIfFrozen(check_id);
                frozen != StoreError::None)
            {
                return frozen;
            }
            if (const StoreError e = UpdateCheck(check_id, check);
                e != StoreError::None)
            {
                return e;
            }
            if (const StoreError e = DeleteSubChecks(check_id);
                e != StoreError::None)
            {
                return e;
            }
        }
        else if (const StoreError e = InsertCheck(check, check_id);
                 e != StoreError::None)
        {
            return e;
        }

        int seq = 0;
        for (SubCheck *sub = check.SubList(); sub != nullptr;
             sub = sub->next, ++seq)
        {
            if (const StoreError e = SaveSubCheck(check_id, *sub, seq);
                e != StoreError::None)
            {
                return e;
            }
        }
        return StoreError::None;
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
        return Report(stmt.Execute(), "delete check");
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
    // For statements run via Execute(), whose result is a bare Status rather
    // than a Step. Success passes through; failure gets the same diagnostic.
    StoreError Report(Status status, const char *what)
    {
        return (status == Status::Ok) ? StoreError::None
                                      : Fail(db_, status, what);
    }

    StoreError FindCheckId(int serial_number, int64_t &out)
    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db_, "SELECT id FROM pos_check "
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
            return StoreError::None;
        }
        if (step != Status::Ok)
            return Translate(step);
        return StoreError::NotFound;
    }

    // A frozen subcheck's money is what the customer was charged and what was
    // remitted. Rewriting the check would delete those rows and insert new
    // ones, which the trg_subcheck_total_frozen trigger cannot see because it
    // fires on UPDATE. Refusing here is what actually closes that hole.
    StoreError RejectIfFrozen(int64_t check_id)
    {
        int64_t frozen = 0;
        const std::string sql =
            "SELECT COUNT(*) FROM subcheck "
            "WHERE check_id = " + std::to_string(check_id) +
            " AND frozen_at_local IS NOT NULL;";
        if (Status s = db_.QueryInt(sql, frozen); s != Status::Ok)
            return Translate(s);
        return (frozen > 0) ? StoreError::Constraint : StoreError::None;
    }

    StoreError DeleteSubChecks(int64_t check_id)
    {
        Statement stmt;
        if (Status s = stmt.Prepare(db_, "DELETE FROM subcheck WHERE check_id = ?1;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = stmt.BindInt(1, check_id); s != Status::Ok)
            return Translate(s);
        return Report(stmt.Execute(), "delete subchecks");
    }

    StoreError BindCheckColumns(Statement &stmt, const Check &check, int base)
    {
        // `base` is the 1-based index of the first check column, so the same
        // binding order serves both the INSERT and the UPDATE.
        const auto bind = [&](int offset, std::optional<int64_t> value) {
            return stmt.BindOptionalInt(base + offset, value);
        };
        const auto bind_int = [&](int offset, int64_t value) {
            return stmt.BindInt(base + offset, value);
        };
        const auto bind_text = [&](int offset, const Str &value) {
            return stmt.BindText(base + offset, TextOf(value));
        };

        Status s = Status::Ok;
        if ((s = bind_int(0, check.type)) != Status::Ok) return Translate(s);
        if ((s = bind_int(1, check.flags)) != Status::Ok) return Translate(s);
        if ((s = bind_int(2, check.check_state)) != Status::Ok) return Translate(s);
        if ((s = bind(3, NullableId(check.user_open))) != Status::Ok) return Translate(s);
        if ((s = bind(4, NullableId(check.user_owner))) != Status::Ok) return Translate(s);
        if ((s = bind(5, NullableId(check.customer_id))) != Status::Ok) return Translate(s);
        if ((s = bind_int(6, check.call_center_id)) != Status::Ok) return Translate(s);
        if ((s = bind_int(7, check.guests)) != Status::Ok) return Translate(s);
        if ((s = bind_int(8, check.has_takeouts)) != Status::Ok) return Translate(s);
        if ((s = bind_int(9, check.checknum)) != Status::Ok) return Translate(s);
        // CF_TRAINING is the flag System::SaveCheck refuses to write on. Storing
        // it as a column means a training check can be excluded by a WHERE
        // clause instead of by remembering to mask a bit.
        if ((s = bind_int(10, (check.flags & CF_TRAINING) ? 1 : 0)) != Status::Ok)
            return Translate(s);
        if ((s = bind_text(11, check.label)) != Status::Ok) return Translate(s);
        if ((s = bind_text(12, check.comment)) != Status::Ok) return Translate(s);
        if ((s = bind_text(13, check.termname)) != Status::Ok) return Translate(s);
        if ((s = bind(14, LocalSeconds(check.time_open))) != Status::Ok) return Translate(s);
        if ((s = bind(15, LocalSeconds(check.chef_time))) != Status::Ok) return Translate(s);
        if ((s = bind(16, LocalSeconds(check.made_time))) != Status::Ok) return Translate(s);
        if ((s = bind(17, LocalSeconds(check.check_in))) != Status::Ok) return Translate(s);
        if ((s = bind(18, LocalSeconds(check.check_out))) != Status::Ok) return Translate(s);
        if ((s = bind(19, LocalSeconds(check.date))) != Status::Ok) return Translate(s);
        return StoreError::None;
    }

    static constexpr int kCheckColumnCount = 20;

    StoreError InsertCheck(const Check &check, int64_t &out_id)
    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db_,
                "INSERT INTO pos_check("
                "  business_day_id, serial_number,"
                "  type, flags, check_state, user_open, user_owner, customer_id,"
                "  call_center_id, guests, has_takeouts, checknum, is_training,"
                "  label, comment, termname,"
                "  time_open_local, chef_time_local, made_time_local,"
                "  check_in_local, check_out_local, date_local)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12,"
                "         ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22)"
                " RETURNING id;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = stmt.BindInt(1, business_day_id_); s != Status::Ok)
            return Translate(s);
        if (Status s = stmt.BindInt(2, check.serial_number); s != Status::Ok)
            return Translate(s);
        if (const StoreError e = BindCheckColumns(stmt, check, 3);
            e != StoreError::None)
        {
            return e;
        }

        Status step = Status::Ok;
        if (!stmt.Step(step))
            return Fail(db_, step, "insert check");
        out_id = stmt.ColumnInt(0);
        return StoreError::None;
    }

    StoreError UpdateCheck(int64_t check_id, const Check &check)
    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db_,
                "UPDATE pos_check SET"
                "  type = ?1, flags = ?2, check_state = ?3, user_open = ?4,"
                "  user_owner = ?5, customer_id = ?6, call_center_id = ?7,"
                "  guests = ?8, has_takeouts = ?9, checknum = ?10,"
                "  is_training = ?11, label = ?12, comment = ?13, termname = ?14,"
                "  time_open_local = ?15, chef_time_local = ?16,"
                "  made_time_local = ?17, check_in_local = ?18,"
                "  check_out_local = ?19, date_local = ?20"
                " WHERE id = ?21;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (const StoreError e = BindCheckColumns(stmt, check, 1);
            e != StoreError::None)
        {
            return e;
        }
        if (Status s = stmt.BindInt(kCheckColumnCount + 1, check_id); s != Status::Ok)
            return Translate(s);
        return Report(stmt.Execute(), "update check");
    }

    // Takes a non-const SubCheck only because OrderList() and PaymentList()
    // have no const overloads. Nothing here mutates it.
    StoreError SaveSubCheck(int64_t check_id, SubCheck &sub, int seq)
    {
        int64_t sub_id = 0;
        {
            Statement stmt;
            if (Status s = stmt.Prepare(
                    db_,
                    "INSERT INTO subcheck("
                    "  check_id, business_day_id, seq, status, check_type,"
                    "  settle_user, settle_time_local, drawer_id, tax_exempt,"
                    "  new_QST_method, frozen_at_local)"
                    " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, NULL)"
                    " RETURNING id;");
                s != Status::Ok)
            {
                return Translate(s);
            }
            Status s = Status::Ok;
            if ((s = stmt.BindInt(1, check_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(2, business_day_id_)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(3, seq)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(4, sub.status)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(5, sub.check_type)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindOptionalInt(6, NullableId(sub.settle_user))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindOptionalInt(7, LocalSeconds(sub.settle_time))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindOptionalInt(8, NullableId(sub.drawer_id))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindText(9, TextOf(sub.tax_exempt))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindInt(10, sub.new_QST_method)) != Status::Ok)
                return Translate(s);

            Status step = Status::Ok;
            if (!stmt.Step(step))
                return Fail(db_, step, "insert subcheck");
            sub_id = stmt.ColumnInt(0);
        }

        // The order tree, stored rather than inferred. Legacy wrote a flat run
        // of parents-then-modifiers and rebuilt the shape on load from
        // Order::IsModifier() plus adjacency, so the relationship was never
        // recorded and could not be recovered when the inference was wrong.
        int order_seq = 0;
        for (const Order *order = sub.OrderList(); order != nullptr;
             order = order->next, ++order_seq)
        {
            int64_t order_id = 0;
            if (const StoreError e =
                    InsertOrder(sub_id, std::nullopt, order_seq, *order, order_id);
                e != StoreError::None)
            {
                return e;
            }

            int mod_seq = 0;
            for (const Order *mod = order->modifier_list; mod != nullptr;
                 mod = mod->next, ++mod_seq)
            {
                int64_t mod_id = 0;
                if (const StoreError e =
                        InsertOrder(sub_id, order_id, mod_seq, *mod, mod_id);
                    e != StoreError::None)
                {
                    return e;
                }
            }
        }

        int payment_seq = 0;
        for (const Payment *payment = sub.PaymentList(); payment != nullptr;
             payment = payment->next, ++payment_seq)
        {
            if (const StoreError e = InsertPayment(sub_id, payment_seq, *payment);
                e != StoreError::None)
            {
                return e;
            }
        }

        return InsertTotals(sub_id, sub);
    }

    StoreError InsertOrder(int64_t sub_id, std::optional<int64_t> parent_id,
                           int seq, const Order &order, int64_t &out_id)
    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db_,
                "INSERT INTO order_item("
                "  subcheck_id, business_day_id, parent_order_id, seq, call_order,"
                "  item_name, item_type, item_family, sales_type, item_cost,"
                "  reduced_cost, qualifier, status, user_id, seat, count,"
                "  employee_meal, is_reduced, auto_coupon_id, total_cost, total_comp)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12,"
                "         ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21)"
                " RETURNING id;");
            s != Status::Ok)
        {
            return Translate(s);
        }

        Status s = Status::Ok;
        if ((s = stmt.BindInt(1, sub_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(2, business_day_id_)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindOptionalInt(3, parent_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(4, seq)) != Status::Ok) return Translate(s);
        // call_order is written for the first time here. Order::Write never
        // emitted it even though Order::Add sorts modifiers by it, so modifiers
        // reordered across every legacy save/load. Live saves carry the real
        // value; historical rows imported later cannot recover one.
        if ((s = stmt.BindInt(5, order.call_order)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindText(6, TextOf(order.item_name))) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(7, order.item_type)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(8, order.item_family)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(9, order.sales_type)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(10, order.item_cost)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(11, order.reduced_cost)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(12, order.qualifier)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(13, order.status)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindOptionalInt(14, NullableId(order.user_id))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindInt(15, order.seat)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(16, order.count)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(17, order.employee_meal)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(18, order.is_reduced)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(19, order.auto_coupon_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(20, order.total_cost)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(21, order.total_comp)) != Status::Ok) return Translate(s);

        Status step = Status::Ok;
        if (!stmt.Step(step))
            return Fail(db_, step, "insert order");
        out_id = stmt.ColumnInt(0);
        return StoreError::None;
    }

    StoreError InsertPayment(int64_t sub_id, int seq, const Payment &payment)
    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db_,
                "INSERT INTO payment("
                "  subcheck_id, business_day_id, seq, tender_type,"
                "  legacy_tender_id, amount, flags, user_id, drawer_id, value,"
                "  synthesized)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);");
            s != Status::Ok)
        {
            return Translate(s);
        }

        Status s = Status::Ok;
        if ((s = stmt.BindInt(1, sub_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(2, business_day_id_)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(3, seq)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(4, payment.tender_type)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(5, payment.tender_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(6, payment.amount)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(7, payment.flags)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindOptionalInt(8, NullableId(payment.user_id))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindOptionalInt(9, NullableId(payment.drawer_id))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindInt(10, payment.value)) != Status::Ok) return Translate(s);
        // FigureTotals deletes and recreates change, overage and money-lost
        // rows on every call. Flagging them means a later reader can tell a
        // regenerated row from one that was actually tendered.
        if ((s = stmt.BindInt(11, IsSynthesized(payment.tender_type) ? 1 : 0))
            != Status::Ok)
        {
            return Translate(s);
        }
        return Report(stmt.Execute(), "insert payment");
    }

    static bool IsSynthesized(int tender_type) noexcept
    {
        return tender_type == TENDER_CHANGE ||
               tender_type == TENDER_OVERAGE ||
               tender_type == TENDER_MONEY_LOST;
    }

    StoreError InsertTotals(int64_t sub_id, const SubCheck &sub)
    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db_,
                "INSERT INTO subcheck_total("
                "  subcheck_id, raw_sales, total_sales, tax_food, tax_alcohol,"
                "  tax_room, tax_merchandise, tax_GST, tax_PST, tax_HST, tax_QST,"
                "  tax_VAT, total_cost, item_comps, payment, balance, tab_total,"
                "  delivery_charge, computed_at_local, source)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12,"
                "         ?13, ?14, ?15, ?16, ?17, ?18, ?19, 0);");
            s != Status::Ok)
        {
            return Translate(s);
        }

        Status s = Status::Ok;
        if ((s = stmt.BindInt(1, sub_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(2, sub.raw_sales)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(3, sub.total_sales)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(4, sub.total_tax_food)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(5, sub.total_tax_alcohol)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(6, sub.total_tax_room)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(7, sub.total_tax_merchandise)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(8, sub.total_tax_GST)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(9, sub.total_tax_PST)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(10, sub.total_tax_HST)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(11, sub.total_tax_QST)) != Status::Ok) return Translate(s);
        // tax_VAT is stored explicitly. EndDay used to drop it when snapshotting
        // a day's policy, so every archived check recomputed VAT as zero.
        if ((s = stmt.BindInt(12, sub.total_tax_VAT)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(13, sub.total_cost)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(14, sub.item_comps)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(15, sub.payment)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(16, sub.balance)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(17, sub.tab_total)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(18, sub.delivery_charge)) != Status::Ok) return Translate(s);
        // computed_at_local is NOT NULL, so an unsettled subcheck records 0
        // rather than NULL. The two are distinguishable: settle_time_local on
        // the subcheck row is NULL in exactly that case.
        const auto computed_at = LocalSeconds(sub.settle_time);
        if ((s = stmt.BindInt(19, computed_at.value_or(0))) != Status::Ok)
            return Translate(s);
        return Report(stmt.Execute(), "insert subcheck totals");
    }

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
