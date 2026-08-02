/*
 * check_writer.cc - Row-level writing of a Check aggregate. See check_writer.hh
 * for why this is shared between the live backend and the importer.
 */

#include "check_writer.hh"

#include "check.hh"

#include "sql/database.hh"
#include "sql/statement.hh"
#include "vt_logger.hh"

#include <optional>
#include <string>

namespace vt::store {

using vt::sql::Database;
using vt::sql::Statement;
using vt::sql::Status;

namespace {

StoreError Translate(Status status) noexcept
{
    switch (status)
    {
    case Status::Ok:         return StoreError::Ok;
    case Status::CannotOpen: return StoreError::Io;
    case Status::Busy:       return StoreError::Busy;
    case Status::Constraint: return StoreError::Constraint;
    case Status::Corrupt:    return StoreError::Corrupt;
    case Status::SqlError:   return StoreError::Io;
    case Status::NotOpen:    return StoreError::Io;
    }
    return StoreError::Io;
}

} // namespace

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

StoreError Report(Database &db, Status status, const char *what)
{
    return (status == Status::Ok) ? StoreError::Ok : Fail(db, status, what);
}

// TimeInfo is date::local_time, with no zone attached, so the only value that
// can be written without inventing information is the local one. The matching
// *_utc columns stay NULL until TimeInfo records a zone; guessing one here
// would be wrong by an hour twice a year and silently so.
std::optional<int64_t> LocalSeconds(const TimeInfo &time)
{
    if (!time.IsSet())
        return std::nullopt;
    return static_cast<int64_t>(time.get_local_time().time_since_epoch().count());
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

namespace {

// FigureTotals deletes and recreates these three on every call, so a reader
// needs to tell a regenerated row from one that was actually tendered.
bool IsSynthesized(int tender_type) noexcept
{
    return tender_type == TENDER_CHANGE ||
           tender_type == TENDER_OVERAGE ||
           tender_type == TENDER_MONEY_LOST;
}

// The pos_check columns bound by BindCheckColumns, so INSERT and UPDATE agree.
constexpr int kCheckColumnCount = 20;

StoreError BindCheckColumns(Database &db, Statement &stmt, const Check &check,
                            int base)
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
    (void)db;

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
    // CF_TRAINING is the flag System::SaveCheck refuses to write on. Storing it
    // as a column means a training check can be excluded by a WHERE clause
    // instead of by remembering to mask a bit.
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
    return StoreError::Ok;
}

} // namespace

StoreError FindCheckBySerial(Database &db, int64_t business_day_id,
                             int serial_number, int64_t &out_id)
{
    Statement stmt;
    if (Status s = stmt.Prepare(
            db, "SELECT id FROM pos_check "
                "WHERE business_day_id = ?1 AND serial_number = ?2;");
        s != Status::Ok)
    {
        return Translate(s);
    }
    if (Status s = stmt.BindInt(1, business_day_id); s != Status::Ok)
        return Translate(s);
    if (Status s = stmt.BindInt(2, serial_number); s != Status::Ok)
        return Translate(s);

    Status step = Status::Ok;
    if (stmt.Step(step))
    {
        out_id = stmt.ColumnInt(0);
        return StoreError::Ok;
    }
    if (step != Status::Ok)
        return Translate(step);
    return StoreError::NotFound;
}

StoreError CheckHasFrozenSubCheck(Database &db, int64_t check_id, bool &out)
{
    int64_t frozen = 0;
    const std::string sql =
        "SELECT COUNT(*) FROM subcheck WHERE check_id = " +
        std::to_string(check_id) + " AND frozen_at_local IS NOT NULL;";
    if (Status s = db.QueryInt(sql, frozen); s != Status::Ok)
        return Translate(s);
    out = (frozen > 0);
    return StoreError::Ok;
}

CheckWriter::CheckWriter(Database &db, int64_t business_day_id, bool freeze,
                         int source)
    : db_(db), business_day_id_(business_day_id), freeze_(freeze), source_(source)
{
}

StoreError CheckWriter::InsertAggregate(const Check &check,
                                        int serial_disambiguator,
                                        int64_t &out_check_id)
{
    Statement stmt;
    if (Status s = stmt.Prepare(
            db_,
            "INSERT INTO pos_check("
            "  business_day_id, serial_number, serial_disambiguator,"
            "  type, flags, check_state, user_open, user_owner, customer_id,"
            "  call_center_id, guests, has_takeouts, checknum, is_training,"
            "  label, comment, termname,"
            "  time_open_local, chef_time_local, made_time_local,"
            "  check_in_local, check_out_local, date_local)"
            " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12,"
            "         ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23)"
            " RETURNING id;");
        s != Status::Ok)
    {
        return Translate(s);
    }
    if (Status s = stmt.BindInt(1, business_day_id_); s != Status::Ok)
        return Translate(s);
    if (Status s = stmt.BindInt(2, check.serial_number); s != Status::Ok)
        return Translate(s);
    if (Status s = stmt.BindInt(3, serial_disambiguator); s != Status::Ok)
        return Translate(s);
    if (const StoreError e = BindCheckColumns(db_, stmt, check, 4);
        e != StoreError::Ok)
    {
        return e;
    }

    Status step = Status::Ok;
    if (!stmt.Step(step))
        return Fail(db_, step, "insert check");
    out_check_id = stmt.ColumnInt(0);

    return WriteSubChecks(out_check_id, check);
}

StoreError CheckWriter::ReplaceChildren(int64_t check_id, const Check &check)
{
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
        if (const StoreError e = BindCheckColumns(db_, stmt, check, 1);
            e != StoreError::Ok)
        {
            return e;
        }
        if (Status s = stmt.BindInt(kCheckColumnCount + 1, check_id); s != Status::Ok)
            return Translate(s);
        if (const StoreError e = Report(db_, stmt.Execute(), "update check");
            e != StoreError::Ok)
        {
            return e;
        }
    }

    {
        // ON DELETE CASCADE carries the orders, payments and totals with it.
        Statement stmt;
        if (Status s = stmt.Prepare(db_, "DELETE FROM subcheck WHERE check_id = ?1;");
            s != Status::Ok)
        {
            return Translate(s);
        }
        if (Status s = stmt.BindInt(1, check_id); s != Status::Ok)
            return Translate(s);
        if (const StoreError e = Report(db_, stmt.Execute(), "delete subchecks");
            e != StoreError::Ok)
        {
            return e;
        }
    }

    return WriteSubChecks(check_id, check);
}

StoreError CheckWriter::WriteSubChecks(int64_t check_id, const Check &check)
{
    int seq = 0;
    for (SubCheck *sub = const_cast<Check &>(check).SubList(); sub != nullptr;
         sub = sub->next, ++seq)
    {
        if (const StoreError e = WriteSubCheck(check_id, *sub, seq);
            e != StoreError::Ok)
        {
            return e;
        }
    }
    return StoreError::Ok;
}

// Takes a non-const SubCheck only because OrderList() and PaymentList() have no
// const overloads. Nothing here mutates it.
StoreError CheckWriter::WriteSubCheck(int64_t check_id, SubCheck &sub, int seq)
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
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)"
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
        const auto settled = LocalSeconds(sub.settle_time);
        if ((s = stmt.BindOptionalInt(7, settled)) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindOptionalInt(8, NullableId(sub.drawer_id))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindText(9, TextOf(sub.tax_exempt))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindInt(10, sub.new_QST_method)) != Status::Ok)
            return Translate(s);
        // Freezing is the importer's job, not the live path's: an imported
        // total is what a customer was actually charged and must never be
        // recomputed, whereas a live subcheck can still be reopened. When there
        // is no settle time to freeze at, fall back to the check's open time so
        // the row is still marked immutable rather than silently editable.
        std::optional<int64_t> frozen_at;
        if (freeze_)
            frozen_at = settled.value_or(0);
        if ((s = stmt.BindOptionalInt(11, frozen_at)) != Status::Ok)
            return Translate(s);

        Status step = Status::Ok;
        if (!stmt.Step(step))
            return Fail(db_, step, "insert subcheck");
        sub_id = stmt.ColumnInt(0);
    }
    ++counts_.subchecks;

    // The order tree, stored rather than inferred. Legacy wrote a flat run of
    // parents-then-modifiers and rebuilt the shape on load from
    // Order::IsModifier() plus adjacency, so the relationship was never recorded
    // and could not be recovered when the inference was wrong.
    int order_seq = 0;
    for (const Order *order = sub.OrderList(); order != nullptr;
         order = order->next, ++order_seq)
    {
        int64_t order_id = 0;
        if (const StoreError e =
                InsertOrder(sub_id, std::nullopt, order_seq, *order, order_id);
            e != StoreError::Ok)
        {
            return e;
        }
        ++counts_.orders;

        int mod_seq = 0;
        for (const Order *mod = order->modifier_list; mod != nullptr;
             mod = mod->next, ++mod_seq)
        {
            int64_t mod_id = 0;
            if (const StoreError e =
                    InsertOrder(sub_id, order_id, mod_seq, *mod, mod_id);
                e != StoreError::Ok)
            {
                return e;
            }
            ++counts_.modifiers;
        }
    }

    int payment_seq = 0;
    for (const Payment *payment = sub.PaymentList(); payment != nullptr;
         payment = payment->next, ++payment_seq)
    {
        if (const StoreError e = InsertPayment(sub_id, payment_seq, *payment);
            e != StoreError::Ok)
        {
            return e;
        }
        ++counts_.payments;
    }

    return WriteTotals(sub_id, sub);
}

StoreError CheckWriter::InsertOrder(int64_t sub_id,
                                    std::optional<int64_t> parent_id, int seq,
                                    const Order &order, int64_t &out_id)
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
    // call_order is written for the first time here. Order::Write never emitted
    // it even though Order::Add sorts modifiers by it, so modifiers reordered
    // across every legacy save/load. Live saves carry the real value; imported
    // historical rows cannot recover one and arrive as the constructor default.
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
    return StoreError::Ok;
}

StoreError CheckWriter::InsertPayment(int64_t sub_id, int seq,
                                      const Payment &payment)
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
    if ((s = stmt.BindInt(11, IsSynthesized(payment.tender_type) ? 1 : 0))
        != Status::Ok)
    {
        return Translate(s);
    }
    return Report(db_, stmt.Execute(), "insert payment");
}

StoreError CheckWriter::WriteTotals(int64_t sub_id, const SubCheck &sub)
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
            "         ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20);");
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
    // tax_VAT is stored explicitly. EndDay used to drop it when snapshotting a
    // day's policy, so every archived check recomputed VAT as zero.
    if ((s = stmt.BindInt(12, sub.total_tax_VAT)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(13, sub.total_cost)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(14, sub.item_comps)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(15, sub.payment)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(16, sub.balance)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(17, sub.tab_total)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(18, sub.delivery_charge)) != Status::Ok) return Translate(s);
    // computed_at_local is NOT NULL, so an unsettled subcheck records 0 rather
    // than NULL. The two are distinguishable: settle_time_local on the subcheck
    // row is NULL in exactly that case.
    const auto computed_at = LocalSeconds(sub.settle_time);
    if ((s = stmt.BindInt(19, computed_at.value_or(0))) != Status::Ok)
        return Translate(s);
    if ((s = stmt.BindInt(20, source_)) != Status::Ok) return Translate(s);
    return Report(db_, stmt.Execute(), "insert subcheck totals");
}

} // namespace vt::store
