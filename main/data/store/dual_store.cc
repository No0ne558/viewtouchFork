/*
 * dual_store.cc - See dual_store.hh. The governing rule throughout: the shadow
 * is never allowed to fail a save.
 */

#include "dual_store.hh"

#include "check.hh"
#include "drawer.hh"
#include "vt_logger.hh"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace vt::store {

namespace {

/*
 * A transaction spanning both backends.
 *
 * Commit does the primary first. If the primary fails there is nothing to
 * shadow, so the shadow is rolled back rather than committed -- otherwise the
 * shadow would accumulate writes the authoritative side rejected and every
 * subsequent comparison would be noise.
 *
 * If the primary succeeds and the shadow fails, the primary's success stands.
 * The shadow is now behind, which Compare() will report, and that is the
 * correct outcome: the run exists to find exactly this.
 */
class DualTransaction final : public Transaction
{
public:
    DualTransaction(std::unique_ptr<Transaction> primary,
                    std::unique_ptr<Transaction> shadow, ShadowHealth &health)
        : primary_(std::move(primary)), shadow_(std::move(shadow)),
          health_(health) {}

    StoreError Commit() override
    {
        const StoreError result =
            (primary_ != nullptr) ? primary_->Commit() : StoreError::Io;

        if (shadow_ != nullptr)
        {
            if (result != StoreError::Ok)
            {
                shadow_->Rollback();
            }
            else if (const StoreError shadow_result = shadow_->Commit();
                     shadow_result != StoreError::Ok)
            {
                ++health_.save_failures;
                health_.last_error = StoreErrorName(shadow_result);
                ::vt::Logger::error("dual run: shadow commit failed ({})",
                                    StoreErrorName(shadow_result));
            }
        }
        return result;
    }

    void Rollback() noexcept override
    {
        if (primary_ != nullptr)
            primary_->Rollback();
        if (shadow_ != nullptr)
            shadow_->Rollback();
    }

    [[nodiscard]] bool IsActive() const noexcept override
    {
        return primary_ != nullptr && primary_->IsActive();
    }

    [[nodiscard]] Transaction *primary() const noexcept { return primary_.get(); }
    [[nodiscard]] Transaction *shadow() const noexcept { return shadow_.get(); }

private:
    std::unique_ptr<Transaction> primary_;
    std::unique_ptr<Transaction> shadow_;
    ShadowHealth &health_;
};

class DualCheckRepository final : public CheckRepository
{
public:
    DualCheckRepository(Store &primary, Store &shadow, ShadowHealth &health)
        : primary_(primary), shadow_(shadow), health_(health) {}

    StoreError Save(Transaction &tx, Check &check) override
    {
        auto *dual = dynamic_cast<DualTransaction *>(&tx);
        if (dual == nullptr || dual->primary() == nullptr)
            return StoreError::Io;

        // Primary first, and its answer is the one returned. The shadow runs
        // afterwards precisely so a shadow failure cannot influence it.
        const StoreError result = primary_.Checks().Save(*dual->primary(), check);
        ++health_.saves;

        if (dual->shadow() != nullptr)
        {
            const StoreError shadow_result =
                shadow_.Checks().Save(*dual->shadow(), check);
            // Unsupported is not a failure. The SQL backend reports it for an
            // archived check, whose routing needs the importer's archive-to-day
            // mapping -- a known gap, not a divergence to chase.
            if (shadow_result != StoreError::Ok &&
                shadow_result != StoreError::Unsupported)
            {
                ++health_.save_failures;
                health_.last_error = StoreErrorName(shadow_result);
                ::vt::Logger::error(
                    "dual run: shadow save of check #{} failed ({})",
                    check.serial_number, StoreErrorName(shadow_result));
            }
        }
        return result;
    }

    StoreError Remove(Transaction &tx, Check &check) override
    {
        auto *dual = dynamic_cast<DualTransaction *>(&tx);
        if (dual == nullptr || dual->primary() == nullptr)
            return StoreError::Io;

        // Shadow FIRST here, unlike Save. CheckRepository::Remove destroys the
        // check on success, so running the primary first would hand the shadow
        // a dangling reference. The ownership note on the interface is not a
        // formality -- this is the call site it exists for.
        if (dual->shadow() != nullptr)
        {
            const StoreError shadow_result =
                shadow_.Checks().Remove(*dual->shadow(), check);
            ++health_.removes;
            if (shadow_result != StoreError::Ok &&
                shadow_result != StoreError::Unsupported)
            {
                ++health_.remove_failures;
                health_.last_error = StoreErrorName(shadow_result);
                ::vt::Logger::error(
                    "dual run: shadow remove of check #{} failed ({})",
                    check.serial_number, StoreErrorName(shadow_result));
            }
        }

        return primary_.Checks().Remove(*dual->primary(), check);
    }

    StoreError Count(int &out) override { return primary_.Checks().Count(out); }

private:
    Store &primary_;
    Store &shadow_;
    ShadowHealth &health_;
};

/*
 * Drawers, same posture as checks: primary first and authoritative, shadow
 * afterwards so it cannot influence the answer.
 *
 * No Remove here because DrawerRepository has none -- the legacy code never
 * deletes a drawer through a System method.
 */
class DualDrawerRepository final : public DrawerRepository
{
public:
    DualDrawerRepository(Store &primary, Store &shadow, ShadowHealth &health)
        : primary_(primary), shadow_(shadow), health_(health) {}

    StoreError Save(Transaction &tx, Drawer &drawer) override
    {
        auto *dual = dynamic_cast<DualTransaction *>(&tx);
        if (dual == nullptr || dual->primary() == nullptr)
            return StoreError::Io;

        const StoreError result = primary_.Drawers().Save(*dual->primary(), drawer);
        ++health_.saves;

        if (dual->shadow() != nullptr)
        {
            const StoreError shadow_result =
                shadow_.Drawers().Save(*dual->shadow(), drawer);
            if (shadow_result != StoreError::Ok &&
                shadow_result != StoreError::Unsupported)
            {
                ++health_.save_failures;
                health_.last_error = StoreErrorName(shadow_result);
                ::vt::Logger::error(
                    "dual run: shadow save of drawer #{} failed ({})",
                    drawer.serial_number, StoreErrorName(shadow_result));
            }
        }
        return result;
    }

    StoreError Count(int &out) override { return primary_.Drawers().Count(out); }

private:
    Store &primary_;
    Store &shadow_;
    ShadowHealth &health_;
};

} // namespace

struct DualRunStore::Impl
{
    std::unique_ptr<Store> primary;
    std::unique_ptr<Store> shadow;
    ShadowHealth health;
    std::unique_ptr<DualCheckRepository> checks;
    std::unique_ptr<DualDrawerRepository> drawers;
    std::string name;
};

DualRunStore::DualRunStore(std::unique_ptr<Store> primary,
                           std::unique_ptr<Store> shadow)
    : impl_(std::make_unique<Impl>())
{
    impl_->primary = std::move(primary);
    impl_->shadow = std::move(shadow);
    impl_->checks = std::make_unique<DualCheckRepository>(
        *impl_->primary, *impl_->shadow, impl_->health);
    impl_->drawers = std::make_unique<DualDrawerRepository>(
        *impl_->primary, *impl_->shadow, impl_->health);
    impl_->name = std::string("dual(") + impl_->primary->Name() + " + " +
                  impl_->shadow->Name() + ")";
}

DualRunStore::~DualRunStore() = default;

std::unique_ptr<Transaction> DualRunStore::Begin()
{
    auto primary = impl_->primary->Begin();
    if (primary == nullptr)
        return nullptr;   // no primary transaction means no write at all

    // A shadow that cannot begin leaves the run one-sided rather than stopping
    // it. Compare() will show the gap.
    auto shadow = impl_->shadow->Begin();
    if (shadow == nullptr)
    {
        ++impl_->health.save_failures;
        impl_->health.last_error = "shadow could not begin a transaction";
        ::vt::Logger::error("dual run: shadow could not begin a transaction");
    }

    return std::make_unique<DualTransaction>(std::move(primary),
                                             std::move(shadow), impl_->health);
}

CheckRepository &DualRunStore::Checks() { return *impl_->checks; }

DrawerRepository &DualRunStore::Drawers() { return *impl_->drawers; }

bool DualRunStore::SupportsAtomicWrites() const noexcept
{
    return impl_->primary->SupportsAtomicWrites();
}

StoreError DualRunStore::HealthCheck()
{
    if (const StoreError e = impl_->primary->HealthCheck(); e != StoreError::Ok)
        return e;
    return impl_->shadow->HealthCheck();
}

StoreError DualRunStore::Snapshot(StoreSnapshot &out)
{
    return impl_->primary->Snapshot(out);
}

const char *DualRunStore::Name() const noexcept { return impl_->name.c_str(); }

StoreError DualRunStore::Compare(std::vector<Divergence> &out)
{
    StoreSnapshot primary;
    if (const StoreError e = impl_->primary->Snapshot(primary);
        e != StoreError::Ok)
    {
        return e;
    }

    StoreSnapshot shadow;
    if (const StoreError e = impl_->shadow->Snapshot(shadow);
        e != StoreError::Ok)
    {
        return e;
    }

    out = Diff(primary, shadow);
    return StoreError::Ok;
}

StoreError DualRunStore::CompareAndDescribe(std::string &report)
{
    StoreSnapshot primary;
    if (const StoreError e = impl_->primary->Snapshot(primary);
        e != StoreError::Ok)
    {
        return e;
    }

    StoreSnapshot shadow;
    if (const StoreError e = impl_->shadow->Snapshot(shadow);
        e != StoreError::Ok)
    {
        return e;
    }

    report = DescribeDivergence(primary, shadow, Diff(primary, shadow));
    return StoreError::Ok;
}

const ShadowHealth &DualRunStore::Shadow() const noexcept { return impl_->health; }

Store &DualRunStore::Primary() noexcept { return *impl_->primary; }

Store &DualRunStore::ShadowStore() noexcept { return *impl_->shadow; }

std::unique_ptr<DualRunStore> MakeDualRunStore(std::unique_ptr<Store> primary,
                                               std::unique_ptr<Store> shadow)
{
    if (primary == nullptr || shadow == nullptr)
        return nullptr;
    return std::make_unique<DualRunStore>(std::move(primary), std::move(shadow));
}

} // namespace vt::store
