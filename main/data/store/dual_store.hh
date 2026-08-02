/*
 * dual_store.hh - Write through two backends and compare what they persisted.
 *
 * This is how the migration gets verified instead of asserted. A site runs with
 * the legacy backend still authoritative and SQLite shadowing it; every save
 * goes to both, and Compare() reports where the persisted forms differ. Cutover
 * becomes a decision made against evidence from that site's own data.
 *
 * Which side is authoritative matters and is explicit:
 *
 *   The primary's result is what callers see. A shadow failure never fails a
 *   save. That is the only safe arrangement while the shadow is unproven --
 *   a bug in the new backend must not be able to stop a restaurant taking
 *   money -- and it is why the shadow's errors are counted and logged rather
 *   than returned.
 *
 *   Compare() is not free and is not called per save. It walks both backends
 *   in full, so it belongs at a quiet moment: end of day, or on demand.
 *
 * A non-empty divergence report is the expected result, not a failure. The two
 * backends genuinely differ in known places -- call_order is not written by the
 * legacy format at all, and string escaping is lossy in both directions. The
 * value is a precise list of where and why, which is what makes it possible to
 * say a difference is understood rather than merely absent.
 */

#ifndef VT_STORE_DUAL_STORE_HH
#define VT_STORE_DUAL_STORE_HH

#include "store.hh"

#include <memory>
#include <string>
#include <vector>

namespace vt::store {

// What the shadow did that the primary did not. Counted rather than thrown,
// because the shadow is never allowed to fail a save.
struct ShadowHealth
{
    int saves{0};
    int save_failures{0};
    int removes{0};
    int remove_failures{0};
    std::string last_error;   // StoreErrorName of the most recent failure
};

class DualRunStore : public Store
{
public:
    // Both backends are owned. The primary is authoritative: its return value
    // is what a caller sees and its transaction is the one that governs.
    DualRunStore(std::unique_ptr<Store> primary, std::unique_ptr<Store> shadow);
    ~DualRunStore() override;

    [[nodiscard]] std::unique_ptr<Transaction> Begin() override;
    [[nodiscard]] CheckRepository &Checks() override;
    [[nodiscard]] DrawerRepository &Drawers() override;

    // Reports the primary's answer. A dual run does not gain the guarantee just
    // by having a backend that offers it -- callers must keep behaving as if
    // the weaker of the two applies until cutover.
    [[nodiscard]] bool SupportsAtomicWrites() const noexcept override;

    // Ends the day on both. The primary's answer governs, but unlike a save a
    // shadow failure here is worth surfacing immediately: a shadow whose day
    // never closes silently overwrites its own records the next day, and the
    // divergence report would only show that after the damage.
    [[nodiscard]] StoreError EndBusinessDay() override;

    // Fails if either side is unhealthy. Unlike a save, there is nothing to
    // lose by surfacing a shadow problem here, and a shadow that cannot be read
    // makes the whole exercise pointless.
    [[nodiscard]] StoreError HealthCheck() override;

    // The primary's snapshot, so a DualRunStore substitutes for its primary.
    [[nodiscard]] StoreError Snapshot(StoreSnapshot &out) override;

    [[nodiscard]] const char *Name() const noexcept override;

    // Snapshot both and diff. Expensive; see the note above.
    [[nodiscard]] StoreError Compare(std::vector<Divergence> &out);

    // Compare() plus a human-readable report. Empty string means no divergence.
    [[nodiscard]] StoreError CompareAndDescribe(std::string &report);

    [[nodiscard]] const ShadowHealth &Shadow() const noexcept;

    [[nodiscard]] Store &Primary() noexcept;
    [[nodiscard]] Store &ShadowStore() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::unique_ptr<DualRunStore> MakeDualRunStore(
    std::unique_ptr<Store> primary, std::unique_ptr<Store> shadow);

} // namespace vt::store

#endif // VT_STORE_DUAL_STORE_HH
