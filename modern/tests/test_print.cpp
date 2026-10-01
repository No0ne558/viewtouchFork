#include <catch2/catch_test_macros.hpp>

#include "pos_fixture.hh"
#include "print/document.hh"
#include "print/spooler.hh"
#include "print/ticket_printer.hh"
#include "print/tickets.hh"
#include "qt_catch.hh"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace vt;
using namespace vt::print;

namespace {

bool contains(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }

TicketContext ctx(const core::PosSettings &s)
{
    return {s, [](std::int64_t) { return std::string("Sep 29, 2026 10:31 AM"); },
            [](std::int64_t) { return std::string("10:31 AM"); }, 0};
}

core::Check burgerCheck(const core::PosSettings &s, const std::vector<core::MenuItem> &menu)
{
    auto item = [&](const char *id) {
        for (const auto &m : menu)
            if (m.id == id) return m;
        FAIL("no item " << id);
        return core::MenuItem{};
    };
    core::Check c;
    c.id = 42;
    c.label = "T3";
    c.guests = 2;
    c.serverName = "Sam";
    const auto &b = c.addItem(item("classic-burger"));
    c.addModifier(b.id, item("medium-rare"));
    c.addModifier(b.id, item("onion-rings"));
    c.addItem(item("draft-beer"));
    c.addComment("No pickles");
    c.addPayment(*s.tender("cash"), Money::fromCents(3000));
    return c;
}

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

TEST_CASE("Text rendering: columns, wrapping, centering", "[print]")
{
    Document d;
    d.center("Café");
    d.columns("Classic Burger", "$11.50");
    d.columns("A very long item name that has to wrap onto the next line", "$1.00");
    d.rule();
    const std::string out = renderText(d, 24);
    CHECK(contains(out, "          Café\n"));                 // (24 - 4) / 2 spaces
    CHECK(contains(out, "Classic Burger    $11.50\n"));      // exactly 24 wide
    CHECK(contains(out, std::string(24, '-')));
    for (const char *line : {"A very long item", "$1.00"})
        CHECK(contains(out, line));
    std::istringstream in(out);
    for (std::string row; std::getline(in, row);)
        CHECK(displayWidth(row) <= 24);
}

TEST_CASE("ESC/POS: init, emphasis, cut, drawer kick, ASCII only", "[print]")
{
    Document d;
    d.text("Crème brûlée", Document::Align::Left, true);
    d.kickDrawer = true;
    const std::string bytes = renderEscPos(d, 42);
    CHECK(bytes.rfind("\x1b@", 0) == 0);                      // ESC @ first
    CHECK(contains(bytes, std::string("\x1b" "E" "\x01", 3)));  // bold on
    CHECK(contains(bytes, "Creme brulee"));                   // transliterated
    CHECK(contains(bytes, std::string("\x1bp\x00\x19\xfa", 5)));
    CHECK(contains(bytes, std::string("\x1dV\x42\x00", 4)));   // partial cut
    CHECK(std::ranges::none_of(bytes, [](char c) { return static_cast<unsigned char>(c) >= 0x80 && c != '\xfa'; }));
}

TEST_CASE("Receipt and kitchen ticket contents", "[print]")
{
    const auto seed = test::seedPosData();
    const core::Check c = burgerCheck(seed.settings, seed.menu);

    const std::string r = renderText(receipt(c, ctx(seed.settings)), 42);
    CHECK(contains(r, "ViewTouch Café"));
    CHECK(contains(r, "123 Main Street"));
    CHECK(contains(r, "Check #42"));
    CHECK(contains(r, "Classic Burger"));
    CHECK(contains(r, "Onion Rings"));
    CHECK_FALSE(contains(r, "No pickles"));    // kitchen notes stay off the receipt
    // 11.50 + 1.00 food, 6.00 beer: tax 1.03 + 0.60 = 1.63; total 20.13; change 9.87
    CHECK(contains(r, "$20.13"));
    CHECK(contains(r, "Change"));
    CHECK(contains(r, "$9.87"));
    CHECK(contains(r, "Thank you for visiting!"));

    const std::string k = renderText(kitchenTicket(c, {c.lines[0], c.lines[2]}, "Kitchen", false, ctx(seed.settings)), 42);
    CHECK(contains(k, "KITCHEN"));
    CHECK(contains(k, "T3"));
    CHECK(contains(k, "1 Classic Burger"));
    CHECK(contains(k, "> MR"));
    CHECK(contains(k, "** No pickles **"));
    CHECK_FALSE(contains(k, "$"));             // no prices in the kitchen
    CHECK(contains(renderText(kitchenTicket(c, {c.lines[0]}, "Kitchen", true, ctx(seed.settings)), 42), "*** VOID ***"));
}

TEST_CASE("Report ticket lays out columns", "[print]")
{
    const auto seed = test::seedPosData();
    core::Report rep;
    rep.title = "Item Sales";
    rep.section("Burgers");
    rep.line({"Classic Burger", "3", "$34.50"});
    rep.total({"Burgers total", "3", "$34.50"});
    const std::string out = renderText(reportTicket(rep, ctx(seed.settings)), 42);
    CHECK(contains(out, "Item Sales"));
    // 42 wide: name, padding, then each value right-aligned in 9 columns.
    CHECK(contains(out, "Classic Burger" + std::string(18, ' ') + "3   $34.50\n"));
}

TEST_CASE("TicketPrinter routes kitchen lines by printer and writes files", "[print]")
{
    QTemporaryDir dir;
    auto seed = test::seedPosData();
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    const core::Check c = burgerCheck(seed.settings, seed.menu);

    printer.printKitchen(seed.settings, c, c.lines, false);
    printer.printReceipt(seed.settings, c, "receipt");
    printer.openDrawer(seed.settings, "receipt");
    REQUIRE(spooler.waitIdle(5000));

    const QString kitchen = readFile(dir.filePath(u"kitchen.txt"_s));
    const QString bar = readFile(dir.filePath(u"bar.txt"_s));
    const QString receiptText = readFile(dir.filePath(u"receipt.txt"_s));
    CHECK(kitchen.contains(u"Classic Burger"_s));
    CHECK(kitchen.contains(u"No pickles"_s));        // comments follow the kitchen
    CHECK_FALSE(kitchen.contains(u"Draft Beer"_s));
    CHECK(bar.contains(u"Draft Beer"_s));
    CHECK(receiptText.contains(u"TOTAL"_s));
    CHECK(receiptText.contains(u"[drawer opened]"_s));   // receipt printer has the drawer
}

TEST_CASE("Network printer receives ESC/POS over TCP", "[print]")
{
    QTcpServer server;
    REQUIRE(server.listen(QHostAddress::LocalHost));
    QByteArray received;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        QTcpSocket *s = server.nextPendingConnection();
        QObject::connect(s, &QTcpSocket::readyRead, [s, &received] { received += s->readAll(); });
    });

    core::PrinterConfig p;
    p.id = "kitchen";
    p.name = "Line";
    p.type = "network";
    p.host = "127.0.0.1";
    p.port = server.serverPort();
    PrintSpooler spooler;
    QSignalSpy printed(&spooler, &PrintSpooler::jobPrinted);
    spooler.submit(p, QByteArray::fromStdString(renderEscPos(Document().text("Hello kitchen"), 42)), u"test"_s);
    REQUIRE(spooler.waitIdle(5000));
    QElapsedTimer t;
    t.start();
    while (!received.contains("Hello kitchen") && t.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    CHECK(received.startsWith("\x1b@"));
    CHECK(received.contains("Hello kitchen"));
    CHECK(printed.size() == 1);
}

TEST_CASE("A dead printer never blocks the caller and is reported", "[print]")
{
    // A port nothing listens on: connection refused, retried, then reported.
    QTcpServer probe;
    REQUIRE(probe.listen(QHostAddress::LocalHost));
    const quint16 port = probe.serverPort();
    probe.close();

    core::PrinterConfig p;
    p.id = "kitchen";
    p.name = "Dead";
    p.type = "network";
    p.host = "127.0.0.1";
    p.port = port;
    PrintSpooler spooler;
    spooler.setTimings(300, 50);
    QSignalSpy failed(&spooler, &PrintSpooler::jobFailed);

    QElapsedTimer t;
    t.start();
    for (int i = 0; i < 20; ++i)
        spooler.submit(p, "ticket", u"ticket %1"_s.arg(i));
    CHECK(t.elapsed() < 100);   // queuing is instant

    // A file printer keeps working meanwhile.
    QTemporaryDir dir;
    core::PrinterConfig f;
    f.id = "receipt";
    f.name = "File";
    f.type = "file";
    f.path = dir.filePath(u"out.txt"_s).toStdString();
    spooler.submit(f, "receipt\n", u"receipt"_s);

    REQUIRE(spooler.waitIdle(20000));
    CHECK(failed.size() == 20);
    CHECK(failed.first()[0].toString() == u"Dead"_s);
    CHECK(readFile(dir.filePath(u"out.txt"_s)) == u"receipt\n"_s);
}
