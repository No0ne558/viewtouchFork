/*
 * importer.cc - Legacy files to SQLite. See importer.hh for the contract,
 * above all that originals are never modified.
 */

#include "importer.hh"

#include "archive.hh"
#include "check.hh"
#include "check_writer.hh"
#include "day_contents.hh"
#include "day_policy.hh"
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
    return StoreError::Ok;
}

StoreError InsertBusinessDay(Database &db, const Archive &archive,
                             const std::string &filename, int64_t &out_id)
{
    Statement stmt;
    if (Status s = stmt.Prepare(
            db,
            "INSERT INTO business_day("
            "  legacy_filename, start_local, end_local, closed_at_local,"
            "  start_utc, end_utc,"
            "  last_serial_number, corrupt,"
            "  src_file_version, src_check_version, src_drawer_version,"
            "  src_tip_version, src_work_version, src_exception_version,"
            "  src_expense_version, src_media_version, src_settings_version)"
            " VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13,"
            "         ?14, ?15, ?16, ?17)"
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
    // The unambiguous companions. An archive's times came off a file that
    // recorded no zone, so a day whose start or end lands in a repeated or
    // skipped hour resolves to NULL rather than to a guess.
    if ((s = stmt.BindOptionalInt(5, UtcSeconds(archive.start_time))) != Status::Ok)
        return Translate(s);
    if ((s = stmt.BindOptionalInt(6, UtcSeconds(archive.end_time))) != Status::Ok)
        return Translate(s);
    if ((s = stmt.BindInt(7, archive.last_serial_number)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(8, archive.corrupt ? 1 : 0)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(9, archive.file_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(10, archive.check_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(11, archive.drawer_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(12, archive.tip_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(13, archive.work_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(14, archive.exception_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(15, archive.expense_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(16, archive.media_version)) != Status::Ok) return Translate(s);
    if ((s = stmt.BindInt(17, archive.settings_version)) != Status::Ok) return Translate(s);

    Status step = Status::Ok;
    if (!stmt.Step(step))
        return (step == Status::Ok) ? StoreError::Io : Translate(step);
    out_id = stmt.ColumnInt(0);
    return StoreError::Ok;
}

StoreError InsertDayPolicy(Database &db, int64_t day_id, const Archive &archive,
                           const Settings &settings)
{
    // One writer for day_policy, shared with the path a live day closes
    // through. Policy copying used to be open-coded per call site, and the copy
    // in System::EndDay omitted tax_VAT and advertise_fund -- silently zeroing
    // VAT on every archived check. A single writer means a new policy field has
    // one place to be added rather than three.
    return WriteDayPolicy(db, day_id, PolicyFromArchive(archive, settings));
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
        if (found != StoreError::Ok && found != StoreError::NotFound)
            return found;
        if (found == StoreError::Ok)
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
            e != StoreError::Ok)
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
    return StoreError::Ok;
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
            e != StoreError::Ok)
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

            if (archive.policy_from_file == 0)
            {
                // Not a failure: the day's checks and drawers are intact and
                // worth importing. But its tax rates are today's, not the ones
                // in force when it traded, so totals recomputed against them
                // are not the figures that were printed. Named individually,
                // because "some of your history has the wrong rates" is not
                // something an operator can act on.
                ++result.stats.policy_not_in_archive;
                ::vt::Logger::warn(
                    "import: {} carries no frozen policy of its own "
                    "(archive version {}); day_policy holds current rates",
                    file, archive.file_version);
            }

            int64_t day_id = 0;
            StoreError step = InsertBusinessDay(db, archive, file, day_id);
            if (step == StoreError::Ok)
                step = InsertDayPolicy(db, day_id, archive, settings);
            if (step == StoreError::Ok)
                step = ImportChecks(db, day_id, archive, result.stats);
            if (step == StoreError::Ok)
            {
                // The rest of the day: tips, expenses and audit exceptions.
                // The same writers the live close path uses, so an imported day
                // and a traded one produce the same rows from the same code
                // rather than from two transcriptions that can drift.
                step = WriteDayContents(db, day_id, archive.tip_db,
                                        archive.expense_db,
                                        archive.exception_db,
                                        MediaFromArchive(archive));
            }
            if (step == StoreError::Ok)
            {
                // The day's credit exceptions, refunds and voids. Archive
                // version 13 introduced them; older days have none, and the
                // null pointers here are exactly that.
                step = WriteCreditTransactions(db, day_id,
                                               archive.cc_void_db,
                                               archive.cc_refund_db,
                                               archive.cc_exception_db);
            }

            if (step != StoreError::Ok)
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
