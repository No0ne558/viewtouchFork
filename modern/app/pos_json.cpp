#include "app/pos_json.hh"

#include <QCryptographicHash>
#include <QRandomGenerator>

#include <cmath>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

QString qs(const std::string &s) { return QString::fromStdString(s); }
std::string ss(const QString &s) { return s.toStdString(); }

std::int64_t centsFromDecimal(double value) { return std::llround(value * 100.0); }
std::int64_t ppmFromPercent(double percent) { return std::llround(percent * 10000.0); }
double decimalFromCents(std::int64_t cents) { return double(cents) / 100.0; }
double percentFromPpm(std::int64_t ppm) { return double(ppm) / 10000.0; }

namespace {

qint64 i64(const QJsonValue &v) { return v.toInteger(); }
Money money(const QJsonValue &v) { return Money::fromCents(v.toInteger()); }

} // namespace

// --- checks --------------------------------------------------------------------

QJsonObject toJson(const Check &c)
{
    QJsonArray lines;
    for (const OrderLine &l : c.lines) {
        QJsonArray mods;
        for (const Modifier &m : l.modifiers) {
            QJsonObject mo{{u"itemId"_s, qs(m.itemId)}, {u"name"_s, qs(m.name)}, {u"unitPrice"_s, qint64(m.unitPrice.cents())}};
            if (m.qualifier != Qualifier::None)
                mo.insert(u"qualifier"_s, qs(toString(m.qualifier)));
            mods.append(mo);
        }
        QJsonObject lo{
            {u"id"_s, qint64(l.id)}, {u"itemId"_s, qs(l.itemId)}, {u"name"_s, qs(l.name)},
            {u"unitPrice"_s, qint64(l.unitPrice.cents())}, {u"quantity"_s, l.quantity},
            {u"taxClass"_s, qs(toString(l.taxClass))}, {u"sent"_s, l.sent}, {u"voided"_s, l.voided},
            {u"sentAt"_s, qint64(l.sentAt)},
        };
        if (l.qualifier != Qualifier::None)
            lo.insert(u"qualifier"_s, qs(toString(l.qualifier)));
        if (!mods.isEmpty())
            lo.insert(u"modifiers"_s, mods);
        if (!l.printer.empty())
            lo.insert(u"printer"_s, qs(l.printer));
        if (l.made) {
            lo.insert(u"made"_s, true);
            lo.insert(u"madeAt"_s, qint64(l.madeAt));
        }
        lines.append(lo);
    }
    QJsonArray payments;
    for (const Payment &p : c.payments) {
        payments.append(QJsonObject{
            {u"id"_s, qint64(p.id)}, {u"tenderId"_s, qs(p.tenderId)}, {u"tenderName"_s, qs(p.tenderName)},
            {u"kind"_s, qs(toString(p.kind))}, {u"amount"_s, qint64(p.amount.cents())}, {u"percentBp"_s, qint64(p.percentBp)},
            {u"tip"_s, qint64(p.tip.cents())},
        });
    }
    return {
        {u"schemaVersion"_s, PosSchemaVersion},
        {u"id"_s, qint64(c.id)}, {u"type"_s, qs(toString(c.type))}, {u"status"_s, qs(toString(c.status))},
        {u"label"_s, qs(c.label)}, {u"guests"_s, c.guests},
        {u"serverId"_s, qs(c.serverId)}, {u"serverName"_s, qs(c.serverName)},
        {u"openedAt"_s, qint64(c.openedAt)}, {u"closedAt"_s, qint64(c.closedAt)},
        {u"lines"_s, lines}, {u"payments"_s, payments},
        {u"nextLineId"_s, qint64(c.nextLineId)}, {u"nextPaymentId"_s, qint64(c.nextPaymentId)},
        {u"businessDay"_s, qint64(c.businessDay)}, {u"drawerSession"_s, qint64(c.drawerSession)},
        {u"gratuityBp"_s, qint64(c.gratuityBp)}, {u"autoGratuity"_s, c.autoGratuity},
        {u"customer"_s, QJsonObject{{u"name"_s, qs(c.customer.name)}, {u"phone"_s, qs(c.customer.phone)},
                                    {u"address"_s, qs(c.customer.address)}, {u"note"_s, qs(c.customer.note)}}},
    };
}

std::optional<Check> checkFromJson(const QJsonObject &o)
{
    if (o.value(u"schemaVersion").toInt(PosSchemaVersion) > PosSchemaVersion || !o.contains(u"id"))
        return std::nullopt;
    Check c;
    c.id = i64(o.value(u"id"));
    c.type = checkTypeFromString(ss(o.value(u"type").toString()));
    c.status = checkStatusFromString(ss(o.value(u"status").toString()));
    c.label = ss(o.value(u"label").toString());
    c.guests = o.value(u"guests").toInt(1);
    c.serverId = ss(o.value(u"serverId").toString());
    c.serverName = ss(o.value(u"serverName").toString());
    c.openedAt = i64(o.value(u"openedAt"));
    c.closedAt = i64(o.value(u"closedAt"));
    for (const QJsonValue &v : o.value(u"lines").toArray()) {
        const QJsonObject lo = v.toObject();
        OrderLine l;
        l.id = i64(lo.value(u"id"));
        l.itemId = ss(lo.value(u"itemId").toString());
        l.name = ss(lo.value(u"name").toString());
        l.unitPrice = money(lo.value(u"unitPrice"));
        l.quantity = lo.value(u"quantity").toInt(1);
        l.taxClass = taxClassFromString(ss(lo.value(u"taxClass").toString()));
        l.qualifier = qualifierFromString(ss(lo.value(u"qualifier").toString()));
        l.printer = ss(lo.value(u"printer").toString());
        l.sent = lo.value(u"sent").toBool();
        l.voided = lo.value(u"voided").toBool();
        l.sentAt = i64(lo.value(u"sentAt"));
        l.made = lo.value(u"made").toBool();
        l.madeAt = i64(lo.value(u"madeAt"));
        for (const QJsonValue &mv : lo.value(u"modifiers").toArray()) {
            const QJsonObject mo = mv.toObject();
            l.modifiers.push_back({ss(mo.value(u"itemId").toString()), ss(mo.value(u"name").toString()),
                                   money(mo.value(u"unitPrice")),
                                   qualifierFromString(ss(mo.value(u"qualifier").toString()))});
        }
        c.lines.push_back(l);
    }
    for (const QJsonValue &v : o.value(u"payments").toArray()) {
        const QJsonObject po = v.toObject();
        Payment p;
        p.id = i64(po.value(u"id"));
        p.tenderId = ss(po.value(u"tenderId").toString());
        p.tenderName = ss(po.value(u"tenderName").toString());
        p.kind = tenderKindFromString(ss(po.value(u"kind").toString()));
        p.amount = money(po.value(u"amount"));
        p.percentBp = i64(po.value(u"percentBp"));
        p.tip = money(po.value(u"tip"));
        c.payments.push_back(p);
    }
    c.nextLineId = std::max<std::int64_t>(i64(o.value(u"nextLineId")), 1);
    c.nextPaymentId = std::max<std::int64_t>(i64(o.value(u"nextPaymentId")), 1);
    c.businessDay = i64(o.value(u"businessDay"));
    c.drawerSession = i64(o.value(u"drawerSession"));
    c.gratuityBp = i64(o.value(u"gratuityBp"));
    c.autoGratuity = o.value(u"autoGratuity").toBool();
    const QJsonObject cust = o.value(u"customer").toObject();
    c.customer = {ss(cust.value(u"name").toString()), ss(cust.value(u"phone").toString()),
                  ss(cust.value(u"address").toString()), ss(cust.value(u"note").toString())};
    return c;
}

// --- menu ------------------------------------------------------------------------

QJsonObject toJson(const MenuItem &m)
{
    QJsonObject o{
        {u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)}, {u"price"_s, decimalFromCents(m.price.cents())},
        {u"taxClass"_s, qs(toString(m.taxClass))},
    };
    if (!m.family.empty()) o.insert(u"family"_s, qs(m.family));
    if (m.isModifier) o.insert(u"modifier"_s, true);
    if (!m.printer.empty()) o.insert(u"printer"_s, qs(m.printer));
    if (!m.available) o.insert(u"available"_s, false);
    return o;
}

MenuItem menuItemFromJson(const QJsonObject &o)
{
    MenuItem m;
    m.id = ss(o.value(u"id").toString());
    m.name = ss(o.value(u"name").toString());
    if (m.name.empty())
        m.name = m.id;
    m.family = ss(o.value(u"family").toString());
    m.price = Money::fromCents(centsFromDecimal(o.value(u"price").toDouble()));
    m.taxClass = taxClassFromString(ss(o.value(u"taxClass").toString(u"food"_s)));
    m.isModifier = o.value(u"modifier").toBool();
    m.printer = ss(o.value(u"printer").toString());
    m.available = o.value(u"available").toBool(true);
    return m;
}

std::vector<MenuItem> menuFromJson(const QJsonArray &a)
{
    std::vector<MenuItem> out;
    for (const QJsonValue &v : a)
        out.push_back(menuItemFromJson(v.toObject()));
    return out;
}

// --- employees ---------------------------------------------------------------------

std::string newSalt()
{
    quint32 words[4];
    QRandomGenerator::system()->fillRange(words);
    return QByteArray(reinterpret_cast<const char *>(words), sizeof words).toHex().toStdString();
}

std::string hashPin(const QString &pin, const std::string &salt)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(QByteArray::fromStdString(salt));
    h.addData(pin.toUtf8());
    return h.result().toHex().toStdString();
}

QJsonObject toJson(const Employee &e)
{
    return {
        {u"id"_s, qs(e.id)}, {u"name"_s, qs(e.name)}, {u"role"_s, qs(e.role)},
        {u"pinSalt"_s, qs(e.pinSalt)}, {u"pinHash"_s, qs(e.pinHash)}, {u"active"_s, e.active},
    };
}

Employee employeeFromJson(const QJsonObject &o)
{
    Employee e;
    e.id = ss(o.value(u"id").toString());
    e.name = ss(o.value(u"name").toString());
    e.role = ss(o.value(u"role").toString(u"server"_s));
    e.active = o.value(u"active").toBool(true);
    if (o.contains(u"pin")) {
        e.pinSalt = newSalt();
        e.pinHash = hashPin(o.value(u"pin").toString(), e.pinSalt);
    } else {
        e.pinSalt = ss(o.value(u"pinSalt").toString());
        e.pinHash = ss(o.value(u"pinHash").toString());
    }
    return e;
}

std::vector<Employee> employeesFromJson(const QJsonArray &a)
{
    std::vector<Employee> out;
    for (const QJsonValue &v : a)
        out.push_back(employeeFromJson(v.toObject()));
    return out;
}

// --- punches -------------------------------------------------------------------------

QJsonObject toJson(const TimePunch &p)
{
    return {{u"id"_s, qint64(p.id)}, {u"employeeId"_s, qs(p.employeeId)}, {u"clockIn"_s, qint64(p.clockIn)},
            {u"clockOut"_s, qint64(p.clockOut)}};
}

TimePunch punchFromJson(const QJsonObject &o)
{
    return {i64(o.value(u"id")), ss(o.value(u"employeeId").toString()), i64(o.value(u"clockIn")),
            i64(o.value(u"clockOut"))};
}

// --- reports and drawers ---------------------------------------------------------------

namespace {
QString kindName(ReportRow::Kind k)
{
    switch (k) {
    case ReportRow::Kind::Section: return u"section"_s;
    case ReportRow::Kind::Total: return u"total"_s;
    case ReportRow::Kind::Note: return u"note"_s;
    case ReportRow::Kind::Line: break;
    }
    return u"line"_s;
}

ReportRow::Kind kindFromName(const QString &s)
{
    if (s == u"section") return ReportRow::Kind::Section;
    if (s == u"total") return ReportRow::Kind::Total;
    if (s == u"note") return ReportRow::Kind::Note;
    return ReportRow::Kind::Line;
}

QJsonArray strings(const std::vector<std::string> &v)
{
    QJsonArray a;
    for (const std::string &s : v)
        a.append(qs(s));
    return a;
}

std::vector<std::string> strings(const QJsonArray &a)
{
    std::vector<std::string> v;
    for (const QJsonValue &x : a)
        v.push_back(ss(x.toString()));
    return v;
}
} // namespace

QJsonObject toJson(const Report &r)
{
    QJsonArray rows;
    for (const ReportRow &row : r.rows)
        rows.append(QJsonObject{{u"kind"_s, kindName(row.kind)}, {u"cells"_s, strings(row.cells)}});
    return {{u"id"_s, qs(r.id)}, {u"title"_s, qs(r.title)}, {u"subtitle"_s, qs(r.subtitle)},
            {u"columns"_s, strings(r.columns)}, {u"rows"_s, rows}};
}

Report reportFromJson(const QJsonObject &o)
{
    Report r;
    r.id = ss(o.value(u"id").toString());
    r.title = ss(o.value(u"title").toString());
    r.subtitle = ss(o.value(u"subtitle").toString());
    r.columns = strings(o.value(u"columns").toArray());
    for (const QJsonValue &v : o.value(u"rows").toArray()) {
        const QJsonObject row = v.toObject();
        r.rows.push_back({kindFromName(row.value(u"kind").toString()), strings(row.value(u"cells").toArray())});
    }
    return r;
}

QJsonObject toJson(const DrawerSession &d)
{
    QJsonArray movements;
    for (const CashMovement &m : d.movements) {
        movements.append(QJsonObject{
            {u"id"_s, qint64(m.id)}, {u"kind"_s, qs(toString(m.kind))}, {u"amount"_s, qint64(m.amount.cents())},
            {u"reason"_s, qs(m.reason)}, {u"by"_s, qs(m.by)}, {u"employeeId"_s, qs(m.employeeId)},
            {u"at"_s, qint64(m.at)}});
    }
    return {{u"id"_s, qint64(d.id)}, {u"name"_s, qs(d.name)}, {u"terminal"_s, qs(d.terminal)},
            {u"openedAt"_s, qint64(d.openedAt)},
            {u"openedBy"_s, qs(d.openedBy)}, {u"startingCash"_s, qint64(d.startingCash.cents())},
            {u"closedAt"_s, qint64(d.closedAt)}, {u"closedBy"_s, qs(d.closedBy)},
            {u"expected"_s, qint64(d.expected.cents())}, {u"counted"_s, qint64(d.counted.cents())},
            {u"movements"_s, movements}, {u"nextMovementId"_s, qint64(d.nextMovementId)}};
}

DrawerSession drawerFromJson(const QJsonObject &o)
{
    DrawerSession d;
    d.id = i64(o.value(u"id"));
    d.name = ss(o.value(u"name").toString(u"Drawer 1"_s));
    d.openedAt = i64(o.value(u"openedAt"));
    d.openedBy = ss(o.value(u"openedBy").toString());
    d.startingCash = money(o.value(u"startingCash"));
    d.closedAt = i64(o.value(u"closedAt"));
    d.closedBy = ss(o.value(u"closedBy").toString());
    d.expected = money(o.value(u"expected"));
    d.counted = money(o.value(u"counted"));
    d.terminal = ss(o.value(u"terminal").toString());
    for (const QJsonValue &v : o.value(u"movements").toArray()) {
        const QJsonObject m = v.toObject();
        d.movements.push_back({i64(m.value(u"id")), cashMovementKindFromString(ss(m.value(u"kind").toString())),
                               money(m.value(u"amount")), ss(m.value(u"reason").toString()),
                               ss(m.value(u"by").toString()), ss(m.value(u"employeeId").toString()),
                               i64(m.value(u"at"))});
    }
    d.nextMovementId = std::max<std::int64_t>(i64(o.value(u"nextMovementId")), 1);
    return d;
}

// --- settings ---------------------------------------------------------------------------

QJsonObject toJson(const PrinterConfig &p)
{
    QJsonObject o{{u"id"_s, qs(p.id)}, {u"name"_s, qs(p.name)}, {u"type"_s, qs(p.type)},
                  {u"width"_s, p.width}, {u"cutter"_s, p.cutter}, {u"drawerKick"_s, p.drawerKick}};
    if (!p.host.empty()) o.insert(u"host"_s, qs(p.host));
    if (p.port != 9100) o.insert(u"port"_s, p.port);
    if (!p.path.empty()) o.insert(u"path"_s, qs(p.path));
    if (!p.format.empty()) o.insert(u"format"_s, qs(p.format));
    return o;
}

PrinterConfig printerFromJson(const QJsonObject &o)
{
    PrinterConfig p;
    p.id = ss(o.value(u"id").toString());
    p.name = ss(o.value(u"name").toString());
    if (p.name.empty())
        p.name = p.id;
    p.type = ss(o.value(u"type").toString(u"none"_s));
    p.host = ss(o.value(u"host").toString());
    p.port = o.value(u"port").toInt(9100);
    p.path = ss(o.value(u"path").toString());
    p.format = ss(o.value(u"format").toString());
    p.width = std::clamp(o.value(u"width").toInt(42), 16, 80);
    p.cutter = o.value(u"cutter").toBool(true);
    p.drawerKick = o.value(u"drawerKick").toBool(false);
    return p;
}

QJsonObject toJson(const PosSettings &s)
{
    QJsonArray terminals;
    for (const TerminalConfig &t : s.terminals)
        terminals.append(QJsonObject{{u"name"_s, qs(t.name)}, {u"receiptPrinter"_s, qs(t.receiptPrinter)}});
    QJsonArray printers;
    for (const PrinterConfig &p : s.printers)
        printers.append(toJson(p));
    QJsonArray tenders;
    for (const Tender &t : s.tenders) {
        QJsonObject o{{u"id"_s, qs(t.id)}, {u"name"_s, qs(t.name)}, {u"kind"_s, qs(toString(t.kind))}};
        if (t.kind == TenderKind::Discount)
            o.insert(u"percent"_s, double(t.percentBp) / 100.0);
        tenders.append(o);
    }
    return {
        {u"schemaVersion"_s, PosSchemaVersion},
        {u"storeName"_s, qs(s.storeName)},
        {u"currencySymbol"_s, qs(s.currencySymbol)},
        {u"tax"_s, QJsonObject{
             {u"food"_s, percentFromPpm(s.tax.foodPpm)}, {u"alcohol"_s, percentFromPpm(s.tax.alcoholPpm)},
             {u"merchandise"_s, percentFromPpm(s.tax.merchandisePpm)}, {u"room"_s, percentFromPpm(s.tax.roomPpm)},
             {u"taxTakeoutFood"_s, s.tax.taxTakeoutFood}}},
        {u"tenders"_s, tenders},
        {u"printers"_s, printers},
        {u"receiptHeader"_s, qs(s.receiptHeader)},
        {u"receiptFooter"_s, qs(s.receiptFooter)},
        {u"gratuity"_s, QJsonObject{{u"percent"_s, double(s.gratuityBp) / 100.0}, {u"minGuests"_s, s.gratuityMinGuests}}},
        {u"terminals"_s, terminals},
    };
}

PosSettings settingsFromJson(const QJsonObject &o)
{
    PosSettings s;
    s.storeName = ss(o.value(u"storeName").toString(qs(s.storeName)));
    s.currencySymbol = ss(o.value(u"currencySymbol").toString(qs(s.currencySymbol)));
    const QJsonObject tax = o.value(u"tax").toObject();
    s.tax.foodPpm = ppmFromPercent(tax.value(u"food").toDouble());
    s.tax.alcoholPpm = ppmFromPercent(tax.value(u"alcohol").toDouble());
    s.tax.merchandisePpm = ppmFromPercent(tax.value(u"merchandise").toDouble());
    s.tax.roomPpm = ppmFromPercent(tax.value(u"room").toDouble());
    s.tax.taxTakeoutFood = tax.value(u"taxTakeoutFood").toBool(true);
    for (const QJsonValue &v : o.value(u"printers").toArray())
        s.printers.push_back(printerFromJson(v.toObject()));
    s.receiptHeader = ss(o.value(u"receiptHeader").toString());
    s.receiptFooter = ss(o.value(u"receiptFooter").toString());
    const QJsonObject gratuity = o.value(u"gratuity").toObject();
    s.gratuityBp = std::llround(gratuity.value(u"percent").toDouble() * 100.0);
    s.gratuityMinGuests = gratuity.value(u"minGuests").toInt(6);
    for (const QJsonValue &v : o.value(u"terminals").toArray()) {
        const QJsonObject t = v.toObject();
        s.terminals.push_back({ss(t.value(u"name").toString()), ss(t.value(u"receiptPrinter").toString())});
    }
    for (const QJsonValue &v : o.value(u"tenders").toArray()) {
        const QJsonObject t = v.toObject();
        Tender tender;
        tender.id = ss(t.value(u"id").toString());
        tender.name = ss(t.value(u"name").toString());
        tender.kind = tenderKindFromString(ss(t.value(u"kind").toString()));
        tender.percentBp = std::llround(t.value(u"percent").toDouble() * 100.0);
        s.tenders.push_back(tender);
    }
    return s;
}

} // namespace vt::app
