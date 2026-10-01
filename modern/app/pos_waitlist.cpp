// PosService: the host stand - a waitlist (quotes, "your table is ready",
// seating) and reservations (booked, checked in, seated or no-show).

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr std::int64_t kMinute = 60'000;

QString clock(std::int64_t ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(u"h:mm AP"_s); }

QString dayAndTime(std::int64_t ms, std::int64_t now)
{
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms);
    const QDate today = QDateTime::fromMSecsSinceEpoch(now).date();
    if (t.date() == today)
        return t.toString(u"h:mm AP"_s);
    if (t.date() == today.addDays(1))
        return QObject::tr("Tomorrow %1").arg(t.toString(u"h:mm AP"_s));
    return t.toString(u"ddd MMM d, h:mm AP"_s);
}

} // namespace

Party *PosService::party(qint64 id)
{
    auto it = std::ranges::find_if(s_->parties, [&](const Party &p) { return p.id == id; });
    return it == s_->parties.end() ? nullptr : &*it;
}

void PosService::saveParty(const Party &p)
{
    if (s_->sink)
        s_->sink->saveParty(p);
    emit s_->dayChanged();
}

int PosService::quoteFor(int ahead) const
{
    const int minutes = (ahead + 1) * std::max(1, s_->settings.waitMinutesPerParty);
    return std::max(5, (minutes + 4) / 5 * 5);   // to the next 5 minutes
}

QVariantMap PosService::waitlistInfo() const
{
    if (!user())
        return {};
    const std::int64_t t = now();
    const auto row = [&](const Party &p, int position) {
        const int waited = int((t - p.waitingSince()) / kMinute);
        return QVariantMap{
            {u"id"_s, qint64(p.id)}, {u"name"_s, qs(p.name)}, {u"phone"_s, qs(p.phone)}, {u"size"_s, p.size},
            {u"note"_s, qs(p.note)}, {u"status"_s, qs(toString(p.status))}, {u"position"_s, position},
            {u"waited"_s, waited}, {u"quoted"_s, p.quotedMinutes},
            {u"late"_s, p.quotedMinutes > 0 && waited > p.quotedMinutes},
            {u"notifiedAgo"_s, p.notifiedAt ? int((t - p.notifiedAt) / kMinute) : -1},
            {u"reservation"_s, p.reservedFor != 0},
            {u"time"_s, p.reservedFor ? dayAndTime(p.reservedFor, t) : clock(p.addedAt)},
            {u"reservedForMs"_s, qint64(p.reservedFor)},
            // A booking that should have arrived by now (15 minutes' grace).
            {u"overdue"_s, p.status == Party::Status::Booked && t > p.reservedFor + 15 * kMinute},
            {u"table"_s, qs(p.table)},
        };
    };
    QVariantList waiting, booked;
    std::vector<const Party *> line;
    for (const Party &p : s_->parties) {
        if (p.inLine())
            line.push_back(&p);
    }
    std::ranges::sort(line, {}, &Party::waitingSince);
    for (const Party *p : line)
        waiting.append(row(*p, int(waiting.size()) + 1));

    std::vector<const Party *> books;
    for (const Party &p : s_->parties) {
        if (p.status == Party::Status::Booked)
            books.push_back(&p);
    }
    std::ranges::sort(books, {}, &Party::reservedFor);
    for (const Party *p : books)
        booked.append(row(*p, 0));

    // Today's numbers: seated, average wait, no-shows. Today: since the
    // business day opened or since midnight, whichever came first.
    const std::int64_t midnight = QDateTime(QDateTime::fromMSecsSinceEpoch(t).date(), QTime(0, 0)).toMSecsSinceEpoch();
    const std::int64_t since = std::min(s_->day.openedAt, midnight);
    int seated = 0, noShows = 0;
    std::int64_t waitedTotal = 0;
    for (const Party &p : s_->parties) {
        if (p.status == Party::Status::Seated && p.seatedAt >= since) {
            ++seated;
            waitedTotal += p.seatedAt - p.waitingSince();
        } else if (p.status == Party::Status::NoShow && p.reservedFor >= since) {
            ++noShows;
        }
    }
    return {
        {u"waiting"_s, waiting}, {u"booked"_s, booked}, {u"seatedToday"_s, seated},
        {u"averageWait"_s, seated ? int(waitedTotal / seated / kMinute) : 0}, {u"noShows"_s, noShows},
        {u"nextQuote"_s, quoteFor(int(waiting.size()))},
    };
}

qint64 PosService::addParty(const QVariantMap &r, bool reservation)
{
    if (!require(perm::Order, reservation ? tr("Reservations") : tr("The waitlist")))
        return 0;
    const std::string name = ss(r.value(u"name"_s).toString().trimmed());
    if (name.empty()) {
        fail(tr("Whose party is it? Enter a name."));
        return 0;
    }
    Party p;
    p.name = name;
    p.phone = ss(r.value(u"phone"_s).toString().trimmed());
    p.size = std::clamp(r.value(u"size"_s, 2).toInt(), 1, 99);
    p.note = ss(r.value(u"note"_s).toString().trimmed());
    p.customerId = ss(r.value(u"customerId"_s).toString());
    p.addedAt = now();
    if (reservation) {
        std::int64_t at = r.value(u"at"_s).toLongLong();
        if (at <= 0) {   // "2026-10-02 19:30"
            const QDateTime t = QDateTime::fromString(r.value(u"at"_s).toString().trimmed(), u"yyyy-MM-dd HH:mm"_s);
            at = t.isValid() ? t.toMSecsSinceEpoch() : 0;
        }
        if (at <= 0) {
            fail(tr("When is the reservation for?"));
            return 0;
        }
        if (at < now() - 60 * kMinute) {
            fail(tr("That time has already passed."));
            return 0;
        }
        p.reservedFor = at;
        p.status = Party::Status::Booked;
    } else {
        int ahead = 0;
        for (const Party &x : s_->parties)
            ahead += x.inLine() ? 1 : 0;
        p.quotedMinutes = r.contains(u"quote"_s) ? std::clamp(r.value(u"quote"_s).toInt(), 0, 600) : quoteFor(ahead);
        p.status = Party::Status::Waiting;
    }
    p.id = ++s_->lastPartyId;
    s_->parties.push_back(p);
    saveParty(p);
    emit notice(reservation ? tr("%1, %2 people, booked for %3").arg(qs(p.name)).arg(p.size).arg(dayAndTime(p.reservedFor, now()))
                            : tr("%1, %2 people: about %3 minutes").arg(qs(p.name)).arg(p.size).arg(p.quotedMinutes));
    return p.id;
}

bool PosService::updateParty(qint64 id, const QVariantMap &r)
{
    if (!require(perm::Order, tr("The waitlist")))
        return false;
    Party *p = party(id);
    if (!p || p->done())
        return fail(tr("That party isn't waiting any more."));
    if (r.contains(u"name"_s) && !r.value(u"name"_s).toString().trimmed().isEmpty())
        p->name = ss(r.value(u"name"_s).toString().trimmed());
    if (r.contains(u"phone"_s))
        p->phone = ss(r.value(u"phone"_s).toString().trimmed());
    if (r.contains(u"size"_s))
        p->size = std::clamp(r.value(u"size"_s).toInt(), 1, 99);
    if (r.contains(u"note"_s))
        p->note = ss(r.value(u"note"_s).toString().trimmed());
    if (r.contains(u"quote"_s))
        p->quotedMinutes = std::clamp(r.value(u"quote"_s).toInt(), 0, 600);
    if (r.contains(u"at"_s) && p->status == Party::Status::Booked && r.value(u"at"_s).toLongLong() > 0)
        p->reservedFor = r.value(u"at"_s).toLongLong();
    saveParty(*p);
    return true;
}

bool PosService::checkInParty(qint64 id)
{
    if (!require(perm::Order, tr("Reservations")))
        return false;
    Party *p = party(id);
    if (!p || p->status != Party::Status::Booked)
        return fail(tr("That reservation isn't waiting to be checked in."));
    p->arrivedAt = now();
    p->status = Party::Status::Waiting;
    p->quotedMinutes = 0;   // seated as soon as their table is free
    saveParty(*p);
    emit notice(tr("%1 is here (%2 people)").arg(qs(p->name)).arg(p->size));
    return true;
}

bool PosService::notifyParty(qint64 id)
{
    if (!require(perm::Order, tr("The waitlist")))
        return false;
    Party *p = party(id);
    if (!p || !p->inLine())
        return fail(tr("That party isn't waiting."));
    p->status = Party::Status::Notified;
    p->notifiedAt = now();
    saveParty(*p);
    QString message = qs(s_->settings.tableReadyText);
    message.replace(u"{name}"_s, qs(p->name)).replace(u"{store}"_s, qs(s_->settings.storeName));
    if (!p->phone.empty() && s_->sendText) {
        s_->sendText(qs(p->phone), message);
        emit notice(tr("Texted %1: their table is ready").arg(qs(p->name)));
    } else {
        emit notice(tr("Tell %1 their table is ready").arg(qs(p->name)));
    }
    return true;
}

bool PosService::seatParty(qint64 id, const QString &table, const QString &serverId)
{
    if (!require(perm::Order, tr("Seating guests")))
        return false;
    Party *p = party(id);
    if (!p || p->done())
        return fail(tr("That party isn't waiting."));
    const QString label = table.trimmed();
    if (label.isEmpty())
        return fail(tr("Choose a table."));
    for (const auto &[cid, c] : s_->open) {
        if (c.type == CheckType::DineIn && QString::compare(qs(c.label), label, Qt::CaseInsensitive) == 0)
            return fail(tr("%1 is taken.").arg(label));
    }
    const Employee *server = serverId.isEmpty() ? user() : s_->employee(ss(serverId));
    if (!server)
        return fail(tr("Who is their server?"));

    // Their table's check, with the party size and their name, for the server.
    releaseCheck();
    pendingTable_ = label;
    entry_ = QString::number(p->size);
    if (!startCheck(CheckType::DineIn))
        return false;
    Check *c = current();
    c->serverId = server->id;
    c->serverName = server->name;
    c->customer.name = p->name;
    c->customer.phone = p->phone;
    c->customer.note = p->note;
    c->customerId = p->customerId;
    if (p->reservedFor)
        noteEvent(*c, tr("Reservation for %1").arg(clock(p->reservedFor)), "seat");
    changed(*c);
    p->status = Party::Status::Seated;
    p->seatedAt = now();
    p->table = ss(label);
    p->checkId = c->id;
    saveParty(*p);
    releaseCheck();   // it's the server's now
    emit notice(tr("%1 seated at %2 with %3").arg(qs(p->name), label, qs(server->name)));
    return true;
}

bool PosService::partyGone(qint64 id, bool noShow)
{
    if (!require(perm::Order, tr("The waitlist")))
        return false;
    Party *p = party(id);
    if (!p || p->done())
        return fail(tr("That party isn't waiting."));
    p->status = noShow && p->status == Party::Status::Booked ? Party::Status::NoShow : Party::Status::Left;
    saveParty(*p);
    emit notice(p->status == Party::Status::NoShow ? tr("%1 marked as a no-show").arg(qs(p->name))
                                                   : tr("%1 is off the list").arg(qs(p->name)));
    return true;
}

} // namespace vt::app
