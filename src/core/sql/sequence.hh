/*
 * sequence.hh - Exact id allocation.
 *
 * Replaces System::NewSerialNumber(), which was a plain in-memory counter that
 * was never persisted. At startup it was recovered by walking archives
 * backwards until one reported a nonzero last_serial_number -- so if the newest
 * archives were empty, or corrupt, or had been pruned, the counter silently
 * restarted and began handing out serial numbers that already existed. That is
 * why duplicate serials appear in historical data.
 *
 * Allocation here happens inside the caller's transaction, in a single
 * statement, so the value is exact and cannot regress after a crash: either the
 * transaction commits with the id consumed, or it rolls back and the id is
 * still free.
 *
 * The single statement is why the build requires SQLite >= 3.35 (RETURNING).
 */

#ifndef VT_SQL_SEQUENCE_HH
#define VT_SQL_SEQUENCE_HH

#include "database.hh"

#include <cstdint>
#include <string_view>

namespace vt::sql {

// Checks and drawers share this one sequence, matching the behaviour of
// System::NewSerialNumber, which allocated for both. Splitting them is correct
// but changes visible numbering, so it is deliberately a separate decision from
// the migration.
inline constexpr std::string_view kPosSerialSequence = "pos_serial";

// Claim the next value. Must be called inside an active transaction: on its own
// it would still be atomic, but the caller almost always needs the id and the
// row that uses it to commit together.
[[nodiscard]] Status NextSequenceValue(Database &db, std::string_view name,
                                       int64_t &out);

// Read without consuming. For diagnostics and tests; a caller that acts on this
// value has a race, and should use NextSequenceValue.
[[nodiscard]] Status PeekSequenceValue(Database &db, std::string_view name,
                                       int64_t &out);

// Ensure a sequence starts at least this high. Used by the importer: after
// loading historical data the sequence has to clear the largest serial already
// present, or the first new check would collide with an old one.
[[nodiscard]] Status RaiseSequenceTo(Database &db, std::string_view name,
                                     int64_t minimum_next);

} // namespace vt::sql

#endif // VT_SQL_SEQUENCE_HH
