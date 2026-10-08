#include <catch2/catch_test_macros.hpp>
#include <QDir>
#include <QImage>
#include <QBuffer>
#include "print/raster.hh"

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
#include <QTest>

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

TEST_CASE("Logo: a picture becomes black-and-white dots, fitted to the paper", "[print][logo]")
{
    // Left half black, right half white, the bottom row transparent.
    QImage img(100, 50, QImage::Format_ARGB32);
    img.fill(Qt::white);
    for (int y = 0; y < 50; ++y)
        for (int x = 0; x < 50; ++x)
            img.setPixel(x, y, qRgb(0, 0, 0));
    for (int x = 0; x < 100; ++x)
        img.setPixel(x, 49, qRgba(0, 0, 0, 0));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    const auto r = print::rasterize(png, 400, 200);
    REQUIRE(r);
    CHECK(r->width == 200);                       // at most twice its size
    CHECK(r->height == 100);
    CHECK(r->bits.size() == std::size_t(r->rowBytes() * r->height));
    CHECK(r->dot(10, 10));                        // black stays black
    CHECK_FALSE(r->dot(190, 10));                 // white stays paper
    CHECK_FALSE(r->dot(150, 99));                 // transparent is paper
    const auto small = print::rasterize(png, 60, 200);
    REQUIRE(small);
    CHECK(small->width == 60);                    // fitted
    CHECK(small->height == 30);
    CHECK_FALSE(print::rasterize("not a picture", 100, 100));
}

TEST_CASE("Logo: printed at the top of receipts on ESC/POS printers, as GS v 0", "[print][logo]")
{
    QImage img(64, 32, QImage::Format_RGB32);
    img.fill(Qt::black);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    // Bytes on paper: GS v 0, then width in bytes and height in rows (little-endian).
    print::Document d;
    d.image(print::rasterize(png, 64, 32));
    d.center("Cafe");
    d.cut = false;
    const std::string bytes = print::renderEscPos(d, 42);
    const std::string head = std::string("\x1dv0\0", 4) + char(8) + char(0) + char(32) + char(0);
    REQUIRE(bytes.find(head) != std::string::npos);
    CHECK(bytes.find(std::string(8 * 32, '\xff')) != std::string::npos);   // all black
    CHECK(print::renderText(d, 42).find("Cafe") != std::string::npos);      // plain text: no picture

    QTemporaryDir dir;
    auto seed = test::seedPosData();
    seed.settings.displayLogo = "store:logo.png";
    seed.settings.receiptLogo = true;
    for (core::PrinterConfig &p : seed.settings.printers)
        if (p.id == "receipt")
            p.format = "escpos";
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    printer.setImageSource([&](const QString &ref) { return ref == u"store:logo.png"_s ? png : QByteArray(); });
    const core::Check c = burgerCheck(seed.settings, seed.menu);
    printer.printReceipt(seed.settings, c, "receipt");
    REQUIRE(spooler.waitIdle(5000));
    QFile out(dir.filePath(u"receipt.txt"_s));
    REQUIRE(out.open(QIODevice::ReadOnly));
    const QByteArray printed = out.readAll();
    CHECK(printed.indexOf(QByteArray("\x1dv0\0", 4)) >= 0);
    CHECK(printed.indexOf(QByteArray("\x1dv0\0", 4)) < printed.indexOf("TOTAL"));   // at the top

    // Off: no picture.
    seed.settings.receiptLogo = false;
    out.close();
    QFile::remove(dir.filePath(u"receipt.txt"_s));
    printer.printReceipt(seed.settings, c, "receipt");
    REQUIRE(spooler.waitIdle(5000));
    REQUIRE(out.open(QIODevice::ReadOnly));
    CHECK(out.readAll().indexOf(QByteArray("\x1dv0", 3)) < 0);
}

TEST_CASE("A printer's test page: logo, text, the width ruler, a cut, the drawer", "[print][testpage]")
{
    QTemporaryDir dir;
    auto seed = test::seedPosData();
    seed.settings.displayLogo = "store:logo.png";
    for (core::PrinterConfig &p : seed.settings.printers)
        if (p.id == "receipt") {
            p.format = "escpos";
            p.drawerKick = true;
            p.cutter = true;
        }
    QImage img(64, 32, QImage::Format_RGB32);
    img.fill(Qt::black);
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    PrintSpooler spooler;
    TicketPrinter printer(spooler, dir.path());
    printer.setImageSource([&](const QString &ref) { return ref == u"store:logo.png"_s ? png : QByteArray(); });
    CHECK_FALSE(printer.printTestPage(seed.settings, "no-such-printer", false));
    REQUIRE(printer.printTestPage(seed.settings, "receipt", true));
    REQUIRE(spooler.waitIdle(5000));
    QFile out(dir.filePath(u"receipt.txt"_s));
    REQUIRE(out.open(QIODevice::ReadOnly));
    const QByteArray printed = out.readAll();
    CHECK(printed.contains("Printer test"));
    CHECK(printed.contains("characters per line"));
    CHECK(printed.indexOf(QByteArray("\x1dv0\0", 4)) >= 0);       // the logo
    CHECK(printed.contains(QByteArray("\x1bp\0", 3)));             // drawer kick
    CHECK(printed.contains(QByteArray("\x1dV\x42", 3)));           // cut
}

// A real printer on the network (hidden; run with the printer's address:
// VTM_PRINTER_HOST=192.168.1.101 vtm_tests "[printerlive]"). Prints the
// Test Print page: logo, text sizes, a sample item and total, the ruler.
TEST_CASE("A network printer, live: the Test Print page", "[.][printerlive]")
{
    const QString host = qEnvironmentVariable("VTM_PRINTER_HOST");
    if (host.isEmpty())
        SKIP("Set VTM_PRINTER_HOST to the printer's address.");
    core::PosSettings settings;
    settings.storeName = "ViewTouch";
    core::PrinterConfig p;
    p.id = "receipt";
    p.name = "Receipt";
    p.type = "network";
    p.host = host.toStdString();
    p.port = qEnvironmentVariableIntValue("VTM_PRINTER_PORT") ? qEnvironmentVariableIntValue("VTM_PRINTER_PORT") : 9100;
    p.width = qEnvironmentVariableIntValue("VTM_PRINTER_WIDTH") ? qEnvironmentVariableIntValue("VTM_PRINTER_WIDTH") : 42;
    settings.printers = {p};
    print::PrintSpooler spooler;
    print::TicketPrinter printer(spooler, QDir::tempPath());
    QString result;
    QObject::connect(&spooler, &print::PrintSpooler::jobPrinted, [&](const QString &, const QString &) { result = u"printed"_s; });
    QObject::connect(&spooler, &print::PrintSpooler::jobFailed,
                     [&](const QString &, const QString &, const QString &error) { result = u"failed: "_s + error; });
    REQUIRE(printer.printTestPage(settings, "receipt", false));
    for (int i = 0; i < 300 && result.isEmpty(); ++i)
        QTest::qWait(50);
    INFO(result.toStdString());
    CHECK(result == u"printed"_s);
}
