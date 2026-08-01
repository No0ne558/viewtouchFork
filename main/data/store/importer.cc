/*
 * importer.cc - Legacy files to SQLite. See importer.hh for the contract,
 * above all that originals are never modified.
 */

#include "importer.hh"

#include "archive.hh"
#include "check.hh"
#include "check_writer.hh"
#include "settings.hh"

#include "sql/database.hh"
#include "sql/migrations.hh"
#include "sql/sequence.hh"
#include "sql/statement.hh"
#include "vt_logger.hh"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace vt::store {

using vt::sql::Database;
using vt::sql::Statement;
using vt::sql::Status;

namespace {

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

std::optional<int64_t> LocalSeconds(const TimeInfo &time)
{
    if (!time.IsSet())
        return std::nullopt;
    return static_cast<int64_t>(time.get_local_time().time_since_epoch().count());
}

// Archives are named by date, so lexical order is chronological. Importing in
// that order means business_day ids run forward in time, which every date-range
// report then gets for free.
std::vector<std::string> ArchiveFilesIn(const std::string &path)
{
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(path, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        const std::string name = entry.path().filename().string();
        // System writes archives as "archive_<serial>"; skip the dot-prefixed
        // temp files the atomic-write path leaves if a write was interrupted.
        if (name.rfind("archive", 0) == 0)
            files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Whether this archive was already imported. business_day.legacy_filename is
// UNIQUE, so this makes a repeated run skip rather than fail -- which matters
// because the first thing anyone does after a partial import is run it again.
StoreError AlreadyImported(Database &db, const std::string &filename, bool &out)
{
    Statement stmt;
    if (Status s = stmt.Prepare(
            db, "SELECT COUNT(*) FROM business_day WHERE legacy_filename = ?1;");
        s != Status::Ok)
    {
        return Translate(s);
    }
    if (Status s = stmt.BindText(1, filename); s != Status::Ok)
        return Translate(s);

    Status step = Status::Ok;
    if (!stmt.Step(step))
        return (step == Status::Ok) ? StoreError::Io : Translate(step);
    out = stmt.ColumnInt(0) > 0;
    return StoreError::None;
}

StoreError InsertBusinessDay(Database &db, const Archive &archive,
                             const std::string &filename, int64_t &out_id)
{
    Statement stmt;
    if (Status s = stmt.Prepare(
            db,
            "INSERT INTO business_day("
            "  legacy_filename, start_local, end_local, closed_at_local,"
            "  last_serial_number, corrupt,"
            "  src_file_version, src_check_version, src_drawer_version,"
            "  src_tip_version, src_work_version, src_exception_version,"
            "  src_expense_version, src_media_version, src_settings_version)"
            " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13,"
            "         ?14, ?15)"
            " RETURNING id;");
        s != Status::Ok)
    {
        return Translate(s);
    }

    const auto start = LocalSeconds(archive.start_time);
    const auto end = LocalSeconds(archive.end_time);

    Status s = Status::Ok;
    if ((s = stmt.BindText(1, filename)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindOptionalInt(2, start)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindOptionalInt(3, end)) != Status::Ok) return Translate(s);
    // An imported archive is by definition a closed day: the file only exists
    // because EndDay wrote it. Leaving closed_at_local NULL would make it the
    // one open day, which ux_business_day_open permits exactly one of -- so the
    // second archive imported would fail the index rather than the data.
    //
    // Fall back to the start time when there is no end time. A closed day with
    // an unknown close is still closed; a NULL here would be a lie the schema
    // enforces.
    if ((s = stmt.BindInt(4, end.value_or(start.value_or(0)))) != Status::Ok)
        return Translate(s);
    if ((s = stmt.BindInt(5, archive.last_serial_number)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(6, archive.corrupt ? 1 : 0)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(7, archive.file_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(8, archive.check_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(9, archive.drawer_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(10, archive.tip_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(11, archive.work_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(12, archive.exception_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(13, archive.expense_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(14, archive.media_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(15, archive.settings_version)) != Status::Ok) return Translate(s);

    Status step = Status::Ok;
    if (!stmt.Step(step))
        return (step == Status::Ok) ? StoreError::Io : Translate(step);
    out_id = stmt.ColumnInt(0);
    return StoreError::None;
}

StoreError InsertDayPolicy(Database &db, int64_t day_id, const Archive &archive,
                           const Settings &settings)
{
    Statement stmt;
    if (Status s = stmt.Prepare(
            db,
            "INSERT INTO day_policy("
            "  business_day_id, tax_food, tax_alcohol, tax_room, tax_merchandise,"
            "  tax_GST, tax_PST, tax_HST, tax_QST, tax_VAT, royalty_rate,"
            "  advertise_fund, price_rounding, change_for_credit,"
            "  change_for_roomcharge, change_for_checks, change_for_gift,"
            "  discount_alcohol, tax_takeout_food, snapshot_complete)"
            " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13,"
            "         ?14, ?15, ?16, ?17, ?18, ?19, 0);");
        s != Status::Ok)
    {
        return Translate(s);
    }

    // Rates bind as REAL, not text. A tax rate that round-trips through decimal
    // moves in its third decimal place, and it multiplies every sale on the day.
    Status s = Status::Ok;
    if ((s = stmt.BindInt(1, day_id)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(2, archive.tax_food)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(3, archive.tax_alcohol)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(4, archive.tax_room)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(5, archive.tax_merchandise)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(6, archive.tax_GST)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(7, archive.tax_PST)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(8, archive.tax_HST)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(9, archive.tax_QST)) != Status::Ok) return Translate(s);
    // Zero in every archive ever written: EndDay's open-coded policy copy
    // omitted tax_VAT, which is why snapshot_complete is 0 for imported days.
    if ((s = stmt.BindDouble(10, archive.tax_VAT)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindDouble(11, archive.royalty_rate)) != Status::Ok) return Translate(s);
    // Same omission as tax_VAT.
    if ((s = stmt.BindDouble(12, archive.advertise_fund)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(13, archive.price_rounding)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(14, archive.change_for_credit)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(15, archive.change_for_roomcharge)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(16, archive.change_for_checks)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(17, archive.change_for_gift)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(18, archive.discount_alcohol)) != Status::Ok) return Translate(s);
    // Archive has no field for this -- FigureTotals read it live from Settings,
    // which is why toggling it restated food tax on every archived takeout
    // check. The live value is the best available answer and is recorded as
    // such: snapshot_complete = 0 says this day's policy is not authoritative.
    if ((s = stmt.BindInt(19, settings.tax_takeout_food)) != Status::Ok) return Translate(s);

    return Translate(stmt.Execute());
}

/*
 * Import one archive's checks.
 *
 * Serial collisions are expected, not exceptional. System::NewSerialNumber was
 * an in-memory counter recovered at boot by scanning archives backwards, so it
 * restarted whenever the newest archive was empty or pruned -- which is how the
 * same serial ends up on two checks in one day. serial_disambiguator makes that
 * representable instead of making the import fail on real data.
 */
StoreError ImportChecks(Database &db, int64_t day_id, Archive &archive,
                        ImportStats &stats)
{
    // source = 2, "recomputed during import", not 1, "imported as found".
    //
    // The distinction is not pedantry: the legacy format stored no totals at
    // all. SubCheck::Read calls FigureTotals at the end of every load
    // (check.cc:3491), so what arrives here is what today's engine computes from
    // the orders against the archive's frozen rates -- never the number that was
    // actually printed on the receipt. That number was not written down.
    //
    // Freezing it is still the right move: it stops the value drifting further
    // on every subsequent read, which is what the legacy code did. But a report
    // must be able to tell a recomputed historical total from a live one, and
    // PR 14's dual-run must not treat this as ground truth.
    CheckWriter writer(db, day_id, /*freeze=*/true, /*source=*/2);

    for (Check *check = archive.CheckList(); check != nullptr; check = check->next)
    {
        if (check->IsTraining())
        {
            ++stats.training_skipped;
            continue;
        }

        int disambiguator = 0;
        int64_t existing = 0;
        StoreError found = FindCheckBySerial(db, day_id, check->serial_number,
                                             existing);
        if (found != StoreError::None && found != StoreError::NotFound)
            return found;
        if (found == StoreError::None)
        {
            // Walk the disambiguator up until the (day, serial, disambiguator)
            // triple is free. Historical duplicates are legal data, so this is
            // ordinary, not a repair.
            ++stats.serial_collisions;
            int64_t taken = 0;
            do
            {
                ++disambiguator;
                Statement stmt;
                if (Status s = stmt.Prepare(
                        db, "SELECT COUNT(*) FROM pos_check WHERE business_day_id = ?1"
                            " AND serial_number = ?2 AND serial_disambiguator = ?3;");
                    s != Status::Ok)
                {
                    return Translate(s);
                }
                if (Status s = stmt.BindInt(1, day_id); s != Status::Ok)
                    return Translate(s);
                if (Status s = stmt.BindInt(2, check->serial_number); s != Status::Ok)
                    return Translate(s);
                if (Status s = stmt.BindInt(3, disambiguator); s != Status::Ok)
                    return Translate(s);
                Status step = Status::Ok;
                if (!stmt.Step(step))
                    return (step == Status::Ok) ? StoreError::Io : Translate(step);
                taken = stmt.ColumnInt(0);
            }
            while (taken > 0);
        }

        int64_t check_id = 0;
        if (const StoreError e =
                writer.InsertAggregate(*check, disambiguator, check_id);
            e != StoreError::None)
        {
            return e;
        }
        ++stats.checks;
        stats.highest_serial =
            std::max<int64_t>(stats.highest_serial, check->serial_number);
    }

    const WriteCounts &counts = writer.Counts();
    stats.subchecks += counts.subchecks;
    stats.orders += counts.orders;
    stats.modifiers += counts.modifiers;
    stats.payments += counts.payments;
    return StoreError::None;
}

} // namespace

ImportResult ImportArchives(const std::string &archive_path,
                            const std::string &db_path, Settings &settings)
{
    ImportResult result;

    Database db;
    if (Status s = db.Open(db_path); s != Status::Ok)
    {
        result.error = Translate(s);
        result.message = "cannot open database: " + db.LastError();
        return result;
    }

    if (const auto migrated = vt::sql::MigrateToLatest(db);
        migrated.status != Status::Ok)
    {
        result.error = Translate(migrated.status);
        result.message = "cannot migrate database: " + migrated.error;
        return result;
    }

    for (const std::string &file : ArchiveFilesIn(archive_path))
    {
        bool seen = false;
        if (const StoreError e = AlreadyImported(db, file, seen);
            e != StoreError::None)
        {
            result.error = e;
            result.message = "cannot check for an existing import of " + file;
            return result;
        }
        if (seen)
            continue;

        // One transaction per archive. A day is the natural unit: a failure
        // partway leaves the days before it imported and this one absent, which
        // is a state the next run continues from cleanly. Wrapping the whole
        // import in one transaction would instead throw away hours of work
        // because the last file was truncated.
        vt::sql::Transaction tx(db);
        if (Status s = tx.Begin(); s != Status::Ok)
        {
            result.error = Translate(s);
            result.message = "cannot begin a transaction for " + file;
            return result;
        }

        // Scoped so the Archive is unloaded before the next iteration; a day's
        // checks and orders are the largest thing held in memory here.
        {
            Archive archive(&settings, file.c_str());
            if (archive.LoadPacked(&settings) != 0)
            {
                // Not fatal. One unreadable archive out of a decade should not
                // stop the other 3,649 from being migrated -- but the import
                // must say which one, or the gap is invisible.
                ++result.stats.archives_failed;
                result.failures.push_back(file);
                ::vt::Logger::error("import: cannot read archive {}", file);
                tx.Rollback();
                continue;
            }
            ++result.stats.archives_read;

            int64_t day_id = 0;
            StoreError step = InsertBusinessDay(db, archive, file, day_id);
            if (step == StoreError::None)
                step = InsertDayPolicy(db, day_id, archive, settings);
            if (step == StoreError::None)
                step = ImportChecks(db, day_id, archive, result.stats);

            if (step != StoreError::None)
            {
                result.error = step;
                result.message = "failed importing " + file + ": " +
                                 StoreErrorName(step);
                tx.Rollback();
                return result;
            }
            ++result.stats.days;
        }

        if (Status s = tx.Commit(); s != Status::Ok)
        {
            result.error = Translate(s);
            result.message = "cannot commit " + file + ": " + db.LastError();
            return result;
        }
    }

    // The sequence has to clear every serial already present or the first check
    // written after the import collides with a historical one. This is the
    // failure the legacy boot-time scan had: it walked archives backwards and
    // stopped at the first nonzero, so a pruned or empty newest archive
    // restarted the counter.
    if (result.stats.highest_serial > 0)
    {
        if (Status s = vt::sql::RaiseSequenceTo(db, vt::sql::kPosSerialSequence,
                                                result.stats.highest_serial + 1);
            s != Status::Ok)
        {
            result.error = Translate(s);
            result.message = "cannot raise the serial sequence past imported data";
            return result;
        }
    }

    return result;
}

} // namespace vt::store
