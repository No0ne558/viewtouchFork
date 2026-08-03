/*
 * day_contents.hh - the rest of a closed business day: tips, expenses,
 * exceptions.
 *
 * An archive file holds five things: the day's checks, its drawers, the tax
 * policy it traded under, these three, and a snapshot of the media (discounts,
 * coupons, comps) in force. The first three are migrated; these are the fourth.
 *
 * Like day_policy, each has exactly one writer, shared by the two paths that
 * produce a day: System::EndDay closing a live day, and the importer replaying
 * an archive. Two writers for the same table is how the tax_VAT omission
 * happened -- one copy grew a field and the other did not.
 *
 * All three are written wholesale rather than incrementally. A day's tips,
 * expenses and exceptions are small (tens of rows, not thousands), they are
 * only meaningful as a set, and the legacy format wrote them the same way --
 * as a counted list inside the archive. Writing them at close, in the same
 * transaction that stamps the day closed, means a day is complete or absent.
 */

#ifndef VT_STORE_DAY_CONTENTS_HH
#define VT_STORE_DAY_CONTENTS_HH

#include "store.hh"

#include <cstdint>

class ExceptionDB;
class ExpenseDB;
class TipDB;

namespace vt::sql { class Database; }

namespace vt::store {

/*
 * Replace this day's tip entries with what `tips` holds.
 *
 * Replace, not append: a TipEntry is a running balance for one employee, not
 * an event, so writing the same day twice must converge rather than double it.
 * That also makes a re-run of an interrupted EndDay safe.
 */
[[nodiscard]] StoreError WriteTips(vt::sql::Database &db, int64_t day_id,
                                   TipDB &tips);

// Replace this day's expenses. Same reasoning; `sequence` preserves the order
// the file format carried only as position.
[[nodiscard]] StoreError WriteExpenses(vt::sql::Database &db, int64_t day_id,
                                       ExpenseDB &expenses);

// Replace this day's item, table and rebuild exceptions -- three separate
// event kinds that share only a time, a user and a check serial.
[[nodiscard]] StoreError WriteExceptions(vt::sql::Database &db, int64_t day_id,
                                         ExceptionDB &exceptions);

// All three, for the callers that have all three. Stops on the first failure,
// which is safe because every caller runs inside a transaction.
[[nodiscard]] StoreError WriteDayContents(vt::sql::Database &db, int64_t day_id,
                                          TipDB &tips, ExpenseDB &expenses,
                                          ExceptionDB &exceptions);

} // namespace vt::store

#endif // VT_STORE_DAY_CONTENTS_HH
