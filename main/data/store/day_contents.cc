#include "day_contents.hh"

#include "check_writer.hh"

#include "exception.hh"
#include "expense.hh"
#include "sql/database.hh"
#include "sql/statement.hh"
#include "tips.hh"

namespace vt::store {

using vt::sql::Database;
using vt::sql::Statement;
using vt::sql::Status;

namespace {

// Clear a day's rows before rewriting them. Every writer below replaces rather
// than appends, so that closing the same day twice converges instead of
// doubling -- which matters because EndDay is not atomic across all its steps
// and a re-run has to be safe.
StoreError ClearDay(Database &db, int64_t day_id, const char *table)
{
    Statement stmt;
    const std::string sql =
        std::string("DELETE FROM ") + table + " WHERE business_day_id = ?1;";
    if (Status s = stmt.Prepare(db, sql); s != Status::Ok)
        return Translate(s);
    if (Status s = stmt.BindInt(1, day_id); s != Status::Ok)
        return Translate(s);
    return Report(db, stmt.Execute(), table);
}

// Bind a TimeInfo's local/utc pair to two consecutive parameters. The utc side
// is NULL when the wall-clock reading names two instants or none -- see
// check_writer.hh, which explains why that is a statement rather than a gap.
StoreError BindTime(Database &db, Statement &stmt, int local_index,
                    const TimeInfo &time)
{
    const std::optional<int64_t> local = LocalSeconds(time);
    const std::optional<int64_t> utc = UtcSeconds(time);

    Status s = local.has_value() ? stmt.BindInt(local_index, *local)
                                 : stmt.BindNull(local_index);
    if (s != Status::Ok)
        return Translate(s);

    s = utc.has_value() ? stmt.BindInt(local_index + 1, *utc)
                        : stmt.BindNull(local_index + 1);
    return (s == Status::Ok) ? StoreError::Ok : Translate(s);
}

} // namespace

StoreError WriteTips(Database &db, int64_t day_id, TipDB &tips)
{
    if (const StoreError e = ClearDay(db, day_id, "tip_entry"); e != StoreError::Ok)
        return e;

    Statement stmt;
    if (Status s = stmt.Prepare(
            db, "INSERT INTO tip_entry("
                "  business_day_id, user_id, amount, previous_amount, paid)"
                " VALUES (?1, ?2, ?3, ?4, ?5);");
        s != Status::Ok)
    {
        return Translate(s);
    }

    for (TipEntry *tip = tips.TipList(); tip != nullptr; tip = tip->next)
    {
        Status s = Status::Ok;
        if ((s = stmt.BindInt(1, day_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(2, tip->user_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(3, tip->amount)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(4, tip->previous_amount)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(5, tip->paid)) != Status::Ok) return Translate(s);

        if (const StoreError e = Report(db, stmt.Execute(), "insert tip entry");
            e != StoreError::Ok)
        {
            return e;
        }
        if (Status r = stmt.Reset(); r != Status::Ok)
            return Translate(r);
    }

    return StoreError::Ok;
}

StoreError WriteExpenses(Database &db, int64_t day_id, ExpenseDB &expenses)
{
    if (const StoreError e = ClearDay(db, day_id, "expense"); e != StoreError::Ok)
        return e;

    Statement stmt;
    if (Status s = stmt.Prepare(
            db, "INSERT INTO expense("
                "  business_day_id, legacy_id, account_id, tax_account_id,"
                "  dest_account_id, employee_id, drawer_id, amount, tax,"
                "  entered, flags, document, explanation,"
                "  exp_date_local, exp_date_utc, sequence)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12,"
                "         ?13, ?14, ?15, ?16);");
        s != Status::Ok)
    {
        return Translate(s);
    }

    int64_t sequence = 0;
    for (Expense *e = expenses.ExpenseList(); e != nullptr; e = e->next)
    {
        Status s = Status::Ok;
        if ((s = stmt.BindInt(1, day_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(2, e->eid)) != Status::Ok) return Translate(s);

        // The three account references are separate things and any of them may
        // be unset. The legacy record stores a bare integer with no way to tell
        // an id from an absence, so zero becomes NULL here.
        if ((s = stmt.BindOptionalInt(3, NullableId(e->account_id))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindOptionalInt(4, NullableId(e->tax_account_id))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindOptionalInt(5, NullableId(e->dest_account_id))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindOptionalInt(6, NullableId(e->employee_id))) != Status::Ok)
            return Translate(s);
        if ((s = stmt.BindOptionalInt(7, NullableId(e->drawer_id))) != Status::Ok)
            return Translate(s);

        if ((s = stmt.BindInt(8, e->amount)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(9, e->tax)) != Status::Ok) return Translate(s);
        // Counted against a drawer, or 0 for never reconciled -- the same
        // distinction drawer_balance.entered carries.
        if ((s = stmt.BindInt(10, e->entered)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(11, e->Flags())) != Status::Ok) return Translate(s);
        if ((s = stmt.BindText(12, TextOf(e->document))) != Status::Ok) return Translate(s);
        if ((s = stmt.BindText(13, TextOf(e->explanation))) != Status::Ok) return Translate(s);

        if (const StoreError te = BindTime(db, stmt, 14, e->exp_date);
            te != StoreError::Ok)
        {
            return te;
        }

        if ((s = stmt.BindInt(16, sequence)) != Status::Ok) return Translate(s);
        ++sequence;

        if (const StoreError err = Report(db, stmt.Execute(), "insert expense");
            err != StoreError::Ok)
        {
            return err;
        }
        if (Status r = stmt.Reset(); r != Status::Ok)
            return Translate(r);
    }

    return StoreError::Ok;
}

StoreError WriteExceptions(Database &db, int64_t day_id, ExceptionDB &exceptions)
{
    for (const char *table : {"item_exception", "table_exception",
                              "rebuild_exception"})
    {
        if (const StoreError e = ClearDay(db, day_id, table); e != StoreError::Ok)
            return e;
    }

    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db, "INSERT INTO item_exception("
                    "  business_day_id, user_id, check_serial, item_name,"
                    "  item_cost, item_type, item_family, exception_type,"
                    "  reason, time_local, time_utc, sequence)"
                    " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12);");
            s != Status::Ok)
        {
            return Translate(s);
        }

        int64_t sequence = 0;
        for (ItemException *ie = exceptions.ItemList(); ie != nullptr; ie = ie->next)
        {
            Status s = Status::Ok;
            if ((s = stmt.BindInt(1, day_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindOptionalInt(2, NullableId(ie->user_id))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindInt(3, ie->check_serial)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindText(4, TextOf(ie->item_name))) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(5, ie->item_cost)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(6, ie->item_type)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(7, ie->item_family)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(8, ie->exception_type)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(9, ie->reason)) != Status::Ok) return Translate(s);
            if (const StoreError te = BindTime(db, stmt, 10, ie->time);
                te != StoreError::Ok)
            {
                return te;
            }
            if ((s = stmt.BindInt(12, sequence)) != Status::Ok) return Translate(s);
            ++sequence;

            if (const StoreError err =
                    Report(db, stmt.Execute(), "insert item exception");
                err != StoreError::Ok)
            {
                return err;
            }
            if (Status r = stmt.Reset(); r != Status::Ok)
                return Translate(r);
        }
    }

    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db, "INSERT INTO table_exception("
                    "  business_day_id, user_id, check_serial, source_id,"
                    "  target_id, table_name, time_local, time_utc, sequence)"
                    " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);");
            s != Status::Ok)
        {
            return Translate(s);
        }

        int64_t sequence = 0;
        for (TableException *te = exceptions.TableList(); te != nullptr; te = te->next)
        {
            Status s = Status::Ok;
            if ((s = stmt.BindInt(1, day_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindOptionalInt(2, NullableId(te->user_id))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindInt(3, te->check_serial)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(4, te->source_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(5, te->target_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindText(6, TextOf(te->table))) != Status::Ok) return Translate(s);
            if (const StoreError err = BindTime(db, stmt, 7, te->time);
                err != StoreError::Ok)
            {
                return err;
            }
            if ((s = stmt.BindInt(9, sequence)) != Status::Ok) return Translate(s);
            ++sequence;

            if (const StoreError err =
                    Report(db, stmt.Execute(), "insert table exception");
                err != StoreError::Ok)
            {
                return err;
            }
            if (Status r = stmt.Reset(); r != Status::Ok)
                return Translate(r);
        }
    }

    {
        Statement stmt;
        if (Status s = stmt.Prepare(
                db, "INSERT INTO rebuild_exception("
                    "  business_day_id, user_id, check_serial,"
                    "  time_local, time_utc, sequence)"
                    " VALUES (?1, ?2, ?3, ?4, ?5, ?6);");
            s != Status::Ok)
        {
            return Translate(s);
        }

        int64_t sequence = 0;
        for (RebuildException *re = exceptions.RebuildList(); re != nullptr;
             re = re->next)
        {
            Status s = Status::Ok;
            if ((s = stmt.BindInt(1, day_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindOptionalInt(2, NullableId(re->user_id))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindInt(3, re->check_serial)) != Status::Ok) return Translate(s);
            if (const StoreError err = BindTime(db, stmt, 4, re->time);
                err != StoreError::Ok)
            {
                return err;
            }
            if ((s = stmt.BindInt(6, sequence)) != Status::Ok) return Translate(s);
            ++sequence;

            if (const StoreError err =
                    Report(db, stmt.Execute(), "insert rebuild exception");
                err != StoreError::Ok)
            {
                return err;
            }
            if (Status r = stmt.Reset(); r != Status::Ok)
                return Translate(r);
        }
    }

    return StoreError::Ok;
}

StoreError WriteDayContents(Database &db, int64_t day_id, TipDB &tips,
                            ExpenseDB &expenses, ExceptionDB &exceptions)
{
    if (const StoreError e = WriteTips(db, day_id, tips); e != StoreError::Ok)
        return e;
    if (const StoreError e = WriteExpenses(db, day_id, expenses); e != StoreError::Ok)
        return e;
    return WriteExceptions(db, day_id, exceptions);
}

} // namespace vt::store
