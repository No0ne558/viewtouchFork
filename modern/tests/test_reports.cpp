#include <catch2/catch_test_macros.hpp>

#include "core/report.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"
#include "reportexport.hh"

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Sales by hour and category, the audit trail, and saving reports.

namespace {

std::int64_t todayAt(int hour, int minute = 0)
{
    return QDateTime(QDate::currentDate(), QTime(hour, minute)).toMSecsSinceEpoch();
}

void sell(PosService &pos, const char *item, const QString &table)
{
    REQUIRE(pos.selectTable(table) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(QString::fromLatin1(item));
    pos.finishChoosing();
    REQUIRE(pos.tender(u"credit"_s));
    REQUIRE(pos.closeCheck());
}

QStringList cellsOf(const QVariantMap &report, const QString &first)
{
    for (const QVariant &v : report[u"rows"_s].toList()) {
        const QStringList cells = v.toMap()[u"cells"_s].toStringList();
        if (!cells.isEmpty() && cells.first() == first)
            return cells;
    }
    return {};
}

} // namespace

TEST_CASE("Sales by hour and by category", "[reports]")
{
    PosService pos(test::seedPosData(), nullptr);
    std::int64_t clock = todayAt(11, 30);
    pos.shared()->setClock([&] { return clock; });
    REQUIRE(pos.loginWithPin(u"1111"_s));
    sell(pos, "coffee", u"T1"_s);                          // $2.75 at 11:30
    clock = todayAt(12, 15);
    sell(pos, "cobb", u"T2"_s);                            // $12.50 at 12:15
    sell(pos, "tea", u"T3"_s);                             // $2.50 at 12:15
    pos.logout();
    REQUIRE(pos.loginWithPin(u"1234"_s));

    const QVariantMap hourly = pos.report(u"hourly"_s);
    CHECK(cellsOf(hourly, u"11 AM - 12 PM"_s) == QStringList{u"11 AM - 12 PM"_s, u"1"_s, u"1"_s, u"$2.75"_s});
    CHECK(cellsOf(hourly, u"12 PM - 1 PM"_s) == QStringList{u"12 PM - 1 PM"_s, u"2"_s, u"2"_s, u"$15.00"_s});
    CHECK(cellsOf(hourly, u"Total"_s).last() == u"$17.75"_s);

    const QVariantMap cats = pos.report(u"categories"_s);
    CHECK(cellsOf(cats, u"Salads"_s) == QStringList{u"Salads"_s, u"1"_s, u"$12.50"_s, u"70.4%"_s});
    CHECK(cellsOf(cats, u"Drinks"_s) == QStringList{u"Drinks"_s, u"2"_s, u"$5.25"_s, u"29.6%"_s});
    // Biggest first.
    CHECK(cats[u"rows"_s].toList().first().toMap()[u"cells"_s].toStringList().first() == u"Salads"_s);
}

TEST_CASE("Audit trail: voids, discounts, reopened, transferred", "[reports][audit]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    pos.entryKey(u"10000"_s);
    REQUIRE(pos.openDrawerSession());
    CHECK(cellsOf(pos.report(u"audit"_s), u"No voids, discounts, reopened, moved, transferred or merged checks."_s)
              .size() == 1);

    REQUIRE(pos.selectTable(u"T4"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"coffee"_s);
    pos.addItem(u"tea"_s);
    REQUIRE(pos.sendOrder());
    REQUIRE(pos.voidItem());                               // the tea
    REQUIRE(pos.tender(u"discount"_s));
    REQUIRE(pos.tender(u"cash"_s));
    const qint64 id = pos.checkInfo()[u"id"_s].toLongLong();
    REQUIRE(pos.closeCheck());
    REQUIRE(pos.reopenCheck(id));

    const QVariantMap audit = pos.report(u"audit"_s);
    CHECK(cellsOf(audit, u"Voids"_s).last() == u"1"_s);
    CHECK(cellsOf(audit, u"Discounts"_s).last() == u"1"_s);
    CHECK(cellsOf(audit, u"Reopened checks"_s).last() == u"1"_s);
    const QStringList voided = cellsOf(audit, u"Voided Tea ($2.50)"_s);
    REQUIRE(voided.size() == 4);
    CHECK(voided[1] == u"T4 #%1"_s.arg(id));
    CHECK(voided[2].startsWith(u"Morgan"_s));
}

TEST_CASE("Reports are kept with the day at End of Day", "[reports]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    sell(pos, "coffee", u"T1"_s);
    REQUIRE(pos.endOfDay());
    const qint64 past = pos.days().at(1).toMap()[u"id"_s].toLongLong();
    for (const char16_t *id : {u"hourly", u"categories", u"audit"})
        CHECK_FALSE(pos.report(QString::fromUtf16(id), past)[u"rows"_s].toList().isEmpty());
}

TEST_CASE("Saving a report as CSV and PDF", "[reports][export]")
{
    QTemporaryDir dir;
    const QVariantMap report{
        {u"title"_s, u"Sales by Category"_s}, {u"subtitle"_s, u"Today"_s},
        {u"columns"_s, QStringList{u"Category"_s, u"Sales"_s}},
        {u"rows"_s, QVariantList{
            QVariantMap{{u"kind"_s, u"line"_s}, {u"cells"_s, QStringList{u"Fish, \"fresh\""_s, u"$1,200.00"_s}}},
            QVariantMap{{u"kind"_s, u"total"_s}, {u"cells"_s, QStringList{u"All"_s, u"$1,200.00"_s}}}}},
    };
    QString error;
    const QString csv = exportReportCsv(report, dir.filePath(u"out"_s), &error);
    REQUIRE_FALSE(csv.isEmpty());
    CHECK(csv.endsWith(u".csv"_s));
    CHECK(QFileInfo(csv).fileName().startsWith(u"sales-by-category-"_s));
    QFile f(csv);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    CHECK(text.contains(u"Category,Sales"_s));
    CHECK(text.contains(u"\"Fish, \"\"fresh\"\"\",\"$1,200.00\""_s));

    const QString pdf = exportReportPdf(report, dir.filePath(u"out"_s), &error);
    REQUIRE_FALSE(pdf.isEmpty());
    QFile p(pdf);
    REQUIRE(p.open(QIODevice::ReadOnly));
    CHECK(p.read(5) == "%PDF-");
    CHECK(p.size() > 500);
}
