/*
 * importer.hh - One-time conversion of legacy data files into SQLite.
 *
 * Reads through the production InputDataFile readers -- Archive::LoadPacked and
 * Check::Load -- rather than a parallel parser. That is deliberate and is the
 * only way the import can be trusted: the legacy format has version gates going
 * back to 1997, a token-counting disambiguation hack for two incompatible
 * "version 10" layouts, and per-sub-entity version stamps inside each archive.
 * A second implementation of all that would be a second set of bugs, and the
 * ones it got wrong would be silent.
 *
 * ORIGINALS ARE NEVER MODIFIED. Nothing here opens a legacy file for writing,
 * renames one, or deletes one. That is what makes rollback real: a site that
 * wants to go back reinstalls the previous version and points it at the data
 * directory it always had. A test asserts the bytes are unchanged rather than
 * trusting the claim.
 *
 * What the import cannot recover, it records rather than papers over:
 *
 *   Order::call_order was never written by Order::Write, so every imported
 *   modifier arrives with the constructor default of 1 and the original kitchen
 *   ordering is gone. Live saves from here on carry the real value.
 *
 *   Settings::tax_takeout_food had no Archive field, so a day's takeout food
 *   tax basis is whatever the setting happens to be now, not what it was.
 *
 *   String escaping was lossy in both directions: the writer mapped ' ' and '~'
 *   onto '_' and the reader mapped '_' back to ' ', so "foo_bar" already reads
 *   as "foo bar" on disk today. Imported names carry that damage.
 *
 *   EndDay never snapshotted tax_VAT or advertise_fund, so every archive holds
 *   zero for both. day_policy.snapshot_complete is set to 0 for imported days so
 *   a report can tell "the rate was zero" from "the rate was never recorded".
 *
 *   No historical total is authoritative, because none was ever stored.
 *   SubCheck's money fields are all derived, and SubCheck::Read recomputes them
 *   with FigureTotals at the end of every load (check.cc:3491). So what the
 *   importer receives is what today's engine computes from the orders against
 *   the archive's frozen rates -- not the figure printed on the receipt, which
 *   was never written down. Imported totals are frozen so they stop drifting,
 *   and marked source = 2 so nothing mistakes them for ground truth. PR 14's
 *   dual-run has to account for this: there is no stored historical number to
 *   diff against.
 */

#ifndef VT_STORE_IMPORTER_HH
#define VT_STORE_IMPORTER_HH

#include "store.hh"

#include <cstdint>
#include <string>
#include <vector>

class Settings;

namespace vt::store {

struct ImportStats
{
    int days{0};              // business_day rows created
    int archives_read{0};
    int archives_failed{0};   // unreadable or wrong version; recorded, not fatal
    int checks{0};
    int subchecks{0};
    int orders{0};
    int modifiers{0};
    int payments{0};
    int training_skipped{0};  // training checks are not revenue
    int serial_collisions{0}; // resolved with serial_disambiguator
    int64_t highest_serial{0};
};

struct ImportResult
{
    StoreError error{StoreError::None};
    std::string message;
    ImportStats stats;

    // Archives that could not be read, with the reason. An import that skipped
    // a day must say which one; a count alone is not actionable.
    std::vector<std::string> failures;

    [[nodiscard]] bool Ok() const noexcept { return error == StoreError::None; }
};

/*
 * Import every archive in `archive_path` into the database at `db_path`.
 *
 * `settings` supplies the version-gated defaults the legacy readers need for
 * fields absent from older files; the archive's own frozen policy overrides it
 * for anything that affects money.
 *
 * Each archive becomes one business_day, closed, with its policy snapshot and
 * its source version stamps recorded for audit. Every imported subcheck is
 * frozen, and its totals are marked source = 2, "recomputed during import".
 *
 * Idempotent by archive filename -- business_day.legacy_filename is UNIQUE, and
 * an archive already present is skipped rather than duplicated, so a failed run
 * can be repeated.
 */
[[nodiscard]] ImportResult ImportArchives(const std::string &archive_path,
                                          const std::string &db_path,
                                          Settings &settings);

} // namespace vt::store

#endif // VT_STORE_IMPORTER_HH
