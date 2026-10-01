#include "storage/pos_store.hh"

#include "app/pos_json.hh"
#include "storage/async_writer.hh"

#include <QDir>
#include <QFileInfo>
#include <QAtomicInt>
#include <QDateTime>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <limits>

using namespace Qt::StringLiterals;
using namespace vt::core;
using vt::app::qs;

namespace vt::storage {

namespace {

QString compact(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject parse(const QVariant &v)
{
    return QJsonDocument::fromJson(v.toByteArray()).object();
}

bool run(QSqlQuery &q, const QString &sql, QString *error)
{
    if (q.exec(sql))
        return true;
    if (error)
        *error = u"%1: %2"_s.arg(sql.left(60), q.lastError().text());
    return false;
}

} // namespace

PosStore::PosStore(QString databasePath)
    : path_(std::move(databasePath))
    , connection_(u"vt-pos-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

PosStore::~PosStore()
{
    {
        QSqlDatabase db = QSqlDatabase::database(connection_, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(connection_);
}

bool PosStore::open(QString *error)
{
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, connection_);
    db.setDatabaseName(path_);
    if (!db.open()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    if (!run(q, u"PRAGMA journal_mode=WAL"_s, error) || !run(q, u"PRAGMA busy_timeout=5000"_s, error)
        || !run(q, u"CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)"_s, error))
        return false;

    int version = 0;
    if (q.exec(u"SELECT value FROM meta WHERE key = 'pos_schema_version'"_s) && q.next())
        version = q.value(0).toInt();
    if (version > DbSchemaVersion) {
        if (error)
            *error = u"POS database schema %1 is newer than this program (%2)"_s.arg(version).arg(DbSchemaVersion);
        return false;
    }

    // Migrations: add a step per version, never edit old ones.
    if (version < 1) {
        if (!db.transaction()) {
            if (error)
                *error = db.lastError().text();
            return false;
        }
        const bool ok =
            run(q, u"CREATE TABLE settings (key TEXT PRIMARY KEY, json TEXT NOT NULL)"_s, error)
            && run(q, u"CREATE TABLE menu_items (id TEXT PRIMARY KEY, position INTEGER NOT NULL, json TEXT NOT NULL)"_s, error)
            && run(q, u"CREATE TABLE employees (id TEXT PRIMARY KEY, json TEXT NOT NULL)"_s, error)
            && run(q, u"CREATE TABLE checks (id INTEGER PRIMARY KEY, status TEXT NOT NULL, label TEXT, "
                       "server_id TEXT, opened_at INTEGER, closed_at INTEGER, json TEXT NOT NULL)"_s, error)
            && run(q, u"CREATE INDEX checks_status ON checks (status)"_s, error)
            && run(q, u"CREATE TABLE time_punches (id INTEGER PRIMARY KEY, employee_id TEXT NOT NULL, "
                       "clock_in INTEGER NOT NULL, clock_out INTEGER NOT NULL DEFAULT 0)"_s, error)
            && run(q, u"INSERT OR REPLACE INTO meta (key, value) VALUES ('pos_schema_version', '1')"_s, error);
        if (!ok || !db.commit()) {
            db.rollback();
            return false;
        }
    }
    if (version < 2) {
        if (!db.transaction()) {
            if (error)
                *error = db.lastError().text();
            return false;
        }
        const bool ok =
            run(q, u"CREATE TABLE business_days (id INTEGER PRIMARY KEY, opened_at INTEGER NOT NULL, "
                   "closed_at INTEGER NOT NULL DEFAULT 0, reports TEXT)"_s, error)
            && run(q, u"CREATE TABLE drawer_sessions (id INTEGER PRIMARY KEY, opened_at INTEGER NOT NULL, "
                      "closed_at INTEGER NOT NULL DEFAULT 0, json TEXT NOT NULL)"_s, error)
            && run(q, u"ALTER TABLE checks ADD COLUMN business_day INTEGER NOT NULL DEFAULT 0"_s, error)
            && run(q, u"CREATE INDEX checks_day ON checks (business_day)"_s, error)
            && run(q, u"UPDATE meta SET value = '2' WHERE key = 'pos_schema_version'"_s, error);
        if (!ok || !db.commit()) {
            db.rollback();
            return false;
        }
    }
    if (version < 3) {   // breaks on time punches
        if (!db.transaction()) {
            if (error)
                *error = db.lastError().text();
            return false;
        }
        const bool ok =
            run(q, u"ALTER TABLE time_punches ADD COLUMN breaks TEXT NOT NULL DEFAULT '[]'"_s, error)
            && run(q, u"UPDATE meta SET value = '3' WHERE key = 'pos_schema_version'"_s, error);
        if (!ok || !db.commit()) {
            db.rollback();
            return false;
        }
    }
    if (version < 4) {   // customers and gift cards
        if (!db.transaction()) {
            if (error)
                *error = db.lastError().text();
            return false;
        }
        const bool ok =
            run(q, u"CREATE TABLE customers (id TEXT PRIMARY KEY, phone TEXT, json TEXT NOT NULL)"_s, error)
            && run(q, u"CREATE INDEX customers_phone ON customers (phone)"_s, error)
            && run(q, u"CREATE TABLE gift_cards (number TEXT PRIMARY KEY, json TEXT NOT NULL)"_s, error)
            && run(q, u"UPDATE meta SET value = '4' WHERE key = 'pos_schema_version'"_s, error);
        if (!ok || !db.commit()) {
            db.rollback();
            return false;
        }
    }
    if (version < 5) {   // the waitlist and reservations
        if (!db.transaction()) {
            if (error)
                *error = db.lastError().text();
            return false;
        }
        const bool ok =
            run(q, u"CREATE TABLE parties (id INTEGER PRIMARY KEY, day_key INTEGER NOT NULL, status TEXT NOT NULL, "
                   "json TEXT NOT NULL)"_s, error)
            && run(q, u"UPDATE meta SET value = '5' WHERE key = 'pos_schema_version'"_s, error);
        if (!ok || !db.commit()) {
            db.rollback();
            return false;
        }
    }
    if (version < 6) {   // inventory
        if (!db.transaction()) {
            if (error)
                *error = db.lastError().text();
            return false;
        }
        const bool ok =
            run(q, u"CREATE TABLE ingredients (id TEXT PRIMARY KEY, position INTEGER NOT NULL, json TEXT NOT NULL)"_s, error)
            && run(q, u"UPDATE meta SET value = '6' WHERE key = 'pos_schema_version'"_s, error);
        if (!ok || !db.commit()) {
            db.rollback();
            return false;
        }
    }
    if (version < 7) {   // the staff schedule
        if (!db.transaction()) {
            if (error)
                *error = db.lastError().text();
            return false;
        }
        const bool ok =
            run(q, u"CREATE TABLE shifts (id INTEGER PRIMARY KEY, start INTEGER NOT NULL, json TEXT NOT NULL)"_s, error)
            && run(q, u"UPDATE meta SET value = '7' WHERE key = 'pos_schema_version'"_s, error);
        if (!ok || !db.commit()) {
            db.rollback();
            return false;
        }
    }
    if (version < 8) {   // reports over a date range look checks up by when they closed
        if (!run(q, u"CREATE INDEX IF NOT EXISTS checks_closed ON checks (closed_at)"_s, error)
            || !run(q, u"UPDATE meta SET value = '8' WHERE key = 'pos_schema_version'"_s, error))
            return false;
    }
    return true;
}

bool PosStore::hasMenu() const
{
    QSqlQuery q(QSqlDatabase::database(connection_));
    return q.exec(u"SELECT COUNT(*) FROM menu_items"_s) && q.next() && q.value(0).toInt() > 0;
}

bool PosStore::seed(const PosSettings &settings, const std::vector<MenuItem> &menu,
                    const std::vector<Employee> &employees, QString *error,
                    const std::vector<core::Ingredient> &ingredients)
{
    QSqlDatabase db = QSqlDatabase::database(connection_);
    if (!db.transaction()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    auto fail = [&] {
        if (error)
            *error = q.lastError().text();
        db.rollback();
        return false;
    };

    q.prepare(u"INSERT OR REPLACE INTO settings (key, json) VALUES ('pos', ?)"_s);
    q.addBindValue(compact(app::toJson(settings)));
    if (!q.exec())
        return fail();
    if (!q.exec(u"DELETE FROM menu_items"_s) || !q.exec(u"DELETE FROM employees"_s)
        || !q.exec(u"DELETE FROM ingredients"_s))
        return fail();
    q.prepare(u"INSERT INTO ingredients (id, position, json) VALUES (?, ?, ?)"_s);
    for (int i = 0; i < int(ingredients.size()); ++i) {
        q.addBindValue(qs(ingredients[i].id));
        q.addBindValue(i);
        q.addBindValue(compact(app::toJson(ingredients[i])));
        if (!q.exec())
            return fail();
    }
    q.prepare(u"INSERT INTO menu_items (id, position, json) VALUES (?, ?, ?)"_s);
    int position = 0;
    for (const MenuItem &m : menu) {
        q.addBindValue(qs(m.id));
        q.addBindValue(position++);
        q.addBindValue(compact(app::toJson(m)));
        if (!q.exec())
            return fail();
    }
    q.prepare(u"INSERT INTO employees (id, json) VALUES (?, ?)"_s);
    for (const Employee &e : employees) {
        q.addBindValue(qs(e.id));
        q.addBindValue(compact(app::toJson(e)));
        if (!q.exec())
            return fail();
    }
    if (!db.commit()) {
        if (error)
            *error = db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

std::optional<app::PosData> PosStore::load(QStringList *errors) const
{
    QSqlQuery q(QSqlDatabase::database(connection_));
    app::PosData data;

    if (!q.exec(u"SELECT json FROM settings WHERE key = 'pos'"_s) || !q.next()) {
        if (errors)
            errors->append(u"POS settings are missing"_s);
        return std::nullopt;
    }
    data.settings = app::settingsFromJson(parse(q.value(0)));

    if (q.exec(u"SELECT json FROM menu_items ORDER BY position"_s)) {
        while (q.next())
            data.menu.push_back(app::menuItemFromJson(parse(q.value(0))));
    }
    if (q.exec(u"SELECT json FROM ingredients ORDER BY position"_s)) {
        while (q.next())
            data.ingredients.push_back(app::ingredientFromJson(parse(q.value(0))));
    }
    if (q.exec(u"SELECT json FROM customers"_s)) {
        while (q.next())
            data.customers.push_back(app::customerFromJson(parse(q.value(0))));
    }
    if (q.exec(u"SELECT json FROM gift_cards"_s)) {
        while (q.next())
            data.giftCards.push_back(app::giftCardFromJson(parse(q.value(0))));
    }
    if (q.exec(u"SELECT json FROM employees ORDER BY id"_s)) {
        while (q.next())
            data.employees.push_back(app::employeeFromJson(parse(q.value(0))));
    }
    data.openChecks = checks(CheckStatus::Open);
    if (q.exec(u"SELECT COALESCE(MAX(id), 0) FROM checks"_s) && q.next())
        data.lastCheckId = q.value(0).toLongLong();

    // Business day: the open one, its closed checks, and recent closed days.
    if (q.exec(u"SELECT COALESCE(MAX(id), 0) FROM business_days"_s) && q.next())
        data.lastDayId = q.value(0).toLongLong();
    if (q.exec(u"SELECT id, opened_at FROM business_days WHERE closed_at = 0 ORDER BY id DESC LIMIT 1"_s) && q.next())
        data.currentDay = BusinessDay{q.value(0).toLongLong(), q.value(1).toLongLong(), 0};
    const std::int64_t dayStart = data.currentDay ? data.currentDay->openedAt : std::numeric_limits<std::int64_t>::max();
    // Parties still waiting or booked (from any day), and today's (for the
    // host's numbers). day_key is when they joined or are booked for.
    q.prepare(u"SELECT json FROM parties WHERE status IN ('booked', 'waiting', 'notified') OR day_key >= ? ORDER BY id"_s);
    q.addBindValue(qint64(dayStart));
    if (q.exec()) {
        while (q.next())
            data.parties.push_back(app::partyFromJson(parse(q.value(0))));
    }
    if (q.exec(u"SELECT COALESCE(MAX(id), 0) FROM parties"_s) && q.next())
        data.lastPartyId = q.value(0).toLongLong();
    // Shifts from two weeks back on (this week's hours, the weeks ahead).
    q.prepare(u"SELECT json FROM shifts WHERE start >= ? ORDER BY start"_s);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch() - 14LL * 24 * 3'600'000);
    if (q.exec()) {
        while (q.next())
            data.shifts.push_back(app::shiftFromJson(parse(q.value(0))));
    }
    if (q.exec(u"SELECT COALESCE(MAX(id), 0) FROM shifts"_s) && q.next())
        data.lastShiftId = q.value(0).toLongLong();
    if (data.currentDay) {
        q.prepare(u"SELECT json FROM checks WHERE status = 'closed' AND business_day = ? ORDER BY id"_s);
        q.addBindValue(qint64(data.currentDay->id));
        if (q.exec()) {
            while (q.next()) {
                if (auto c = app::checkFromJson(parse(q.value(0))))
                    data.closedToday.push_back(std::move(*c));
            }
        }
    }
    if (q.exec(u"SELECT id, opened_at, closed_at, reports FROM business_days WHERE closed_at != 0 "
               "ORDER BY id DESC LIMIT 60"_s)) {
        while (q.next()) {
            data.pastDays.push_back({BusinessDay{q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toLongLong()},
                                     parse(q.value(3))});
        }
    }

    for (const TimePunch &p : punches()) {
        data.lastPunchId = std::max(data.lastPunchId, p.id);
        if (p.open() || p.clockIn >= dayStart)
            data.punches.push_back(p);
        else if (p.clockIn >= dayStart - 8LL * 24 * 3'600'000)   // for weekly overtime
            data.earlierPunches.push_back(p);
    }

    if (q.exec(u"SELECT COALESCE(MAX(id), 0) FROM drawer_sessions"_s) && q.next())
        data.lastDrawerId = q.value(0).toLongLong();
    q.prepare(u"SELECT json FROM drawer_sessions WHERE closed_at = 0 OR opened_at >= ? ORDER BY id"_s);
    q.addBindValue(qint64(dayStart));
    if (q.exec()) {
        while (q.next())
            data.drawers.push_back(app::drawerFromJson(parse(q.value(0))));
    }
    return data;
}

std::vector<Check> PosStore::checks(CheckStatus status) const
{
    std::vector<Check> out;
    QSqlQuery q(QSqlDatabase::database(connection_));
    q.prepare(u"SELECT json FROM checks WHERE status = ? ORDER BY id"_s);
    q.addBindValue(qs(toString(status)));
    if (!q.exec())
        return out;
    while (q.next()) {
        if (auto c = app::checkFromJson(parse(q.value(0))))
            out.push_back(std::move(*c));
    }
    return out;
}

std::vector<TimePunch> PosStore::punches() const
{
    std::vector<TimePunch> out;
    QSqlQuery q(QSqlDatabase::database(connection_));
    if (!q.exec(u"SELECT id, employee_id, clock_in, clock_out, breaks FROM time_punches ORDER BY id"_s))
        return out;
    while (q.next()) {
        TimePunch p{q.value(0).toLongLong(), q.value(1).toString().toStdString(), q.value(2).toLongLong(),
                    q.value(3).toLongLong(), {}};
        for (const QJsonValue &b : QJsonDocument::fromJson(q.value(4).toByteArray()).array())
            p.breaks.push_back({b.toObject().value(u"start").toInteger(), b.toObject().value(u"end").toInteger()});
        out.push_back(std::move(p));
    }
    return out;
}

void SqlPosSink::saveCheck(const Check &c)
{
    writer_.upsert(u"checks"_s, QString::number(c.id), {
        {u"id"_s, qint64(c.id)}, {u"status"_s, qs(toString(c.status))}, {u"label"_s, qs(c.label)},
        {u"server_id"_s, qs(c.serverId)}, {u"opened_at"_s, qint64(c.openedAt)},
        {u"closed_at"_s, qint64(c.closedAt)}, {u"business_day"_s, qint64(c.businessDay)},
        {u"json"_s, compact(app::toJson(c))},
    });
}

void SqlPosSink::saveDay(const BusinessDay &day, const QJsonObject &reports)
{
    writer_.upsert(u"business_days"_s, QString::number(day.id), {
        {u"id"_s, qint64(day.id)}, {u"opened_at"_s, qint64(day.openedAt)}, {u"closed_at"_s, qint64(day.closedAt)},
        {u"reports"_s, reports.isEmpty() ? QVariant() : QVariant(compact(reports))},
    });
}

void SqlPosSink::saveDrawer(const DrawerSession &d)
{
    writer_.upsert(u"drawer_sessions"_s, QString::number(d.id), {
        {u"id"_s, qint64(d.id)}, {u"opened_at"_s, qint64(d.openedAt)}, {u"closed_at"_s, qint64(d.closedAt)},
        {u"json"_s, compact(app::toJson(d))},
    });
}

void SqlPosSink::saveSettings(const PosSettings &s)
{
    writer_.upsert(u"settings"_s, u"pos"_s, {{u"key"_s, u"pos"_s}, {u"json"_s, compact(app::toJson(s))}});
}

void SqlPosSink::saveMenuItem(const MenuItem &item, int position)
{
    writer_.upsert(u"menu_items"_s, qs(item.id), {
        {u"id"_s, qs(item.id)}, {u"position"_s, position}, {u"json"_s, compact(app::toJson(item))},
    });
}

void SqlPosSink::deleteMenuItem(const std::string &id)
{
    writer_.remove(u"menu_items"_s, u"id"_s, qs(id));
}

void SqlPosSink::saveCustomer(const core::CustomerRecord &c)
{
    writer_.upsert(u"customers"_s, qs(c.id), {{u"id"_s, qs(c.id)},
                                             {u"phone"_s, qs(core::CustomerRecord::digits(c.phone))},
                                             {u"json"_s, compact(app::toJson(c))}});
}

void SqlPosSink::saveGiftCard(const core::GiftCard &g)
{
    writer_.upsert(u"gift_cards"_s, qs(g.number), {{u"number"_s, qs(g.number)}, {u"json"_s, compact(app::toJson(g))}});
}

void SqlPosSink::saveParty(const core::Party &party)
{
    writer_.upsert(u"parties"_s, QString::number(party.id),
                   {{u"id"_s, qint64(party.id)}, {u"day_key"_s, qint64(party.reservedFor ? party.reservedFor : party.addedAt)},
                    {u"status"_s, QString::fromStdString(core::toString(party.status))},
                    {u"json"_s, compact(app::toJson(party))}});
}

void SqlPosSink::saveIngredient(const core::Ingredient &i, int position)
{
    writer_.upsert(u"ingredients"_s, qs(i.id), {{u"id"_s, qs(i.id)}, {u"position"_s, position},
                                               {u"json"_s, compact(app::toJson(i))}});
}

void SqlPosSink::deleteIngredient(const std::string &id)
{
    writer_.remove(u"ingredients"_s, u"id"_s, qs(id));
}

void SqlPosSink::saveShift(const core::Shift &s)
{
    writer_.upsert(u"shifts"_s, QString::number(s.id),
                   {{u"id"_s, qint64(s.id)}, {u"start"_s, qint64(s.start)}, {u"json"_s, compact(app::toJson(s))}});
}

void SqlPosSink::deleteShift(std::int64_t id)
{
    writer_.remove(u"shifts"_s, u"id"_s, QString::number(id));
}

void SqlPosSink::saveEmployee(const Employee &e)
{
    writer_.upsert(u"employees"_s, qs(e.id), {{u"id"_s, qs(e.id)}, {u"json"_s, compact(app::toJson(e))}});
}

void SqlPosSink::savePunch(const TimePunch &p)
{
    writer_.upsert(u"time_punches"_s, QString::number(p.id), {
        {u"id"_s, qint64(p.id)}, {u"employee_id"_s, qs(p.employeeId)},
        {u"clock_in"_s, qint64(p.clockIn)}, {u"clock_out"_s, qint64(p.clockOut)},
        {u"breaks"_s, QString::fromUtf8(QJsonDocument(app::toJson(p).value(u"breaks").toArray()).toJson(QJsonDocument::Compact))},
    });
}

std::vector<Check> closedChecksBetween(const QString &dbPath, std::int64_t from, std::int64_t to, QString *error)
{
    static QAtomicInt counter;
    const QString name = u"vt-history-%1"_s.arg(counter.fetchAndAddRelaxed(1));
    std::vector<Check> out;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, name);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(u"QSQLITE_BUSY_TIMEOUT=5000"_s);
        if (!db.open()) {
            if (error)
                *error = db.lastError().text();
        } else {
            QSqlQuery q(db);
            q.prepare(u"SELECT json FROM checks WHERE status = 'closed' AND closed_at >= ? AND closed_at < ? ORDER BY closed_at"_s);
            q.addBindValue(qint64(from));
            q.addBindValue(qint64(to));
            if (q.exec()) {
                while (q.next()) {
                    if (auto c = app::checkFromJson(QJsonDocument::fromJson(q.value(0).toByteArray()).object()))
                        out.push_back(std::move(*c));
                }
            } else if (error) {
                *error = q.lastError().text();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return out;
}

} // namespace vt::storage
