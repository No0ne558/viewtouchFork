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
        lines.append(lo);
    }
    QJsonArray payments;
    for (const Payment &p : c.payments) {
        payments.append(QJsonObject{
            {u"id"_s, qint64(p.id)}, {u"tenderId"_s, qs(p.tenderId)}, {u"tenderName"_s, qs(p.tenderName)},
            {u"kind"_s, qs(toString(p.kind))}, {u"amount"_s, qint64(p.amount.cents())}, {u"percentBp"_s, qint64(p.percentBp)},
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
        c.payments.push_back(p);
    }
    c.nextLineId = std::max<std::int64_t>(i64(o.value(u"nextLineId")), 1);
    c.nextPaymentId = std::max<std::int64_t>(i64(o.value(u"nextPaymentId")), 1);
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

// --- settings ---------------------------------------------------------------------------

QJsonObject toJson(const PosSettings &s)
{
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
