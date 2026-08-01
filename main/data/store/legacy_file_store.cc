/*
 * legacy_file_store.cc - The existing file persistence behind the Store seam.
 *
 * Every method here delegates to System, so behaviour is byte-for-byte what it
 * was. That is the point: introducing the interface must not change what gets
 * written, or the dual-run comparison in Phase 4 would be measuring this change
 * rather than the SQL backend.
 */

#include "store.hh"

#include "archive.hh"
#include "check.hh"
#include "data_persistence_manager.hh"
#include "system.hh"

#include <memory>

namespace vt::store {

const char *StoreErrorName(StoreError error) noexcept
{
    switch (error)
    {
    case StoreError::None:        return "ok";
    case StoreError::NotFound:    return "not found";
    case StoreError::Io:          return "i/o error";
    case StoreError::Corrupt:     return "corrupt";
    case StoreError::Constraint:  return "constraint violation";
    case StoreError::Busy:        return "busy";
    case StoreError::Unsupported: return "unsupported by this backend";
    }
    return "unknown";
}

namespace {

/*
 * The legacy format has no transactions.
 *
 * Each check is its own file and each write stands alone, so there is no way to
 * group several of them. Commit therefore succeeds without having grouped
 * anything and Rollback cannot undo a write that already happened.
 *
 * This is modelled honestly rather than hidden: Store::SupportsAtomicWrites()
 * returns false for this backend, so a caller that genuinely needs all-or-nothing
 * can detect it. Pretending otherwise would make the two backends look
 * interchangeable when their crash behaviour differs completely -- which is the
 * single largest thing the migration buys.
 *
 * Note the individual writes are still atomic and durable at the file level
 * after the temp-file-plus-rename change. What is missing is atomicity *across*
 * files: EndDay touching a dozen of them can still be interrupted halfway.
 */
class LegacyTransaction final : public Transaction
{
public:
    StoreError Commit() override
    {
        active_ = false;
        return StoreError::None;
    }

    void Rollback() noexcept override
    {
        // Nothing to undo. Writes already reached disk individually.
        active_ = false;
    }

    [[nodiscard]] bool IsActive() const noexcept override { return active_; }

private:
    bool active_{true};
};

class LegacyCheckRepository final : public CheckRepository
{
public:
    explicit LegacyCheckRepository(System *system) : system_(system) {}

    StoreError Save(Transaction &, Check &check) override
    {
        if (system_ == nullptr)
            return StoreError::Io;

        // This mirrors Check::Save() (check.cc:326) step for step rather than
        // calling it, because Check::Save() reaches the MasterSystem global
        // while the seam takes its System by injection. Getting the dispatch
        // wrong here is not a subtle difference: System::SaveCheck alone
        // ignores `copy` entirely and would write a working copy to disk under
        // the real check's filename.
        //
        // The dirty marking is part of the behaviour being preserved -- it is
        // what tells DataPersistenceManager the check set needs an autosave --
        // so it belongs on this side of the seam until call sites move over.
        GetDataPersistenceManager().MarkDataDirty("checks");

        if (check.archive != nullptr)
        {
            // Archived checks are never written individually. The archive
            // rewrites its whole day the next time it is saved, so all this
            // does is mark that day dirty.
            check.archive->changed = 1;
            return StoreError::None;
        }

        if (check.copy != 0)
        {
            // A working copy shares the original's identity. Persisting it
            // would overwrite the original with an uncommitted edit.
            return StoreError::None;
        }

        if (system_->SaveCheck(&check) != 0)
            return StoreError::Io;

        GetDataPersistenceManager().MarkDataClean("checks");
        return StoreError::None;
    }

    StoreError Remove(Transaction &, Check &check) override
    {
        if (system_ == nullptr)
            return StoreError::Io;

        return (system_->DestroyCheck(&check) == 0) ? StoreError::None : StoreError::Io;
    }

    StoreError Count(int &out) override
    {
        if (system_ == nullptr)
            return StoreError::Io;

        // Counts what is in memory rather than what is on disk. For this backend
        // those are the same set: the check list is loaded from current/ at
        // startup and written back from it.
        int total = 0;
        for (const Check *check = system_->CheckList(); check != nullptr;
             check = check->next)
        {
            ++total;
        }
        out = total;
        return StoreError::None;
    }

private:
    System *system_;
};

class LegacyFileStore final : public Store
{
public:
    explicit LegacyFileStore(System *system)
        : system_(system), checks_(system) {}

    [[nodiscard]] std::unique_ptr<Transaction> Begin() override
    {
        return std::make_unique<LegacyTransaction>();
    }

    [[nodiscard]] CheckRepository &Checks() override { return checks_; }

    [[nodiscard]] bool SupportsAtomicWrites() const noexcept override
    {
        return false;   // see LegacyTransaction
    }

    [[nodiscard]] StoreError HealthCheck() override
    {
        return (system_ != nullptr) ? StoreError::None : StoreError::Io;
    }

    [[nodiscard]] const char *Name() const noexcept override
    {
        return "legacy-file";
    }

private:
    System *system_;
    LegacyCheckRepository checks_;
};

} // namespace

std::unique_ptr<Store> MakeLegacyFileStore(System *system)
{
    return std::make_unique<LegacyFileStore>(system);
}

} // namespace vt::store
