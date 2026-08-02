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
#include "drawer.hh"
#include "sales.hh"
#include "system.hh"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace vt::store {

const char *StoreErrorName(StoreError error) noexcept
{
    switch (error)
    {
    case StoreError::Ok:        return "ok";
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
        return StoreError::Ok;
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
        // The DataPersistenceManager dirty-marking used to happen here, on the
        // grounds that it belonged on this side of the seam "until call sites
        // move over". They have: Check::Save() now routes through a store, and
        // does the marking itself, so both backends behave the same and the
        // autosave bookkeeping stays with the caller it belongs to.

        if (check.archive != nullptr)
        {
            // Archived checks are never written individually. The archive
            // rewrites its whole day the next time it is saved, so all this
            // does is mark that day dirty.
            check.archive->changed = 1;
            return StoreError::Ok;
        }

        if (check.copy != 0)
        {
            // A working copy shares the original's identity. Persisting it
            // would overwrite the original with an uncommitted edit.
            return StoreError::Ok;
        }

        return (system_->SaveCheck(&check) == 0) ? StoreError::Ok
                                                 : StoreError::Io;
    }

    StoreError Remove(Transaction &, Check &check) override
    {
        if (system_ == nullptr)
            return StoreError::Io;

        // DestroyCheckDirect, not DestroyCheck: the latter routes back through
        // the configured store, which is this object. They have to be separate
        // functions or they would call each other forever.
        return (system_->DestroyCheckDirect(&check) == 0) ? StoreError::Ok
                                                          : StoreError::Io;
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
        return StoreError::Ok;
    }

private:
    System *system_;
};

class LegacyDrawerRepository final : public DrawerRepository
{
public:
    explicit LegacyDrawerRepository(System *system) : system_(system) {}

    StoreError Save(Transaction &, Drawer &drawer) override
    {
        if (system_ == nullptr)
            return StoreError::Io;

        // Mirrors Drawer::Save() (drawer.cc:348). Two cases rather than the
        // three a check has: there is no such thing as a drawer copy.
        if (drawer.archive != nullptr)
        {
            drawer.archive->changed = 1;
            return StoreError::Ok;
        }

        return (system_->SaveDrawer(&drawer) == 0) ? StoreError::Ok
                                                   : StoreError::Io;
    }

    StoreError Count(int &out) override
    {
        if (system_ == nullptr)
            return StoreError::Io;

        int total = 0;
        for (const Drawer *drawer = system_->DrawerList(); drawer != nullptr;
             drawer = drawer->next)
        {
            ++total;
        }
        out = total;
        return StoreError::Ok;
    }

private:
    System *system_;
};

class LegacyFileStore final : public Store
{
public:
    explicit LegacyFileStore(System *system)
        : system_(system), checks_(system), drawers_(system) {}

    [[nodiscard]] std::unique_ptr<Transaction> Begin() override
    {
        return std::make_unique<LegacyTransaction>();
    }

    [[nodiscard]] CheckRepository &Checks() override { return checks_; }

    [[nodiscard]] DrawerRepository &Drawers() override { return drawers_; }

    [[nodiscard]] bool SupportsAtomicWrites() const noexcept override
    {
        return false;   // see LegacyTransaction
    }

    [[nodiscard]] StoreError EndBusinessDay() override
    {
        // Nothing to do. There is no day container in the file layout: the
        // archive file that EndDay writes IS the day, and EndDay writes it
        // itself. Reporting Ok rather than Unsupported because the day did end
        // successfully -- this backend simply has no separate bookkeeping for
        // it, which is not a failure to report to a caller mid-EndDay.
        return StoreError::Ok;
    }

    [[nodiscard]] StoreError HealthCheck() override
    {
        return (system_ != nullptr) ? StoreError::Ok : StoreError::Io;
    }

    /*
     * Reload every check from its file and describe what came back.
     *
     * Deliberately not a walk of System::CheckList(). The in-memory objects
     * still carry fields the format never writes -- call_order above all -- so
     * snapshotting them would report this backend as preserving data it
     * silently drops. Going through Check::Load is what makes the dual-run
     * comparison a statement about persistence.
     */
    [[nodiscard]] StoreError Snapshot(StoreSnapshot &out) override
    {
        if (system_ == nullptr)
            return StoreError::Io;

        out = StoreSnapshot{};
        out.backend = Name();

        // Collect the filenames first. Check::Load builds a fresh object, and
        // holding the live list while doing file I/O is the pattern that caused
        // the autosave use-after-free.
        std::vector<std::string> files;
        for (const Check *check = system_->CheckList(); check != nullptr;
             check = check->next)
        {
            if (check->copy != 0 || check->archive != nullptr)
                continue;
            const char *name = check->filename.Value();
            if (name != nullptr && name[0] != '\0')
                files.emplace_back(name);
        }

        for (const std::string &file : files)
        {
            Check loaded;
            if (loaded.Load(&system_->settings, file.c_str()) != 0)
                return StoreError::Corrupt;
            out.checks.push_back(SnapshotOf(loaded));
        }

        std::sort(out.checks.begin(), out.checks.end(),
                  [](const CheckSnapshot &a, const CheckSnapshot &b) {
                      return a.serial_number < b.serial_number;
                  });
        return StoreError::Ok;
    }

    [[nodiscard]] const char *Name() const noexcept override
    {
        return "legacy-file";
    }

private:
    System *system_;
    LegacyCheckRepository checks_;
    LegacyDrawerRepository drawers_;
};

} // namespace

std::unique_ptr<Store> MakeLegacyFileStore(System *system)
{
    return std::make_unique<LegacyFileStore>(system);
}

} // namespace vt::store
