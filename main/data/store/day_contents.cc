#include "day_contents.hh"

#include "check_writer.hh"

#include "archive.hh"
#include "credit.hh"
#include "exception.hh"
#include "expense.hh"
#include "settings.hh"
#include "system.hh"
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

MediaSnapshot MediaFromSettings(Settings &settings)
{
    return MediaSnapshot{settings.DiscountList(), settings.CouponList(),
                         settings.CreditCardList(), settings.CompList(),
                         settings.MealList()};
}

MediaSnapshot MediaFromArchive(Archive &archive)
{
    return MediaSnapshot{archive.DiscountList(), archive.CouponList(),
                         archive.CreditCardList(), archive.CompList(),
                         archive.MealList()};
}

namespace {

// Media kind ids, matching media_kind_ref in migration 0006.
constexpr int kMediaDiscount = 1;
constexpr int kMediaCoupon = 2;
constexpr int kMediaCreditCard = 3;
constexpr int kMediaComp = 4;
constexpr int kMediaMeal = 5;

// Insert one media row and hand back its id, which the coupon extension needs.
StoreError InsertMedia(Database &db, int64_t day_id, int kind, int legacy_id,
                       const Str &name, int is_local, int amount, int flags,
                       int active, int64_t &out_id)
{
    Statement stmt;
    if (Status s = stmt.Prepare(
            db, "INSERT INTO day_media("
                "  business_day_id, media_kind, legacy_id, name, is_local,"
                "  amount, flags, active)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)"
                " RETURNING id;");
        s != Status::Ok)
    {
        return Translate(s);
    }

    Status s = Status::Ok;
    if ((s = stmt.BindInt(1, day_id)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(2, kind)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(3, legacy_id)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindText(4, TextOf(name))) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(5, is_local)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(6, amount)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(7, flags)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(8, active)) != Status::Ok) return Translate(s);

    Status step = Status::Ok;
    if (!stmt.Step(step))
        return Fail(db, step, "insert day media");

    out_id = stmt.ColumnInt(0);
    return StoreError::Ok;
}

// A coupon validity boundary. Nullable because a coupon with no window is a
// different thing from one that starts at midnight, and TimeInfo distinguishes
// them with IsSet().
StoreError BindOptionalLocal(Statement &stmt, int index, const TimeInfo &time)
{
    const std::optional<int64_t> local = LocalSeconds(time);
    const Status s = local.has_value() ? stmt.BindInt(index, *local)
                                       : stmt.BindNull(index);
    return (s == Status::Ok) ? StoreError::Ok : Translate(s);
}

} // namespace

StoreError WriteDayMedia(Database &db, int64_t day_id, const MediaSnapshot &media)
{
    // day_media_coupon cascades from day_media, so clearing the parent is
    // enough. Replace rather than append, like every other writer here.
    if (const StoreError e = ClearDay(db, day_id, "day_media"); e != StoreError::Ok)
        return e;

    int64_t row_id = 0;

    for (DiscountInfo *d = media.discounts; d != nullptr; d = d->next)
    {
        if (const StoreError e = InsertMedia(db, day_id, kMediaDiscount, d->id,
                                             d->name, d->IsLocal(), d->amount,
                                             d->flags, d->active, row_id);
            e != StoreError::Ok)
        {
            return e;
        }
    }

    for (CreditCardInfo *c = media.credit_cards; c != nullptr; c = c->next)
    {
        // No amount and no flags on this one -- zero is correct rather than
        // absent, because there is no amount for it to be missing.
        if (const StoreError e = InsertMedia(db, day_id, kMediaCreditCard, c->id,
                                             c->name, c->IsLocal(), 0, 0,
                                             c->active, row_id);
            e != StoreError::Ok)
        {
            return e;
        }
    }

    for (CompInfo *c = media.comps; c != nullptr; c = c->next)
    {
        if (const StoreError e = InsertMedia(db, day_id, kMediaComp, c->id,
                                             c->name, c->IsLocal(), 0, c->flags,
                                             c->active, row_id);
            e != StoreError::Ok)
        {
            return e;
        }
    }

    for (MealInfo *m = media.meals; m != nullptr; m = m->next)
    {
        if (const StoreError e = InsertMedia(db, day_id, kMediaMeal, m->id,
                                             m->name, m->IsLocal(), m->amount,
                                             m->flags, m->active, row_id);
            e != StoreError::Ok)
        {
            return e;
        }
    }

    for (CouponInfo *c = media.coupons; c != nullptr; c = c->next)
    {
        if (const StoreError e = InsertMedia(db, day_id, kMediaCoupon, c->id,
                                             c->name, c->IsLocal(), c->amount,
                                             c->flags, c->active, row_id);
            e != StoreError::Ok)
        {
            return e;
        }

        Statement stmt;
        if (Status s = stmt.Prepare(
                db, "INSERT INTO day_media_coupon("
                    "  day_media_id, automatic, item_family, item_id, item_name,"
                    "  start_time_local, end_time_local, start_date_local,"
                    "  end_date_local, days, months)"
                    " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);");
            s != Status::Ok)
        {
            return Translate(s);
        }

        Status s = Status::Ok;
        if ((s = stmt.BindInt(1, row_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(2, c->automatic)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(3, c->family)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(4, c->item_id)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindText(5, TextOf(c->item_name))) != Status::Ok) return Translate(s);

        if (const StoreError e = BindOptionalLocal(stmt, 6, c->start_time);
            e != StoreError::Ok) return e;
        if (const StoreError e = BindOptionalLocal(stmt, 7, c->end_time);
            e != StoreError::Ok) return e;
        if (const StoreError e = BindOptionalLocal(stmt, 8, c->start_date);
            e != StoreError::Ok) return e;
        if (const StoreError e = BindOptionalLocal(stmt, 9, c->end_date);
            e != StoreError::Ok) return e;

        if ((s = stmt.BindInt(10, c->days)) != Status::Ok) return Translate(s);
        if ((s = stmt.BindInt(11, c->months)) != Status::Ok) return Translate(s);

        if (const StoreError e = Report(db, stmt.Execute(), "insert coupon media");
            e != StoreError::Ok)
        {
            return e;
        }
    }

    return StoreError::Ok;
}

StoreError WriteCreditTransactions(Database &db, int64_t day_id,
                                   CreditDB *voids, CreditDB *refunds,
                                   CreditDB *exceptions)
{
    if (const StoreError e = ClearDay(db, day_id, "credit_transaction");
        e != StoreError::Ok)
    {
        return e;
    }

    Statement stmt;
    if (Status s = stmt.Prepare(
            db, "INSERT INTO credit_transaction("
                "  business_day_id, db_kind, card_number, pan_is_masked,"
                "  last_four, expire, card_holder, card_type, credit_type,"
                "  processor, approval, auth_code, response_code, batch, item,"
                "  ttid, amount, tip, full_amount, last_action, state,"
                "  auth_state, trans_success, sequence)"
                " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12,"
                "         ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22,"
                "         ?23, ?24);");
        s != Status::Ok)
    {
        return Translate(s);
    }

    // The one decision about what leaves memory, made once. Credit::Write asks
    // the same question of the same setting; nothing here reaches past PAN().
    const int store_full = (MasterSystem != nullptr)
                               ? MasterSystem->settings.save_entire_cc_num : 0;

    struct Source { CreditDB *db; int kind; };
    const Source sources[] = {{voids, 1}, {refunds, 2}, {exceptions, 3}};

    for (const Source &source : sources)
    {
        if (source.db == nullptr)
            continue;

        int64_t sequence = 0;
        for (Credit *credit = source.db->CreditList(); credit != nullptr;
             credit = credit->next)
        {
            if (credit->IsEmpty())
                continue;   // matches CreditDB::Write, which skips them too

            Status s = Status::Ok;
            if ((s = stmt.BindInt(1, day_id)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(2, source.kind)) != Status::Ok) return Translate(s);
            if ((s = stmt.BindText(3, credit->PAN(store_full))) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindInt(4, store_full ? 0 : 1)) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindText(5, credit->LastFour())) != Status::Ok)
                return Translate(s);
            if ((s = stmt.BindText(6, credit->Expire())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindText(7, credit->CardName())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(8, credit->CardType())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(9, credit->CreditType())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(10, credit->Processor())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindText(11, credit->Approval())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindText(12, credit->Auth())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindText(13, credit->Code())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(14, credit->BatchId())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(15, credit->ItemId())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(16, credit->TTID())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(17, credit->Amount())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(18, credit->Tip())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(19, credit->FullAmount())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(20, credit->LastAction())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(21, credit->State())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(22, credit->AuthState())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(23, credit->TransSuccess())) != Status::Ok) return Translate(s);
            if ((s = stmt.BindInt(24, sequence)) != Status::Ok) return Translate(s);
            ++sequence;

            if (const StoreError e =
                    Report(db, stmt.Execute(), "insert credit transaction");
                e != StoreError::Ok)
            {
                return e;
            }
            if (Status r = stmt.Reset(); r != Status::Ok)
                return Translate(r);
        }
    }

    return StoreError::Ok;
}

StoreError WriteDayContents(Database &db, int64_t day_id, TipDB &tips,
                            ExpenseDB &expenses, ExceptionDB &exceptions,
                            const MediaSnapshot &media)
{
    if (const StoreError e = WriteTips(db, day_id, tips); e != StoreError::Ok)
        return e;
    if (const StoreError e = WriteExpenses(db, day_id, expenses); e != StoreError::Ok)
        return e;
    if (const StoreError e = WriteExceptions(db, day_id, exceptions);
        e != StoreError::Ok)
    {
        return e;
    }
    return WriteDayMedia(db, day_id, media);
}

} // namespace vt::store
