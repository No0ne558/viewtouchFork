/*
 * snapshot.hh - A backend-independent view of what a store holds.
 *
 * This is the object the dual-run comparison is built on. The alternative --
 * loading each backend into `Check` objects and comparing those -- would need a
 * full SQL-to-Check reader before anything could be verified, and would compare
 * two things through a third piece of code that also has to be right.
 *
 * A snapshot is defined to be read FROM THE PERSISTED FORM, never from memory.
 * That distinction is the whole value: the in-memory Check still holds
 * call_order, the field Order::Write never emitted, so a memory-to-memory
 * comparison would report agreement on data one side cannot actually store. The
 * legacy backend therefore loads its check files back off disk to answer, and
 * the SQL backend queries.
 *
 * Consequently a diff is expected to be non-empty in known places. That is not
 * a defect in the comparison -- it is the comparison working. The point of PR
 * 14 is a list of exactly where the two backends disagree and why, not a green
 * light.
 */

#ifndef VT_STORE_SNAPSHOT_HH
#define VT_STORE_SNAPSHOT_HH

#include <string>
#include <vector>

class Check;

namespace vt::store {

struct OrderSnapshot
{
    std::string item_name;
    int item_type{0};
    int item_family{0};
    int sales_type{0};
    int item_cost{0};
    int count{0};
    int seat{0};
    int qualifier{0};
    // Never written by Order::Write, so the legacy side always reports the
    // constructor default here regardless of what was in memory.
    int call_order{0};
    // -1 for a root order; otherwise the index of its parent in the enclosing
    // subcheck's order list. An index rather than an id, because ids are
    // backend-specific and would diverge for reasons that mean nothing.
    int parent_index{-1};
};

struct PaymentSnapshot
{
    int tender_type{0};
    int tender_id{0};
    int amount{0};
    int flags{0};
};

struct SubCheckSnapshot
{
    int seq{0};
    int status{0};
    int check_type{0};

    std::vector<OrderSnapshot> orders;
    std::vector<PaymentSnapshot> payments;
};

struct CheckSnapshot
{
    int serial_number{0};
    int type{0};
    int flags{0};
    int guests{0};
    std::string label;
    std::string comment;

    std::vector<SubCheckSnapshot> subchecks;
};

struct StoreSnapshot
{
    std::string backend;                  // Store::Name(), for attribution
    std::vector<CheckSnapshot> checks;    // ordered by serial number
};

// One field that differs, named well enough to act on without re-running.
struct Divergence
{
    // e.g. "check[143].subcheck[0].order[1].call_order"
    std::string path;
    std::string left;    // value on the left backend
    std::string right;
    std::string note;    // why, when the reason is known
};

/*
 * Describe an in-memory Check.
 *
 * Used by the legacy backend on a Check it has just loaded back off disk, which
 * is what makes its snapshot a statement about the persisted form rather than
 * about memory. Calling this on a live Check would describe what the process
 * holds, not what it wrote -- see the note at the top of this file.
 */
[[nodiscard]] CheckSnapshot SnapshotOf(Check &check);

// Compare two snapshots field by field. Checks are matched by serial number, so
// a check present on one side only is reported as such rather than shifting
// every subsequent comparison.
[[nodiscard]] std::vector<Divergence> Diff(const StoreSnapshot &left,
                                           const StoreSnapshot &right);

// Human-readable report, naming both backends. Empty string when there is no
// divergence.
[[nodiscard]] std::string DescribeDivergence(const StoreSnapshot &left,
                                             const StoreSnapshot &right,
                                             const std::vector<Divergence> &found);

} // namespace vt::store

#endif // VT_STORE_SNAPSHOT_HH
