/*
 * store.hh - The persistence seam.
 *
 * Business logic talks to these interfaces instead of calling System::SaveCheck
 * and friends directly. Two implementations exist behind them: LegacyFileStore,
 * which delegates to the existing file code and changes no behaviour, and (in a
 * later change) SqliteStore. Having both behind one interface is what lets the
 * migration be verified by running them side by side and diffing, rather than by
 * a one-shot cutover and hope.
 *
 * Deliberate choices, each with a reason that comes from this codebase rather
 * than from taste:
 *
 *   Whole-aggregate granularity. Save() writes an entire check -- its subchecks,
 *   orders, modifiers and payments -- as one call. That matches Check::Save()
 *   exactly, so the legacy adapter is a delegation rather than a rewrite, and it
 *   means the SQL adapter never has to reconcile a half-written aggregate.
 *   Anything finer would need dirty-tracking, which this codebase has no concept
 *   of.
 *
 *   Transactions are passed explicitly, never ambient. A thread-local or
 *   implicit "current transaction" combined with a thread pool is the bug class
 *   this project has just finished removing from the autosave path.
 *
 *   No exceptions across the boundary. The codebase is int-return throughout;
 *   StoreError fits it. Introducing a second error idiom mid-migration would
 *   make every call site ambiguous about which one applies.
 *
 * Threading: a Store belongs to one thread. vt_main is the only writer.
 */

#ifndef VT_STORE_HH
#define VT_STORE_HH

#include <memory>
#include <string>

#include "snapshot.hh"

class Check;
class Drawer;
class System;

namespace vt::store {

/*
 * Named `Ok` rather than `None`, matching vt::sql::Status.
 *
 * `None` is also an X11 macro (`#define None 0L`), so `StoreError::None`
 * expands to `StoreError::0L` in any translation unit that includes X headers
 * -- which is where the application's call sites live. Renaming it here is the
 * durable fix; a per-file workaround would just move the trap to whoever wires
 * up the next call site.
 */
enum class StoreError
{
    Ok = 0,
    NotFound,
    Io,          // the write did not reach disk
    Corrupt,     // what was read back is not usable
    Constraint,  // rejected by a schema rule
    Busy,        // another writer holds the lock
    Unsupported, // this backend cannot do it -- see LegacyFileStore
};

[[nodiscard]] const char *StoreErrorName(StoreError error) noexcept;

/*
 * A unit of work.
 *
 * The legacy backend cannot honour this: its writes are individual files with
 * no way to group them, so its Transaction accepts Commit and Rollback and
 * neither means anything. That is not an oversight to paper over -- it is
 * precisely the property the migration exists to gain, and naming it here keeps
 * the two backends' guarantees comparable rather than implicitly assumed equal.
 * SupportsAtomicWrites() lets a caller that genuinely needs the guarantee ask.
 */
class Transaction
{
public:
    virtual ~Transaction() = default;

    virtual StoreError Commit() = 0;
    virtual void Rollback() noexcept = 0;
    [[nodiscard]] virtual bool IsActive() const noexcept = 0;
};

class CheckRepository
{
public:
    virtual ~CheckRepository() = default;

    // Persist the whole aggregate. Creates or replaces.
    //
    // Three cases, matching Check::Save(): an archived check marks its archive
    // dirty and is not written on its own; a working copy is not written at
    // all; anything else is written. The first two report None -- nothing went
    // wrong, there was simply nothing to write.
    virtual StoreError Save(Transaction &tx, Check &check) = 0;

    // Remove the aggregate and everything under it.
    //
    // OWNERSHIP: on success the check is destroyed and the reference is
    // dangling. This is inherited from System::DestroyCheck, which unlinks,
    // unlinks the file and `delete`s in one step; the seam does not paper over
    // it, because a backend that quietly kept the object alive would leak on
    // every void. Never call this on a check the caller did not heap-allocate
    // and hand to System.
    virtual StoreError Remove(Transaction &tx, Check &check) = 0;

    // Number of checks currently persisted. Cheap on SQL, a directory walk on
    // the legacy backend, so callers should not put it in a loop.
    [[nodiscard]] virtual StoreError Count(int &out) = 0;
};

/*
 * Drawers: the other half of end-of-day reconciliation.
 *
 * Simpler than checks because Drawer::Save() has only two cases rather than
 * three -- there is no such thing as a drawer copy. Deletion is not on this
 * interface at all: the legacy code never deletes a drawer through a System
 * method, it calls Drawer::DestroyFile() directly from EndDay, and inventing a
 * Remove() nothing calls would be interface for its own sake.
 */
class DrawerRepository
{
public:
    virtual ~DrawerRepository() = default;

    // Persist the whole drawer -- its payments and its balances. An archived
    // drawer marks its archive dirty and is not written on its own, matching
    // Drawer::Save().
    virtual StoreError Save(Transaction &tx, Drawer &drawer) = 0;

    [[nodiscard]] virtual StoreError Count(int &out) = 0;
};

class Store
{
public:
    virtual ~Store() = default;

    [[nodiscard]] virtual std::unique_ptr<Transaction> Begin() = 0;
    [[nodiscard]] virtual CheckRepository &Checks() = 0;
    [[nodiscard]] virtual DrawerRepository &Drawers() = 0;

    // Whether a Transaction from this store actually groups its writes. False
    // for the legacy backend. A caller that must not leave a partial state --
    // EndDay above all -- has to check this rather than assume.
    [[nodiscard]] virtual bool SupportsAtomicWrites() const noexcept = 0;

    // Cheap readiness probe: is the backing store reachable and writable.
    [[nodiscard]] virtual StoreError HealthCheck() = 0;

    /*
     * Everything this backend has persisted, in a backend-independent form.
     *
     * Read from the persisted form, never from memory -- see snapshot.hh. The
     * legacy backend loads its check files back off disk to answer this, which
     * is deliberate and is the only way the dual-run comparison says anything:
     * an in-memory Check still holds fields the format cannot write, so
     * comparing memory to memory would report agreement on data one side
     * cannot actually store.
     *
     * Not cheap. This walks everything, so it belongs in verification and
     * diagnostics, not in the save path.
     */
    [[nodiscard]] virtual StoreError Snapshot(StoreSnapshot &out) = 0;

    // Human-readable backend name, for logs and for the dual-run divergence
    // report to attribute a difference to a side.
    [[nodiscard]] virtual const char *Name() const noexcept = 0;
};

// The existing file-based persistence, behind the interface. Delegates to
// System, so behaviour is unchanged -- this exists to establish the seam, not to
// improve the legacy path.
[[nodiscard]] std::unique_ptr<Store> MakeLegacyFileStore(System *system);

/*
 * The SQLite backend.
 *
 * Opens (creating if absent) the database at `path`, migrates it to the latest
 * schema, and resolves the one open business day, creating it if there is none.
 * Pass ":memory:" for a throwaway database, which is what the tests use.
 *
 * Returns nullptr on failure with `error` set; on success `error` is None. The
 * out-parameter exists because opening can fail for reasons a caller must
 * distinguish -- Io for an unopenable file, Corrupt for a database whose schema
 * will not migrate -- and a factory cannot express that in its return type
 * without exceptions, which this boundary does not use.
 */
[[nodiscard]] std::unique_ptr<Store> MakeSqliteStore(const std::string &path,
                                                     StoreError &error);

} // namespace vt::store

#endif // VT_STORE_HH
