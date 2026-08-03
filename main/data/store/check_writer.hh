/*
 * check_writer.hh - Writes a Check aggregate into the SQL schema.
 *
 * Shared by the live SQL backend (sqlite_store.cc) and the importer
 * (importer.cc), because they write the same rows and any divergence between
 * them would show up as a false positive in the dual-run diff -- i.e. exactly
 * where it would be least useful and hardest to attribute.
 *
 * The two callers differ in three parameters, all explicit:
 *
 *   business_day_id   the live path writes to the open day; the importer writes
 *                     to whichever day the archive became.
 *
 *   freeze            the importer freezes what it writes -- those totals are
 *                     historical fact and must not be recomputed. The live path
 *                     does not, because a settled subcheck can be reopened, and
 *                     auto-freezing on close would break that.
 *
 *   source            0 = computed live, 1 = imported as found. Reports need to
 *                     distinguish "this is what was charged" from "this is what
 *                     today's code would compute".
 *
 * This is not a repository: it has no policy about whether a check should be
 * written at all. The archive/copy dispatch and the frozen-check refusal live
 * with the caller, because the importer's answers to both differ from the live
 * path's.
 */

#ifndef VT_STORE_CHECK_WRITER_HH
#define VT_STORE_CHECK_WRITER_HH

#include "store.hh"

#include "sql/database.hh"

#include <cstdint>
#include <optional>
#include <string>

class Check;
class Order;
class Payment;
class Str;
class SubCheck;
class TimeInfo;

namespace vt::store {

/*
 * Shared SQL binding helpers.
 *
 * These live here rather than in each repository because the drawer repository
 * needs exactly the same conversions the check writer does, and two copies of
 * "how a TimeInfo becomes a column" is two places for them to drift.
 */

// TimeInfo is date::local_time with no zone attached, so the only value that can
// be written without inventing information is the local one. The matching *_utc
// columns stay NULL until TimeInfo records a zone; guessing one would be wrong
// by an hour twice a year and silently so.
[[nodiscard]] std::optional<int64_t> LocalSeconds(const TimeInfo &time);

/*
 * The same instant resolved to UTC, or nullopt when it cannot be.
 *
 * This is the companion the schema was designed with and which nothing wrote
 * until now: `_local` is the legacy wall-clock value, `_utc` is the unambiguous
 * instant. Storing both is what lets a report over a date range mean something
 * across a daylight-saving boundary, which the legacy format structurally could
 * not express -- a `TimeInfo` is a `date::local_time` with no zone attached.
 *
 * Two local times have no single UTC answer, and both return nullopt rather
 * than a guess:
 *
 *   Ambiguous -- the hour that happens twice when clocks go back. There are
 *   genuinely two instants and nothing was recorded to choose between them.
 *
 *   Nonexistent -- the hour skipped when clocks go forward. A timestamp inside
 *   it is corrupt rather than merely unclear.
 *
 * NULL in those two cases is not a gap to fill in later. It is the honest
 * statement that this particular wall-clock reading does not identify a moment,
 * and it is why the column is nullable.
 */
[[nodiscard]] std::optional<int64_t> UtcSeconds(const TimeInfo &time);

// The zone those conversions were made against, recorded per business day so a
// later reader can tell what `_local` meant. Empty if no zone database is
// available, in which case every `_utc` is NULL too.
[[nodiscard]] std::string StoreTimeZoneName();

// Ids are 0 when absent throughout this codebase. NULL says "nobody", which a
// zero cannot, and keeps a foreign key from having to accept a sentinel.
[[nodiscard]] std::optional<int64_t> NullableId(int id);

[[nodiscard]] std::string TextOf(const Str &value);

// sqlite Status to the seam's StoreError. Was defined identically and
// separately in three translation units; one copy means a new Status value has
// one switch to be added to.
[[nodiscard]] StoreError Translate(vt::sql::Status status) noexcept;

// Log why a statement failed, then translate. A Constraint return alone says a
// rule was broken but not which one, and the schema has foreign keys, partial
// unique indexes, CHECK constraints and five triggers that all produce it.
[[nodiscard]] StoreError Fail(vt::sql::Database &db, vt::sql::Status status,
                              const char *what);

// Same, for statements run via Execute() whose result is a bare Status.
[[nodiscard]] StoreError Report(vt::sql::Database &db, vt::sql::Status status,
                                const char *what);

// Row counts, so an import can report what it actually moved rather than
// "done".
struct WriteCounts
{
    int subchecks{0};
    int orders{0};
    int modifiers{0};
    int payments{0};
};

class CheckWriter
{
public:
    // `source` matches subcheck_total.source: 0 computed live, 1 imported as
    // found, 2 recomputed during import.
    CheckWriter(vt::sql::Database &db, int64_t business_day_id,
                bool freeze, int source);

    // Insert a new pos_check row and everything under it. `serial_disambiguator`
    // is non-zero only when a genuine same-day serial collision has to be made
    // representable, which happens in historical archives because the legacy
    // counter was never persisted.
    [[nodiscard]] StoreError InsertAggregate(const Check &check,
                                             int serial_disambiguator,
                                             int64_t &out_check_id);

    // Update an existing pos_check row in place and rewrite its children.
    [[nodiscard]] StoreError ReplaceChildren(int64_t check_id, const Check &check);

    [[nodiscard]] const WriteCounts &Counts() const noexcept { return counts_; }
    void ResetCounts() noexcept { counts_ = WriteCounts{}; }

private:
    [[nodiscard]] StoreError WriteSubChecks(int64_t check_id, const Check &check);
    [[nodiscard]] StoreError WriteSubCheck(int64_t check_id, SubCheck &sub, int seq);
    [[nodiscard]] StoreError InsertOrder(int64_t subcheck_id,
                                         std::optional<int64_t> parent_id,
                                         int seq, const Order &order,
                                         int64_t &out_id);
    [[nodiscard]] StoreError InsertPayment(int64_t subcheck_id, int seq,
                                           const Payment &payment);
    [[nodiscard]] StoreError WriteTotals(int64_t subcheck_id, const SubCheck &sub);

    vt::sql::Database &db_;
    int64_t business_day_id_;
    bool freeze_;
    int source_;
    WriteCounts counts_;
};

// Look up a check row by its day-scoped serial. Returns NotFound when absent,
// which is not an error -- it is how the live path decides insert vs. replace.
[[nodiscard]] StoreError FindCheckBySerial(vt::sql::Database &db,
                                           int64_t business_day_id,
                                           int serial_number, int64_t &out_id);

// Whether any subcheck under this check has been frozen. Rewriting one deletes
// and reinserts its rows, which the frozen-totals trigger cannot catch because
// it fires on UPDATE.
[[nodiscard]] StoreError CheckHasFrozenSubCheck(vt::sql::Database &db,
                                                int64_t check_id, bool &out);

} // namespace vt::store

#endif // VT_STORE_CHECK_WRITER_HH
