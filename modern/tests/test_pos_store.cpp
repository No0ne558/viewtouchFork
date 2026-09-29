#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/async_writer.hh"
#include "storage/pos_store.hh"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::storage::AsyncWriter;
using vt::storage::PosStore;
using vt::storage::SqlPosSink;

TEST_CASE("PosStore seeds, then loads settings, menu, employees", "[posstore]")
{
    QTemporaryDir dir;
    const auto seed = test::seedPosData();
    PosStore store(dir.filePath(u"vt.db"_s));
    QString error;
    REQUIRE(store.open(&error));
    CHECK_FALSE(store.hasMenu());
    REQUIRE(store.seed(seed.settings, seed.menu, seed.employees, &error));
    CHECK(store.hasMenu());

    const auto loaded = store.load();
    REQUIRE(loaded);
    CHECK(loaded->settings == seed.settings);
    CHECK(loaded->menu == seed.menu);
    CHECK(loaded->employees.size() == seed.employees.size());
    CHECK(loaded->lastCheckId == 0);

    // The PIN still works after a round trip (hash + salt persisted).
    app::PosService pos(*loaded, nullptr);
    CHECK(pos.loginWithPin(u"1234"_s));
}

TEST_CASE("Service writes through the background writer and reloads", "[posstore]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    const auto seed = test::seedPosData();
    {
        PosStore store(path);
        REQUIRE(store.open());
        REQUIRE(store.seed(seed.settings, seed.menu, seed.employees));
    }

    qint64 closedId = 0;
    {
        PosStore store(path);
        REQUIRE(store.open());
        AsyncWriter writer(path);
        SqlPosSink sink(writer);
        app::PosService pos(*store.load(), &sink);
        REQUIRE(pos.loginWithPin(u"1111"_s));
        pos.pinKey(u"1"_s);   // PIN for clock in is the logged-in user; harmless
        REQUIRE(pos.clockIn());

        pos.selectTable(u"T2"_s);
        pos.startCheck(core::CheckType::DineIn);
        pos.addItem(u"caesar"_s);
        pos.sendOrder();

        pos.releaseCheck();
        pos.startCheck(core::CheckType::Takeout);
        pos.addItem(u"soda"_s);
        closedId = pos.checkInfo().value(u"id"_s).toLongLong();
        REQUIRE(pos.tender(u"cash"_s));
        REQUIRE(pos.closeCheck());
        // writer destructor flushes
    }

    PosStore store(path);
    REQUIRE(store.open());
    const auto data = store.load();
    REQUIRE(data);
    REQUIRE(data->openChecks.size() == 1);
    CHECK(data->openChecks[0].label == "T2");
    CHECK(data->openChecks[0].lines[0].sent);
    CHECK(data->lastCheckId == 2);
    REQUIRE(data->openPunches.size() == 1);

    const auto closed = store.checks(core::CheckStatus::Closed);
    REQUIRE(closed.size() == 1);
    CHECK(closed[0].id == closedId);
    CHECK(closed[0].payments.size() == 1);
}

TEST_CASE("AsyncWriter coalesces repeated writes of one row", "[posstore]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    PosStore store(path);
    REQUIRE(store.open());
    AsyncWriter writer(path);

    core::TimePunch p{1, "sam", 1000, 0};
    for (int i = 0; i < 500; ++i) {
        p.clockOut = 2000 + i;
        writer.upsert(u"time_punches"_s, u"1"_s,
                      {{u"id"_s, 1}, {u"employee_id"_s, u"sam"_s}, {u"clock_in"_s, 1000}, {u"clock_out"_s, qint64(p.clockOut)}});
    }
    CHECK(writer.pending() <= 1);   // never more than one queued version of the row
    writer.flush();
    CHECK(writer.pending() == 0);
    const auto punches = store.punches();
    REQUIRE(punches.size() == 1);
    CHECK(punches[0].clockOut == 2499);
}

TEST_CASE("AsyncWriter reports failures and keeps the row queued", "[posstore]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    PosStore store(path);
    REQUIRE(store.open());
    AsyncWriter writer(path);
    QSignalSpy failed(&writer, &AsyncWriter::writeFailed);

    writer.upsert(u"no_such_table"_s, u"1"_s, {{u"id"_s, 1}});
    writer.flush();
    QCoreApplication::processEvents();   // deliver the queued signal
    REQUIRE(failed.size() >= 1);
    CHECK(failed.first()[0].toString().contains(u"no_such_table"_s));
    CHECK(writer.pending() == 1);
}

TEST_CASE("Queuing writes never blocks the caller", "[posstore]")
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"vt.db"_s);
    PosStore store(path);
    REQUIRE(store.open());
    AsyncWriter writer(path);

    // 2000 distinct rows: the caller only queues; the worker does the I/O.
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 2000; ++i) {
        writer.upsert(u"time_punches"_s, QString::number(i),
                      {{u"id"_s, i}, {u"employee_id"_s, u"sam"_s}, {u"clock_in"_s, i}, {u"clock_out"_s, 0}});
    }
    const qint64 queueMs = timer.elapsed();
    CHECK(queueMs < 200);
    writer.flush();
    CHECK(store.punches().size() == 2000);
}
