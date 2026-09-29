#include "print/ticket_printer.hh"

#include <QDateTime>
#include <QDir>
#include <QLocale>

#include <map>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::print {

TicketPrinter::TicketPrinter(PrintSpooler &spooler, QString outputDir)
    : spooler_(spooler)
    , outputDir_(std::move(outputDir))
    , now_([] { return QDateTime::currentMSecsSinceEpoch(); })
{
}

TicketContext TicketPrinter::context(const PosSettings &settings) const
{
    return TicketContext{
        settings,
        [](std::int64_t ms) {
            return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms), QLocale::ShortFormat).toStdString();
        },
        [](std::int64_t ms) {
            return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms).time(), QLocale::ShortFormat).toStdString();
        },
        now_(),
    };
}

void TicketPrinter::send(const PosSettings &, const PrinterConfig &printer, const Document &doc,
                         const QString &description)
{
    PrinterConfig target = printer;
    if (target.type == "file" && QDir::isRelativePath(QString::fromStdString(target.path)))
        target.path = QDir(outputDir_).filePath(QString::fromStdString(target.path)).toStdString();
    Document d = doc;
    d.cut = d.cut && printer.cutter;
    const std::string bytes = printer.effectiveFormat() == "escpos" ? renderEscPos(d, printer.width)
                                                                   : renderText(d, printer.width);
    spooler_.submit(target, QByteArray::fromStdString(bytes), description);
}

void TicketPrinter::printKitchen(const PosSettings &settings, const Check &check,
                                 const std::vector<OrderLine> &lines, bool voids)
{
    // Group by station; items without a printer (or with an unknown one) go
    // to the kitchen, and nowhere if there is no kitchen printer either.
    std::map<std::string, std::vector<OrderLine>> byStation;
    for (const OrderLine &l : lines) {
        std::string station = l.printer.empty() ? "kitchen" : l.printer;
        if (!settings.printer(station))
            station = "kitchen";
        byStation[station].push_back(l);
    }
    const TicketContext ctx = context(settings);
    for (const auto &[station, stationLines] : byStation) {
        const PrinterConfig *p = settings.printer(station);
        if (!p)
            continue;
        send(settings, *p, kitchenTicket(check, stationLines, p->name, voids, ctx),
             (voids ? u"Void ticket %1"_s : u"Ticket %1"_s).arg(QString::fromStdString(check.label)));
    }
}

void TicketPrinter::printReceipt(const PosSettings &settings, const Check &check)
{
    if (const PrinterConfig *p = settings.printer("receipt"))
        send(settings, *p, receipt(check, context(settings)), u"Receipt #%1"_s.arg(check.id));
}

void TicketPrinter::printReport(const PosSettings &settings, const Report &report)
{
    if (const PrinterConfig *p = settings.printer("receipt"))
        send(settings, *p, reportTicket(report, context(settings)), QString::fromStdString(report.title));
}

void TicketPrinter::openDrawer(const PosSettings &settings)
{
    for (const PrinterConfig &p : settings.printers) {
        if (p.drawerKick) {
            send(settings, p, drawerKick(), u"Open drawer"_s);
            return;
        }
    }
}

} // namespace vt::print
