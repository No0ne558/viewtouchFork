#include "day_policy.hh"

#include "check_writer.hh"

#include "archive.hh"
#include "settings.hh"
#include "sql/database.hh"
#include "sql/statement.hh"

namespace vt::store {

using vt::sql::Database;
using vt::sql::Statement;
using vt::sql::Status;

DayPolicy PolicyFromSettings(const Settings &settings)
{
    // Mirrors Archive::CopyPolicyFrom, deliberately: the two are the same
    // snapshot taken through different doors, and they must stay the same
    // snapshot. If you add a rate here, add it there.
    DayPolicy policy;
    policy.tax_food              = settings.tax_food;
    policy.tax_alcohol           = settings.tax_alcohol;
    policy.tax_room              = settings.tax_room;
    policy.tax_merchandise       = settings.tax_merchandise;
    policy.tax_GST               = settings.tax_GST;
    policy.tax_PST               = settings.tax_PST;
    policy.tax_HST               = settings.tax_HST;
    policy.tax_QST               = settings.tax_QST;
    policy.tax_VAT               = settings.tax_VAT;
    policy.royalty_rate          = settings.royalty_rate;
    policy.advertise_fund        = settings.advertise_fund;
    policy.price_rounding        = settings.price_rounding;
    policy.change_for_credit     = settings.change_for_credit;
    policy.change_for_roomcharge = settings.change_for_roomcharge;
    policy.change_for_checks     = settings.change_for_checks;
    policy.change_for_gift       = settings.change_for_gift;
    policy.discount_alcohol      = settings.discount_alcohol;
    policy.tax_takeout_food      = settings.tax_takeout_food;

    // Authoritative: read from the live Settings at the moment the day closed,
    // which is the only moment they are still the day's own rates.
    policy.complete = true;
    return policy;
}

DayPolicy PolicyFromArchive(const Archive &archive, const Settings &settings)
{
    DayPolicy policy;
    policy.tax_food              = archive.tax_food;
    policy.tax_alcohol           = archive.tax_alcohol;
    policy.tax_room              = archive.tax_room;
    policy.tax_merchandise       = archive.tax_merchandise;
    policy.tax_GST               = archive.tax_GST;
    policy.tax_PST               = archive.tax_PST;
    policy.tax_HST               = archive.tax_HST;
    policy.tax_QST               = archive.tax_QST;
    // Zero in every archive ever written: EndDay's open-coded policy copy
    // omitted tax_VAT, which is one reason imported days are never complete.
    policy.tax_VAT               = archive.tax_VAT;
    policy.royalty_rate          = archive.royalty_rate;
    // Same omission as tax_VAT.
    policy.advertise_fund        = archive.advertise_fund;
    policy.price_rounding        = archive.price_rounding;
    policy.change_for_credit     = archive.change_for_credit;
    policy.change_for_roomcharge = archive.change_for_roomcharge;
    policy.change_for_checks     = archive.change_for_checks;
    policy.change_for_gift       = archive.change_for_gift;
    policy.discount_alcohol      = archive.discount_alcohol;

    // The archive has no field for this at all -- FigureTotals read it live
    // from Settings, which is why toggling it restated food tax on every
    // archived takeout check. Today's value is the best available answer.
    policy.tax_takeout_food      = settings.tax_takeout_food;

    // Never authoritative. Two independent reasons, before considering whether
    // the archive's own policy block loaded (Archive::policy_from_file): the
    // legacy end-of-day did not record tax_VAT or advertise_fund, and
    // tax_takeout_food was never in the format.
    policy.complete = false;
    return policy;
}

StoreError WriteDayPolicy(Database &db, int64_t day_id, const DayPolicy &policy)
{
    Statement stmt;
    // Upsert rather than insert. EndDay is not atomic across every step it
    // performs, so an interrupted one that is re-run must not fail on a day it
    // already stamped -- and must not leave two policies for one day either.
    if (Status s = stmt.Prepare(
            db,
            "INSERT INTO day_policy("
            "  business_day_id, tax_food, tax_alcohol, tax_room, tax_merchandise,"
            "  tax_GST, tax_PST, tax_HST, tax_QST, tax_VAT, royalty_rate,"
            "  advertise_fund, price_rounding, change_for_credit,"
            "  change_for_roomcharge, change_for_checks, change_for_gift,"
            "  discount_alcohol, tax_takeout_food, store_tz, snapshot_complete)"
            " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13,"
            "         ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21)"
            " ON CONFLICT(business_day_id) DO UPDATE SET"
            "  tax_food = excluded.tax_food,"
            "  tax_alcohol = excluded.tax_alcohol,"
            "  tax_room = excluded.tax_room,"
            "  tax_merchandise = excluded.tax_merchandise,"
            "  tax_GST = excluded.tax_GST,"
            "  tax_PST = excluded.tax_PST,"
            "  tax_HST = excluded.tax_HST,"
            "  tax_QST = excluded.tax_QST,"
            "  tax_VAT = excluded.tax_VAT,"
            "  royalty_rate = excluded.royalty_rate,"
            "  advertise_fund = excluded.advertise_fund,"
            "  price_rounding = excluded.price_rounding,"
            "  change_for_credit = excluded.change_for_credit,"
            "  change_for_roomcharge = excluded.change_for_roomcharge,"
            "  change_for_checks = excluded.change_for_checks,"
            "  change_for_gift = excluded.change_for_gift,"
            "  discount_alcohol = excluded.discount_alcohol,"
            "  tax_takeout_food = excluded.tax_takeout_food,"
            "  store_tz = excluded.store_tz,"
            "  snapshot_complete = excluded.snapshot_complete;");
        s != Status::Ok)
    {
        return Translate(s);
    }

    // Rates bind as REAL, not text. A tax rate that round-trips through decimal
    // moves in its third decimal place, and it multiplies every sale on the day.
    Status s = Status::Ok;
    if ((s = stmt.BindInt(1, day_id)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(2, policy.tax_food)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(3, policy.tax_alcohol)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(4, policy.tax_room)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(5, policy.tax_merchandise)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(6, policy.tax_GST)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(7, policy.tax_PST)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(8, policy.tax_HST)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(9, policy.tax_QST)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(10, policy.tax_VAT)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(11, policy.royalty_rate)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(12, policy.advertise_fund)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(13, policy.price_rounding)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(14, policy.change_for_credit)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(15, policy.change_for_roomcharge)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(16, policy.change_for_checks)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(17, policy.change_for_gift)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(18, policy.discount_alcohol)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(19, policy.tax_takeout_food)) != Status::Ok) return Translate(s);
    // The zone the _utc columns were resolved against, so a later reader can
    // tell what the _local values meant rather than assuming the machine that
    // reads them is configured like the one that wrote them.
    if ((s = stmt.BindText(20, StoreTimeZoneName())) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(21, policy.complete ? 1 : 0)) != Status::Ok) return Translate(s);

    return Translate(stmt.Execute());
}

} // namespace vt::store
