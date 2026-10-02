// PosService: the staff schedule - shifts by week, the next shift on the
// logout screen, and (when the store wants it) clocking in only near a
// scheduled shift, with a manager able to clock someone in anyway.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr std::int64_t kMinute = 60'000;

QString hourText(std::int64_t ms)
{
    const QTime t = QDateTime::fromMSecsSinceEpoch(ms).time();
    return t.minute() ? t.toString(u"h:mm AP"_s) : t.toString(u"h AP"_s);
}

// "2026-10-02 16:00" or epoch ms.
std::int64_t timeFrom(const QVariant &v)
{
    if (v.typeId() != QMetaType::QString && v.toLongLong() > 0)
        return v.toLongLong();
    const QDateTime t = QDateTime::fromString(v.toString().trimmed(), u"yyyy-MM-dd HH:mm"_s);
    return t.isValid() ? t.toMSecsSinceEpoch() : (v.toLongLong() > 0 ? v.toLongLong() : 0);
}

} // namespace

const Shift *PosService::shiftNow(const std::string &employeeId) const
{
    const std::int64_t t = now();
    const std::int64_t early = std::int64_t(s_->settings.clockInEarlyMinutes) * kMinute;
    for (const Shift &s : s_->shifts) {
        if (s.employeeId == employeeId && s.start - early <= t && t < s.end)
            return &s;
    }
    return nullptr;
}

QString PosService::nextShift() const
{
    const Employee *e = user();
    if (!e)
        return {};
    const Shift *next = nullptr;
    for (const Shift &s : s_->shifts) {
        if (s.employeeId == e->id && s.end > now() && (!next || s.start < next->start))
            next = &s;
    }
    if (!next)
        return {};
    const QDate day = QDateTime::fromMSecsSinceEpoch(next->start).date();
    const QDate today = QDateTime::fromMSecsSinceEpoch(now()).date();
    const QString when = day == today ? tr("today") : day == today.addDays(1) ? tr("tomorrow") : day.toString(u"ddd MMM d"_s);
    return tr("%1 %2 - %3").arg(when, hourText(next->start), hourText(next->end));
}

QString PosService::scheduleCheck(const Employee &e) const
{
    if (!s_->settings.scheduleRequired || e.can(perm::Manager) || shiftNow(e.id))
        return {};
    return tr("%1 isn't on the schedule now. A manager can clock you in from Manager → Schedule.").arg(qs(e.name));
}

bool PosService::addShift(const QVariantMap &r)
{
    if (!require(perm::Manager, tr("The schedule")))
        return false;
    const Employee *e = s_->employee(ss(r.value(u"employeeId"_s).toString()));
    if (!e || !e->active)
        return fail(tr("Choose who works the shift."));
    Shift s;
    s.employeeId = e->id;
    s.start = timeFrom(r.value(u"start"_s));
    s.end = timeFrom(r.value(u"end"_s));
    if (s.start <= 0 || s.end <= 0)
        return fail(tr("When does the shift start and end?"));
    if (s.end <= s.start)
        s.end += 24 * 60 * kMinute;   // past midnight
    if (s.end - s.start > 16 * 60 * kMinute)
        return fail(tr("A shift can be at most 16 hours."));
    for (const Shift &o : s_->shifts) {
        if (o.employeeId == s.employeeId && o.start < s.end && s.start < o.end)
            return fail(tr("%1 already works %2 - %3 then.").arg(qs(e->name), hourText(o.start), hourText(o.end)));
    }
    s.note = ss(r.value(u"note"_s).toString().trimmed());
    s.id = ++s_->lastShiftId;
    s_->shifts.push_back(s);
    std::ranges::sort(s_->shifts, {}, &Shift::start);
    if (s_->sink)
        s_->sink->saveShift(s);
    emit notice(tr("%1: %2 %3 - %4").arg(qs(e->name), QDateTime::fromMSecsSinceEpoch(s.start).toString(u"ddd"_s),
                                         hourText(s.start), hourText(s.end)));
    emit s_->staffChanged();
    return true;
}

bool PosService::removeShift(qint64 id)
{
    if (!require(perm::Manager, tr("The schedule")))
        return false;
    const auto it = std::ranges::find_if(s_->shifts, [&](const Shift &s) { return s.id == id; });
    if (it == s_->shifts.end())
        return fail(tr("That shift is gone."));
    s_->shifts.erase(it);
    if (s_->sink)
        s_->sink->deleteShift(id);
    emit notice(tr("Shift removed"));
    emit s_->staffChanged();
    return true;
}

bool PosService::clockInEmployee(const QString &employeeId)
{
    if (!require(perm::Manager, tr("Clocking someone in")))
        return false;
    const Employee *e = s_->employee(ss(employeeId));
    if (!e || !e->active)
        return fail(tr("No such employee."));
    if (openPunch(e->id))
        return fail(tr("%1 is already clocked in.").arg(qs(e->name)));
    return punchIn(*e, e->jobs().front(), qs(user()->name));   // their main job
}

bool PosService::setScheduleWeek(int offset)
{
    scheduleWeek_ = std::clamp(offset, -2, 8);
    emit sessionChanged();
    return true;
}

QVariantMap PosService::scheduleInfo() const
{
    if (!user())
        return {};
    const QDate today = QDateTime::fromMSecsSinceEpoch(now()).date();
    const int back = (today.dayOfWeek() % 7 - s_->settings.weekStartsOn + 7) % 7;
    const QDate first = today.addDays(-back + 7 * scheduleWeek_);
    QVariantList days;
    std::map<std::string, double> hours;
    for (int d = 0; d < 7; ++d) {
        const QDate date = first.addDays(d);
        const std::int64_t from = QDateTime(date, QTime(0, 0)).toMSecsSinceEpoch();
        const std::int64_t to = QDateTime(date.addDays(1), QTime(0, 0)).toMSecsSinceEpoch();
        QVariantList list;
        for (const Shift &s : s_->shifts) {
            if (s.start < from || s.start >= to)
                continue;
            const Employee *e = s_->employee(s.employeeId);
            const bool onClock = std::ranges::any_of(s_->punches, [&](const TimePunch &p) {
                return p.employeeId == s.employeeId && p.open();
            });
            list.append(QVariantMap{
                {u"id"_s, qint64(s.id)}, {u"employeeId"_s, qs(s.employeeId)},
                {u"name"_s, e ? qs(e->name) : qs(s.employeeId)}, {u"role"_s, e ? qs(e->role) : QString()},
                {u"time"_s, hourText(s.start) + u" - "_s + hourText(s.end)}, {u"note"_s, qs(s.note)},
                {u"hours"_s, s.hours()}, {u"now"_s, s.start <= now() && now() < s.end}, {u"onClock"_s, onClock},
            });
            hours[s.employeeId] += s.hours();
        }
        days.append(QVariantMap{{u"date"_s, date.toString(u"yyyy-MM-dd"_s)},
                                {u"label"_s, date.toString(u"ddd M/d"_s)}, {u"today"_s, date == today},
                                {u"shifts"_s, list}});
    }
    QVariantList totals;
    for (const auto &[id, h] : hours) {
        const Employee *e = s_->employee(id);
        totals.append(QVariantMap{{u"name"_s, e ? qs(e->name) : qs(id)}, {u"hours"_s, h},
                                  {u"over"_s, s_->settings.overtimeWeeklyHours > 0 && h > s_->settings.overtimeWeeklyHours}});
    }
    QVariantList staff;
    for (const Employee &e : s_->employees) {
        if (e.active)
            staff.append(QVariantMap{{u"id"_s, qs(e.id)}, {u"name"_s, qs(e.name)}, {u"role"_s, qs(e.role)}});
    }
    return {
        {u"week"_s, scheduleWeek_}, {u"title"_s, scheduleWeek_ == 0 ? tr("This week") : scheduleWeek_ == 1 ? tr("Next week")
                                                  : tr("Week of %1").arg(first.toString(u"MMM d"_s))},
        {u"days"_s, days}, {u"totals"_s, totals}, {u"staff"_s, staff},
        {u"required"_s, s_->settings.scheduleRequired},
    };
}

} // namespace vt::app
