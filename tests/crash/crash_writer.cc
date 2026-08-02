/*
 * crash_writer.cc - A process that writes checks and can be killed mid-write.
 *
 * The crash-safety harness needs a victim it can SIGKILL, which a test cannot
 * do to itself, so this is a separate executable driven by tests/unit/
 * test_crash_safety.cc.
 *
 * It writes batches: N checks inside one Store transaction, then commit, then
 * report the batch on stdout and flush. A batch is the unit the harness reasons
 * about, because "all of these or none of them" is exactly the guarantee the
 * two backends differ on.
 *
 * --kill-after makes the crash DETERMINISTIC rather than raced. Killing on a
 * timer would demonstrate the difference only sometimes, and a test that has to
 * win a race to prove its point is a flaky test dressed up as evidence. With
 * --kill-in-batch B --kill-after K the process SIGKILLs itself after saving
 * exactly K checks of batch B, before that batch commits, so both backends face
 * an identical, repeatable interruption.
 *
 * SIGKILL specifically, and self-inflicted: no atexit handlers, no destructors,
 * no buffered writes flushed on the way out. That is the closest a test can get
 * to a power cut without one.
 */

#include "main/business/check.hh"
#include "main/business/sales.hh"
#include "main/data/store/store.hh"
#include "main/data/system.hh"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unistd.h>

namespace {

// Serials start here so a batch index is recoverable from any serial: batch
// b holds serials [kSerialBase + b*N, kSerialBase + b*N + N).
constexpr int kSerialBase = 100000;

Check *BuildCheck(int serial)
{
    auto *check = new Check;
    check->serial_number = serial;
    check->type = CHECK_TAKEOUT;
    check->guests = 2;

    SubCheck *sub = check->NewSubCheck();

    auto *burger = new Order("Burger", 950);
    burger->item_family = FAMILY_BURGERS;
    sub->Add(burger);

    auto *payment = new Payment(TENDER_CASH, 0, 0, 950);
    sub->Add(payment);

    return check;
}

[[noreturn]] void Usage()
{
    std::fprintf(stderr,
                 "usage: vt_crash_writer --backend legacy|sqlite --path P\n"
                 "                       [--batch N] [--batches M]\n"
                 "                       [--kill-in-batch B --kill-after K]\n");
    std::exit(2);
}

} // namespace

int main(int argc, char **argv)
{
    std::string backend;
    std::string path;
    int batch_size = 8;
    int batch_limit = -1;      // -1 = until killed
    int kill_in_batch = -1;
    int kill_after = -1;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc)
                Usage();
            return argv[++i];
        };

        if (arg == "--backend")            backend = next();
        else if (arg == "--path")          path = next();
        else if (arg == "--batch")         batch_size = std::stoi(next());
        else if (arg == "--batches")       batch_limit = std::stoi(next());
        else if (arg == "--kill-in-batch") kill_in_batch = std::stoi(next());
        else if (arg == "--kill-after")    kill_after = std::stoi(next());
        else Usage();
    }

    if (path.empty() || (backend != "legacy" && backend != "sqlite"))
        Usage();

    MasterSystem = std::make_unique<System>();

    std::unique_ptr<vt::store::Store> store;
    if (backend == "legacy")
    {
        MasterSystem->current_path.Set(path.c_str());
        store = vt::store::MakeLegacyFileStore(MasterSystem.get());
    }
    else
    {
        vt::store::StoreError error = vt::store::StoreError::None;
        store = vt::store::MakeSqliteStore(path, error);
        if (store == nullptr)
        {
            std::fprintf(stderr, "cannot open %s: %s\n", path.c_str(),
                         vt::store::StoreErrorName(error));
            return 1;
        }
    }

    for (int batch = 0; batch_limit < 0 || batch < batch_limit; ++batch)
    {
        auto tx = store->Begin();
        if (tx == nullptr)
        {
            std::fprintf(stderr, "cannot begin a transaction\n");
            return 1;
        }

        for (int i = 0; i < batch_size; ++i)
        {
            if (batch == kill_in_batch && i == kill_after)
            {
                // Before this batch commits, and with no chance to clean up.
                // Both backends see the same interruption at the same point;
                // what they leave behind is the whole question.
                std::fflush(nullptr);
                std::raise(SIGKILL);
            }

            const int serial = kSerialBase + batch * batch_size + i;
            std::unique_ptr<Check> check(BuildCheck(serial));
            if (store->Checks().Save(*tx, *check) != vt::store::StoreError::None)
            {
                std::fprintf(stderr, "save of check %d failed\n", serial);
                return 1;
            }
        }

        if (tx->Commit() != vt::store::StoreError::None)
        {
            std::fprintf(stderr, "commit of batch %d failed\n", batch);
            return 1;
        }

        // Reported only after the commit returns, and flushed immediately. The
        // harness treats every line it actually reads as a promise that must
        // have survived; a batch committed but not yet reported is allowed to
        // survive too, which is why the invariant is one-directional.
        std::printf("COMMITTED %d\n", batch);
        std::fflush(stdout);
    }

    return 0;
}
