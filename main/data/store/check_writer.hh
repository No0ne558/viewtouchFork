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

#include <cstdint>
#include <optional>

namespace vt::sql { class Database; }

class Check;
class Order;
class Payment;
class SubCheck;

namespace vt::store {

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
