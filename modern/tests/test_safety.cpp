#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "storage/async_writer.hh"
#include "storage/backup.hh"
#include "storage/layout_store.hh"
#include "storage/pos_store.hh"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;
using vt::storage::AsyncWriter;
using vt::storage::PosStore;
using vt::storage::SqlPosSink;

// Safety under real use: power cuts, a damaged database, a second backup
// copy, and a long busy service on several terminals.

namespace {

QString seeded(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(u"vt.db"_s);
    const auto seed = test::seedPosData();
    storage::LayoutStore pages(path);   // a real store has its pages too
    REQUIRE(pages.open());
    PosStore store(path);
    REQUIRE(store.open());
    auto staff = seed.employees;
    core::Employee riley = staff.back();   // a fourth person, for four terminals
    riley.id = "riley";
    riley.name = "Riley";
    riley.pinSalt = "riley-salt";
    riley.pinHash = app::hashPin(u"4444"_s, riley.pinSalt);
    staff.push_back(riley);
    REQUIRE(store.seed(seed.settings, seed.menu, staff));
    return path;
}

// What a power cut leaves: the files as they are on disk right now, with
// nothing closed or checkpointed.
QString pullThePlug(const QString &db, const QString &to)
{
    QDir().mkpath(to);
    const QString copy = QDir(to).filePath(u"vt.db"_s);
    for (const QString &suffix : {u""_s, u"-wal"_s}) {
        if (QFile::exists(db + suffix))
            REQUIRE(QFile::copy(db + suffix, copy + suffix));
    }
    return copy;
}

} // namespace

TEST_CASE("Power cut mid-service: committed sales and the open check survive", "[safety]")
{
    QTemporaryDir dir;
    const QString path = seeded(dir);
    PosStore store(path);
    REQUIRE(store.open());
    AsyncWriter writer(path);
    SqlPosSink sink(writer);
    PosService pos(*store.load(), &sink);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());

    // One check closed, one half done: items sent and partly paid.
    REQUIRE(pos.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"cobb"_s);
    REQUIRE(pos.tender(u"cash"_s));
    REQUIRE(pos.closeCheck());
    REQUIRE(pos.selectTable(u"T2"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"caesar"_s);
    pos.addItem(u"coffee"_s);
    REQUIRE(pos.sendOrder());
    pos.entryKey(u"500"_s);
    REQUIRE(pos.tender(u"cash"_s));
    writer.flush();                                        // committed...

    QTemporaryDir after;
    const QString copy = pullThePlug(path, after.path()); // ...then the power goes
    QString problem;
    REQUIRE(storage::databaseIntact(copy, &problem));
    PosStore back(copy);
    REQUIRE(back.open());
    const auto data = back.load();
    REQUIRE(data);
    REQUIRE(data->closedToday.size() == 1);
    REQUIRE(data->openChecks.size() == 1);
    const core::Check &open = data->openChecks.front();
    CHECK(open.label == "T2");
    REQUIRE(open.lines.size() == 2);
    CHECK(open.lines[0].sent);
    REQUIRE(open.payments.size() == 1);
    CHECK(open.payments[0].amount == Money::fromCents(500));
    REQUIRE(data->drawers.size() == 1);
    CHECK(data->drawers[0].open());

    // And the store carries on from there.
    PosService again(*data, nullptr);
    REQUIRE(again.loginWithPin(u"1234"_s));
    REQUIRE(again.openCheck(open.id));
    REQUIRE(again.tender(u"cash"_s));
    REQUIRE(again.closeCheck());
}

TEST_CASE("A damaged database is caught at start", "[safety][backup]")
{
    QTemporaryDir dir;
    const QString path = seeded(dir);
    QString problem;
    REQUIRE(storage::databaseIntact(path, &problem));

    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadWrite));
    const qint64 size = f.size();
    for (qint64 at = 4096 + 100; at < size; at += 4096) {   // scribble on every page but the first
        f.seek(at);
        f.write(QByteArray(400, '\x5a'));
    }
    f.close();
    CHECK_FALSE(storage::databaseIntact(path, &problem));
    CHECK_FALSE(problem.isEmpty());

    QFile junk(dir.filePath(u"junk.db"_s));
    REQUIRE(junk.open(QIODevice::WriteOnly));
    junk.write("this is not a database, just some text that is long enough to look like one");
    junk.close();
    CHECK_FALSE(storage::databaseIntact(junk.fileName(), &problem));
}

TEST_CASE("Backups: a second checked copy elsewhere; a missing drive is reported", "[safety][backup]")
{
    QTemporaryDir dir;
    const QString path = seeded(dir);
    const QString backups = dir.filePath(u"backups"_s);
    const QString usb = dir.filePath(u"usb/viewtouch"_s);

    storage::BackupScheduler scheduler(path, backups, 2, 0);
    scheduler.setCopyDirectory(usb);
    QSignalSpy done(&scheduler, &storage::BackupScheduler::finished);
    for (int i = 0; i < 3; ++i) {
        scheduler.backupNow();
        REQUIRE(done.wait(10000));
        INFO(done.last()[2].toString().toStdString());
        REQUIRE(done.last()[0].toBool());
        CHECK(done.last()[4].toBool());
        CHECK(done.last()[3].toString().contains(usb));
    }
    // Both places keep the newest two, and the copies are good databases.
    CHECK(storage::listBackups(backups).size() == 2);
    const QStringList copies = storage::listBackups(usb);
    REQUIRE(copies.size() == 2);
    CHECK(storage::verifyDatabase(copies.first()));
    CHECK(QFileInfo(copies.first()).fileName() == QFileInfo(storage::listBackups(backups).first()).fileName());

    // The drive is gone (a file where the folder should be): the backup
    // itself still works, the copy is reported.
    QFile blocker(dir.filePath(u"gone"_s));
    REQUIRE(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    scheduler.setCopyDirectory(dir.filePath(u"gone/viewtouch"_s));
    scheduler.backupNow();
    REQUIRE(done.wait(10000));
    CHECK(done.last()[0].toBool());
    CHECK_FALSE(done.last()[4].toBool());
    CHECK(done.last()[3].toString().startsWith(u"Second copy failed"_s));
}

TEST_CASE("Back Up Now: managers only, and the status shows on End of Day", "[safety][backup]")
{
    PosService pos(test::seedPosData(), nullptr);
    int asked = 0;
    pos.shared()->requestBackup = [&] { ++asked; return true; };
    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK_FALSE(pos.backupNow());
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1234"_s));
    REQUIRE(pos.backupNow());
    CHECK(asked == 1);
    pos.shared()->setBackupStatus({{u"at"_s, u"6:02 PM"_s}, {u"ok"_s, true}, {u"copy"_s, u"Copied to /media/usb."_s},
                                   {u"copyOk"_s, true}});
    CHECK(pos.dayInfo()[u"backup"_s].toMap()[u"at"_s] == u"6:02 PM"_s);

    pos.shared()->requestBackup = nullptr;                 // a store without backups
    CHECK_FALSE(pos.backupNow());
}

TEST_CASE("A long busy service on four terminals: quick, and nothing lost", "[safety][stress]")
{
    QTemporaryDir dir;
    const QString path = seeded(dir);
    PosStore store(path);
    REQUIRE(store.open());
    AsyncWriter writer(path);
    SqlPosSink sink(writer);
    app::PosShared shared(*store.load(), &sink);
    shared.settings.cashMode = core::CashMode::ServerBank;

    std::vector<std::unique_ptr<PosService>> terminals;
    for (int t = 0; t < 4; ++t)
        terminals.push_back(std::make_unique<PosService>(&shared, u"T%1"_s.arg(t)));
    const QStringList pins{u"1111"_s, u"2222"_s, u"4444"_s, u"1234"_s};
    for (int t = 0; t < 4; ++t) {
        REQUIRE(terminals[t]->loginWithPin(pins[t]));
        terminals[t]->entryKey(u"10000"_s);
        REQUIRE(terminals[t]->openDrawerSession());
    }

    const char *items[] = {"coffee", "cobb", "caesar", "pancakes", "water", "kids-burger", "soda"};
    std::vector<qint64> us;   // how long each step held the screen
    QElapsedTimer step;
    const auto timed = [&](auto f) {
        step.start();
        const bool ok = f();
        us.push_back(step.nsecsElapsed() / 1000);
        return ok;
    };
    constexpr int perTerminal = 250;   // 1000 checks, ~5000 steps
    Money expected;
    for (int n = 0; n < perTerminal; ++n) {
        for (int t = 0; t < 4; ++t) {
            PosService &pos = *terminals[t];
            REQUIRE(timed([&] { return pos.startCheck(core::CheckType::Takeout); }));
            for (int i = 0; i < 3; ++i)
                REQUIRE(timed([&] { return pos.addItem(QString::fromLatin1(items[(n + t + i) % 7])); }));
            REQUIRE(timed([&] { return pos.sendOrder(); }));
            REQUIRE(timed([&] { return pos.tender(u"cash"_s); }));
            REQUIRE(timed([&] { return pos.closeCheck(); }));
        }
    }
    for (const core::Check &c : shared.closedToday)
        expected += c.totals(shared.settings.tax).total;

    std::ranges::sort(us);
    const qint64 p99 = us[us.size() * 99 / 100];
    const qint64 worst = us.back();
    INFO("steps " << us.size() << ", p99 " << p99 << " us, worst " << worst << " us");
    CHECK(p99 < 20'000);     // the screen answers right away...
    CHECK(worst < 250'000);  // ...and never stalls

    // Everything reached the disk.
    QElapsedTimer drain;
    drain.start();
    writer.flush();
    INFO("writer drained in " << drain.elapsed() << " ms");
    CHECK(drain.elapsed() < 30'000);
    PosStore back(path);
    REQUIRE(back.open());
    const auto data = back.load();
    REQUIRE(data);
    REQUIRE(data->closedToday.size() == 4 * perTerminal);
    Money reloaded;
    for (const core::Check &c : data->closedToday)
        reloaded += c.totals(data->settings.tax).total;
    CHECK(reloaded == expected);
    CHECK(data->openChecks.empty());
}
