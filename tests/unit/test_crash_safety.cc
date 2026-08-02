/*
 * Crash safety: SIGKILL a writer mid-batch and see what each backend left.
 *
 * This is the experiment the whole migration rests on. Everything else in the
 * SQL work is an argument that atomicity across a group of writes is worth
 * having; this measures what its absence actually costs, on both backends, at
 * an identical and repeatable interruption point.
 *
 * A "batch" is N checks written inside one Store transaction. The victim
 * process (tests/crash/crash_writer.cc) SIGKILLs itself after saving exactly K
 * checks of a chosen batch, before that batch commits. Deterministic, not
 * raced: a test that has to win a race to prove its point is a flaky test
 * dressed up as evidence.
 *
 * The expected results differ, and both are asserted:
 *
 *   legacy  leaves the K checks it managed to write. One file per check, each
 *           individually atomic since the temp-file-plus-rename change, but
 *           nothing groups them -- so a killed EndDay leaves a torn day.
 *   sqlite  leaves nothing from the interrupted batch. The transaction never
 *           committed, so recovery discards it.
 *
 * The torn-batch assertion against the legacy backend is a test that documents
 * a defect rather than guarding a fix. It is expected to keep passing until
 * that backend is retired.
 */

#include <catch2/catch_all.hpp>

#include "main/business/check.hh"
#include "main/data/store/store.hh"
#include "main/data/settings.hh"
#include "main/data/system.hh"
#include "sql/database.hh"
#include "sql/statement.hh"
#include "support/vt_test_env.hh"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vt::store;

namespace {

constexpr int kSerialBase = 100000;   // must match crash_writer.cc

struct CrashDir
{
    fs::path dir;
    std::string db;

    explicit CrashDir(const std::string &name)
        : dir(fs::temp_directory_path() / name),
          db((fs::temp_directory_path() / (name + ".vtdb")).string())
    {
        Clean();
        std::error_code ec;
        fs::create_directories(dir, ec);
    }
    ~CrashDir() { Clean(); }

    void Clean() const
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
        for (const char *suffix : {"", "-wal", "-shm"})
            fs::remove(db + suffix, ec);
    }
};

struct RunResult
{
    // Batch numbers the writer reported as committed before it died. Every one
    // of these is a promise: it must have survived.
    std::vector<int> reported;
    int exit_signal{0};
};

// Run the writer to completion (it kills itself) and collect what it reported.
RunResult RunWriter(const std::vector<std::string> &args)
{
    int pipe_fds[2] = {-1, -1};
    REQUIRE(::pipe(pipe_fds) == 0);

    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(VT_CRASH_WRITER_PATH));
    for (const std::string &arg : args)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);

    const pid_t pid = ::fork();

    if (pid == 0)
    {
        // No Catch2 assertions past this point: the child records them into a
        // reporter whose output execv is about to discard, and a failure here
        // would be counted in a process that no longer exists.
        ::close(pipe_fds[0]);
        ::dup2(pipe_fds[1], STDOUT_FILENO);
        ::close(pipe_fds[1]);
        ::execv(VT_CRASH_WRITER_PATH, argv.data());
        ::_exit(127);
    }

    ::close(pipe_fds[1]);
    REQUIRE(pid > 0);

    RunResult result;
    std::string buffer;
    char chunk[4096];
    ssize_t got = 0;
    while ((got = ::read(pipe_fds[0], chunk, sizeof(chunk))) > 0)
        buffer.append(chunk, static_cast<std::size_t>(got));
    ::close(pipe_fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
        result.exit_signal = WTERMSIG(status);

    // Only complete lines count. A half-written line means the process died
    // mid-report, and the harness must not treat that batch as promised.
    std::size_t start = 0;
    while (true)
    {
        const std::size_t end = buffer.find('\n', start);
        if (end == std::string::npos)
            break;
        const std::string line = buffer.substr(start, end - start);
        start = end + 1;
        if (line.rfind("COMMITTED ", 0) == 0)
            result.reported.push_back(std::stoi(line.substr(10)));
    }
    return result;
}

// Serials that actually survived on disk, read back through the production
// reader rather than by parsing files here.
std::set<int> SurvivingSerialsLegacy(const fs::path &dir, Settings &settings)
{
    std::set<int> serials;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dir, ec))
    {
        const std::string name = entry.path().filename().string();
        if (name.rfind("check_", 0) != 0)
            continue;   // dot-prefixed temp files are skipped by design

        Check check;
        if (check.Load(&settings, entry.path().string().c_str()) != 0)
            continue;   // an unreadable file did not survive
        serials.insert(check.serial_number);
    }
    return serials;
}

std::set<int> SurvivingSerialsSqlite(const std::string &db_path)
{
    std::set<int> serials;
    vt::sql::Database db;
    REQUIRE(db.Open(db_path) == vt::sql::Status::Ok);

    vt::sql::Statement stmt;
    REQUIRE(stmt.Prepare(db, "SELECT serial_number FROM pos_check;")
            == vt::sql::Status::Ok);
    vt::sql::Status step = vt::sql::Status::Ok;
    while (stmt.Step(step))
        serials.insert(static_cast<int>(stmt.ColumnInt(0)));
    REQUIRE(step == vt::sql::Status::Ok);
    return serials;
}

// How many checks of `batch` survived, out of batch_size.
int SurvivorsInBatch(const std::set<int> &serials, int batch, int batch_size)
{
    int count = 0;
    for (int i = 0; i < batch_size; ++i)
    {
        if (serials.count(kSerialBase + batch * batch_size + i) > 0)
            ++count;
    }
    return count;
}

} // namespace

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "A committed batch survives being killed, on both backends",
                 "[crash]")
{
    // The floor both backends must clear. Anything the writer reported as
    // committed had already returned from Commit(), so losing one would mean
    // losing money that the till had confirmed.
    constexpr int kBatchSize = 6;
    constexpr int kKillBatch = 2;

    SECTION("legacy")
    {
        CrashDir crash("vt_crash_legacy_committed");
        const RunResult run = RunWriter(
            {"--backend", "legacy", "--path", crash.dir.string(),
             "--batch", std::to_string(kBatchSize),
             "--kill-in-batch", std::to_string(kKillBatch), "--kill-after", "3"});

        REQUIRE(run.exit_signal == SIGKILL);
        REQUIRE(run.reported.size() == kKillBatch);   // batches 0 and 1

        const std::set<int> survivors =
            SurvivingSerialsLegacy(crash.dir, MasterSystem->settings);
        for (int batch : run.reported)
            REQUIRE(SurvivorsInBatch(survivors, batch, kBatchSize) == kBatchSize);
    }

    SECTION("sqlite")
    {
        CrashDir crash("vt_crash_sqlite_committed");
        const RunResult run = RunWriter(
            {"--backend", "sqlite", "--path", crash.db,
             "--batch", std::to_string(kBatchSize),
             "--kill-in-batch", std::to_string(kKillBatch), "--kill-after", "3"});

        REQUIRE(run.exit_signal == SIGKILL);
        REQUIRE(run.reported.size() == kKillBatch);

        const std::set<int> survivors = SurvivingSerialsSqlite(crash.db);
        for (int batch : run.reported)
            REQUIRE(SurvivorsInBatch(survivors, batch, kBatchSize) == kBatchSize);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "An interrupted batch tears on files and does not on SQL",
                 "[crash][atomicity]")
{
    // The measurement the migration exists to produce. Identical interruption,
    // same point, two backends.
    constexpr int kBatchSize = 6;
    constexpr int kKillBatch = 1;
    constexpr int kWrittenBeforeKill = 4;

    SECTION("legacy leaves a torn batch")
    {
        // Documents the defect rather than guarding a fix, and is expected to
        // keep passing until this backend is retired. Each individual file is
        // atomic since the temp-file-plus-rename change, so the four checks
        // that got written are intact -- but nothing groups them, so the batch
        // as a whole is half applied. On a real EndDay that is a day's archive
        // written for some checks and not others.
        CrashDir crash("vt_crash_legacy_torn");
        const RunResult run = RunWriter(
            {"--backend", "legacy", "--path", crash.dir.string(),
             "--batch", std::to_string(kBatchSize),
             "--kill-in-batch", std::to_string(kKillBatch),
             "--kill-after", std::to_string(kWrittenBeforeKill)});

        REQUIRE(run.exit_signal == SIGKILL);

        const std::set<int> survivors =
            SurvivingSerialsLegacy(crash.dir, MasterSystem->settings);
        const int partial = SurvivorsInBatch(survivors, kKillBatch, kBatchSize);

        INFO("legacy kept " << partial << " of " << kBatchSize
                            << " checks from the interrupted batch");
        REQUIRE(partial == kWrittenBeforeKill);
        REQUIRE(partial > 0);
        REQUIRE(partial < kBatchSize);
    }

    SECTION("sqlite leaves nothing from the interrupted batch")
    {
        // The guarantee being bought. The transaction never committed, so
        // recovery discards every row it wrote -- all four of them, at the same
        // interruption point that tore the file backend.
        CrashDir crash("vt_crash_sqlite_atomic");
        const RunResult run = RunWriter(
            {"--backend", "sqlite", "--path", crash.db,
             "--batch", std::to_string(kBatchSize),
             "--kill-in-batch", std::to_string(kKillBatch),
             "--kill-after", std::to_string(kWrittenBeforeKill)});

        REQUIRE(run.exit_signal == SIGKILL);

        const std::set<int> survivors = SurvivingSerialsSqlite(crash.db);
        REQUIRE(SurvivorsInBatch(survivors, kKillBatch, kBatchSize) == 0);

        // And the batch before it is fully intact, so this is atomicity rather
        // than the database having simply lost everything.
        REQUIRE(SurvivorsInBatch(survivors, 0, kBatchSize) == kBatchSize);
    }
}

TEST_CASE_METHOD(vt_test::VtSystemFixture,
                 "A killed database reopens clean and keeps its schema",
                 "[crash][recovery]")
{
    CrashDir crash("vt_crash_recovery");
    constexpr int kBatchSize = 5;

    const RunResult run = RunWriter(
        {"--backend", "sqlite", "--path", crash.db,
         "--batch", std::to_string(kBatchSize),
         "--kill-in-batch", "3", "--kill-after", "2"});
    REQUIRE(run.exit_signal == SIGKILL);

    // Reopening runs the migration check, so a database left mid-write that
    // could not be recovered would fail here rather than at the first save
    // during a service.
    StoreError error = StoreError::Io;
    auto store = MakeSqliteStore(crash.db, error);
    REQUIRE(error == StoreError::None);
    REQUIRE(store != nullptr);
    REQUIRE(store->HealthCheck() == StoreError::None);

    // integrity_check, not the cheap quick_check the health probe uses. This is
    // the thorough one -- too slow to run at startup, which is exactly why it
    // belongs in a crash test.
    {
        vt::sql::Database db;
        REQUIRE(db.Open(crash.db) == vt::sql::Status::Ok);
        vt::sql::Statement stmt;
        REQUIRE(stmt.Prepare(db, "PRAGMA integrity_check;") == vt::sql::Status::Ok);
        vt::sql::Status step = vt::sql::Status::Ok;
        REQUIRE(stmt.Step(step));
        REQUIRE(stmt.ColumnText(0) == "ok");
    }

    // Writing after recovery works, which is the thing an operator actually
    // needs the morning after a power cut.
    auto tx = store->Begin();
    REQUIRE(tx != nullptr);
    Check check;
    check.serial_number = 999999;
    check.NewSubCheck();
    REQUIRE(store->Checks().Save(*tx, check) == StoreError::None);
    REQUIRE(tx->Commit() == StoreError::None);

    const std::set<int> survivors = SurvivingSerialsSqlite(crash.db);
    REQUIRE(survivors.count(999999) == 1);
}
