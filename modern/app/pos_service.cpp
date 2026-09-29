#include "app/pos_service.hh"

#include "app/pos_json.hh"

#include <QDateTime>
#include <QLocale>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

constexpr int kMaxPin = 8;
constexpr int kMaxEntryDigits = 9;
constexpr int kMaxGuests = 99;

QString timeOfDay(std::int64_t ms)
{
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms).time(), QLocale::ShortFormat);
}

} // namespace

PosService::PosService(PosData data, PosSink *sink, QObject *parent)
    : QObject(parent)
    , settings_(std::move(data.settings))
    , menu_(std::move(data.menu))
    , employees_(std::move(data.employees))
    , punches_(std::move(data.punches))
    , lastCheckId_(data.lastCheckId)
    , lastPunchId_(data.lastPunchId)
    , sink_(sink)
    , now_([] { return QDateTime::currentMSecsSinceEpoch(); })
    , lastDayId_(data.lastDayId)
    , closedToday_(std::move(data.closedToday))
    , drawer_(std::move(data.drawer))
    , lastDrawerId_(data.lastDrawerId)
    , pastDays_(std::move(data.pastDays))
{
    for (Check &c : data.openChecks) {
        lastCheckId_ = std::max(lastCheckId_, c.id);
        open_.emplace(c.id, std::move(c));
    }
    for (const Check &c : closedToday_)
        lastCheckId_ = std::max(lastCheckId_, c.id);
    if (drawer_)
        lastDrawerId_ = std::max(lastDrawerId_, drawer_->id);
    if (data.currentDay && data.currentDay->open()) {
        day_ = *data.currentDay;
        lastDayId_ = std::max(lastDayId_, day_.id);
    } else {
        startDay();
    }
}

// --- helpers ---------------------------------------------------------------------

const Check *PosService::currentCheck() const
{
    auto it = open_.find(currentId_);
    return it == open_.end() ? nullptr : &it->second;
}

Check *PosService::current()
{
    auto it = open_.find(currentId_);
    return it == open_.end() ? nullptr : &it->second;
}

const MenuItem *PosService::findItem(const QString &idOrName) const
{
    const std::string key = ss(idOrName);
    for (const MenuItem &m : menu_) {
        if (m.id == key)
            return &m;
    }
    for (const MenuItem &m : menu_) {
        if (QString::compare(qs(m.name), idOrName, Qt::CaseInsensitive) == 0)
            return &m;
    }
    return nullptr;
}

bool PosService::can(const QString &permission) const
{
    return user_ && user_->can(ss(permission));
}

bool PosService::require(const char *permission, const QString &action)
{
    if (!user_)
        return fail(tr("Log in first."));
    if (!user_->can(permission))
        return fail(tr("%1 is not allowed for %2.").arg(action, qs(user_->name)));
    return true;
}

bool PosService::fail(const QString &message)
{
    emit notice(message);
    return false;
}

QString PosService::format(Money amount) const
{
    const QString s = qs(amount.toString());
    return s.startsWith(u'-') ? u"-"_s + qs(settings_.currencySymbol) + s.mid(1)
                              : qs(settings_.currencySymbol) + s;
}

void PosService::changed(Check &check)
{
    if (sink_)
        sink_->saveCheck(check);
    emit checkChanged();
    emit openChecksChanged();
}

// --- session ---------------------------------------------------------------------

void PosService::pinKey(const QString &key)
{
    if (key == u"clear")
        pin_.clear();
    else if (key == u"back")
        pin_.chop(1);
    else if (key.size() == 1 && key[0].isDigit() && pin_.size() < kMaxPin)
        pin_ += key;
    else
        return;
    emit entryChanged();
}

const Employee *PosService::employeeByPin(const QString &pin) const
{
    if (pin.isEmpty())
        return nullptr;
    for (const Employee &e : employees_) {
        if (e.active && e.pinHash == hashPin(pin, e.pinSalt))
            return &e;
    }
    return nullptr;
}

bool PosService::login()
{
    const QString pin = pin_;
    pin_.clear();
    emit entryChanged();
    return loginWithPin(pin);
}

bool PosService::loginWithPin(const QString &pin)
{
    const Employee *e = employeeByPin(pin);
    if (!e)
        return fail(tr("That PIN is not recognized."));
    user_ = e;
    emit sessionChanged();
    emit loggedInChanged(true);
    emit notice(tr("Welcome, %1").arg(qs(e->name)));
    return true;
}

void PosService::logout()
{
    if (!user_)
        return;
    releaseCheck();
    user_ = nullptr;
    pin_.clear();
    clearEntry();
    qualifier_ = Qualifier::None;
    emit qualifierChanged();
    emit sessionChanged();
    emit loggedInChanged(false);
}

TimePunch *PosService::openPunch(const std::string &employeeId)
{
    for (TimePunch &p : punches_) {
        if (p.employeeId == employeeId && p.open())
            return &p;
    }
    return nullptr;
}

bool PosService::clockIn()
{
    const Employee *e = user_ ? user_ : employeeByPin(pin_);
    pin_.clear();
    emit entryChanged();
    if (!e)
        return fail(tr("Enter your PIN, then Clock In."));
    if (openPunch(e->id))
        return fail(tr("%1 is already clocked in.").arg(qs(e->name)));
    TimePunch p{++lastPunchId_, e->id, now(), 0};
    punches_.push_back(p);
    if (sink_)
        sink_->savePunch(p);
    emit sessionChanged();
    emit dayChanged();
    emit notice(tr("%1 clocked in at %2").arg(qs(e->name), timeOfDay(p.clockIn)));
    return true;
}

bool PosService::clockOut()
{
    const Employee *e = user_ ? user_ : employeeByPin(pin_);
    pin_.clear();
    emit entryChanged();
    if (!e)
        return fail(tr("Enter your PIN, then Clock Out."));
    TimePunch *p = openPunch(e->id);
    if (!p)
        return fail(tr("%1 is not clocked in.").arg(qs(e->name)));
    p->clockOut = now();
    if (sink_)
        sink_->savePunch(*p);
    const double hours = double(p->clockOut - p->clockIn) / 3'600'000.0;
    emit sessionChanged();
    emit dayChanged();
    emit notice(tr("%1 clocked out (%2 hours)").arg(qs(e->name), QLocale().toString(hours, 'f', 2)));
    return true;
}

// --- keypads ----------------------------------------------------------------------

void PosService::entryKey(const QString &key)
{
    if (key == u"clear")
        entry_.clear();
    else if (key == u"back")
        entry_.chop(1);
    else if (!key.isEmpty() && std::ranges::all_of(key, [](QChar c) { return c.isDigit(); })) {
        if (entry_.size() + key.size() > kMaxEntryDigits)
            return;
        entry_ += key;
        while (entry_.size() > 1 && entry_.startsWith(u'0'))
            entry_.remove(0, 1);
    } else {
        return;
    }
    emit entryChanged();
}

void PosService::adjustGuests(int delta)
{
    entry_ = QString::number(std::clamp(entryGuests() + delta, 1, kMaxGuests));
    emit entryChanged();
}

void PosService::textKey(const QString &key)
{
    if (key == u"clear")
        text_.clear();
    else if (key == u"back")
        text_.chop(1);
    else if (key == u"space")
        text_ += u' ';
    else if (key.size() == 1)
        text_ += key;
    else
        return;
    emit entryChanged();
}

void PosService::clearEntry()
{
    if (entry_.isEmpty() && text_.isEmpty())
        return;
    entry_.clear();
    text_.clear();
    emit entryChanged();
}

// --- checks ------------------------------------------------------------------------

QVariantMap PosService::tableStatus(const QString &label) const
{
    QVariantMap status{{u"open"_s, false}};
    int count = 0;
    Money total;
    bool current = false;
    for (const auto &[id, c] : open_) {
        if (c.type != CheckType::DineIn || qs(c.label) != label)
            continue;
        if (count++ == 0) {
            status = {
                {u"open"_s, true}, {u"checkId"_s, qint64(id)}, {u"server"_s, qs(c.serverName)},
                {u"guests"_s, c.guests}, {u"mine"_s, user_ && c.serverId == user_->id},
            };
        }
        total += c.totals(settings_.tax).total;
        current = current || id == currentId_;
    }
    if (count > 0) {
        status.insert(u"checks"_s, count);
        status.insert(u"total"_s, format(total));
        status.insert(u"current"_s, current);
    }
    return status;
}

PosService::TableResult PosService::selectTable(const QString &label)
{
    if (!require(perm::Order, tr("Opening tables")))
        return TableResult::Failed;
    std::vector<std::int64_t> atTable;
    for (const auto &[id, c] : open_) {
        if (c.type == CheckType::DineIn && qs(c.label) == label)
            atTable.push_back(id);
    }
    if (atTable.size() == 1) {
        openCheck(atTable.front());
        return TableResult::OpenedExisting;
    }
    if (atTable.size() > 1) {
        releaseCheck();
        setCheckFilter(label);
        return TableResult::ChooseCheck;
    }
    releaseCheck();
    pendingTable_ = label;
    entry_.clear();
    emit entryChanged();
    emit checkChanged();
    return TableResult::NeedsGuestCount;
}

bool PosService::startCheck(CheckType type)
{
    if (!require(perm::Order, tr("Starting a check")))
        return false;
    if (type == CheckType::DineIn && pendingTable_.isEmpty())
        return fail(tr("Choose a table first."));

    Check c;
    c.id = ++lastCheckId_;
    c.type = type;
    c.serverId = user_->id;
    c.serverName = user_->name;
    c.openedAt = now();
    switch (type) {
    case CheckType::DineIn:
        c.label = ss(pendingTable_);
        c.guests = entryGuests();
        break;
    case CheckType::Takeout:
        c.label = ss(tr("Takeout %1").arg(c.id));
        break;
    case CheckType::Quick:
        c.label = ss(tr("Quick %1").arg(c.id));
        break;
    }
    pendingTable_.clear();
    entry_.clear();
    emit entryChanged();

    const auto id = c.id;
    open_.emplace(id, std::move(c));
    currentId_ = id;
    selectedLine_ = 0;
    selectedPayment_ = 0;
    changed(open_.at(id));
    return true;
}

bool PosService::openCheck(std::int64_t checkId)
{
    if (!open_.contains(checkId))
        return fail(tr("That check is no longer open."));
    currentId_ = checkId;
    selectedLine_ = 0;
    selectedPayment_ = 0;
    pendingTable_.clear();
    checkFilter_.clear();
    emit checkChanged();
    emit openChecksChanged();
    return true;
}

void PosService::releaseCheck()
{
    if (currentId_ == 0 && pendingTable_.isEmpty() && checkFilter_.isEmpty())
        return;
    checkFilter_.clear();
    currentId_ = 0;
    selectedLine_ = 0;
    selectedPayment_ = 0;
    pendingTable_.clear();
    qualifier_ = Qualifier::None;
    emit qualifierChanged();
    emit checkChanged();
    emit openChecksChanged();
}

bool PosService::addItem(const QString &idOrName)
{
    if (!require(perm::Order, tr("Ordering")))
        return false;
    const MenuItem *item = findItem(idOrName);
    if (!item)
        return fail(tr("'%1' is not on the menu.").arg(idOrName));
    if (!item->available)
        return fail(tr("%1 is sold out.").arg(qs(item->name)));

    if (!current() && !startCheck(CheckType::Quick))
        return false;
    Check &c = *current();

    const Qualifier q = qualifier_;
    if (item->isModifier) {
        OrderLine *target = c.line(selectedLine_);
        if (!target || target->sent || target->isComment())
            target = c.lastItemLine();
        if (!target || !c.addModifier(target->id, *item, q))
            return fail(tr("Order an item before adding %1.").arg(qs(item->name)));
        selectedLine_ = target->id;
    } else {
        selectedLine_ = c.addItem(*item, q).id;
    }
    if (qualifier_ != Qualifier::None) {
        qualifier_ = Qualifier::None;
        emit qualifierChanged();
    }
    changed(c);
    return true;
}

void PosService::setQualifier(const QString &qualifier)
{
    const Qualifier q = qualifierFromString(ss(qualifier));
    qualifier_ = (q == qualifier_) ? Qualifier::None : q;
    emit qualifierChanged();
}

void PosService::selectLine(qint64 lineId)
{
    if (lineId == selectedLine_)
        return;
    selectedLine_ = lineId;
    emit checkChanged();
}

void PosService::selectPayment(qint64 paymentId)
{
    if (paymentId == selectedPayment_)
        return;
    selectedPayment_ = paymentId;
    emit checkChanged();
}

bool PosService::voidItem()
{
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    OrderLine *l = c->line(selectedLine_);
    if (!l || l->voided) {
        // Default to the newest line still counting toward the total.
        l = nullptr;
        for (auto it = c->lines.rbegin(); it != c->lines.rend() && !l; ++it) {
            if (!it->voided)
                l = &*it;
        }
    }
    if (!l)
        return fail(tr("Nothing to void."));
    const QString name = qs(l->displayName());
    if (!l->sent) {
        c->removeLine(l->id);
        emit notice(tr("Removed %1").arg(name));
    } else {
        if (!require(perm::Void, tr("Voiding sent items")))
            return false;
        c->voidLine(l->id);
        if (printer_)
            printer_->printKitchen(settings_, *c, {*l}, true);
        emit notice(tr("Voided %1").arg(name));
    }
    selectedLine_ = 0;
    changed(*c);
    return true;
}

bool PosService::sendOrder()
{
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    std::vector<OrderLine> fresh;
    for (const OrderLine &l : c->lines) {
        if (!l.sent)
            fresh.push_back(l);
    }
    const int n = c->sendAll(now());
    if (n == 0)
        return fail(tr("Nothing new to send."));
    if (printer_)
        printer_->printKitchen(settings_, *c, fresh, false);
    emit notice(n == 1 ? tr("Sent 1 item to the kitchen") : tr("Sent %1 items to the kitchen").arg(n));
    changed(*c);
    return true;
}

bool PosService::addComment()
{
    if (!require(perm::Order, tr("Adding notes")))
        return false;
    const QString text = text_.trimmed();
    if (text.isEmpty())
        return fail(tr("Type the note first."));
    if (!current() && !startCheck(CheckType::Quick))
        return false;
    Check &c = *current();
    selectedLine_ = c.addComment(ss(text)).id;
    text_.clear();
    emit entryChanged();
    changed(c);
    return true;
}

bool PosService::tender(const QString &tenderId, std::optional<std::int64_t> amountCents)
{
    if (!require(perm::Settle, tr("Taking payments")))
        return false;
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    const Tender *t = settings_.tender(ss(tenderId));
    if (!t)
        return fail(tr("Payment type '%1' is not set up.").arg(tenderId));

    const Totals before = c->totals(settings_.tax);
    Money amount;
    if (t->kind != TenderKind::Discount) {
        if (before.balance.cents() <= 0)
            return fail(tr("Nothing is owed on this check."));
        if (amountCents)
            amount = Money::fromCents(*amountCents);
        else if (!entry_.isEmpty())
            amount = Money::fromCents(entry_.toLongLong());
        else
            amount = before.balance;
        if (amount.cents() <= 0)
            return fail(tr("Enter an amount."));
        // Only cash can be over-tendered (to give change).
        if (t->kind == TenderKind::Card && amount > before.balance)
            amount = before.balance;
    }
    c->addPayment(*t, amount);
    entry_.clear();
    emit entryChanged();
    const Totals after = c->totals(settings_.tax);
    if (after.change.cents() > 0)
        emit notice(tr("Change due: %1").arg(format(after.change)));
    else
        emit notice(tr("%1 applied").arg(qs(t->name)));
    changed(*c);
    return true;
}

bool PosService::removePayment()
{
    Check *c = current();
    if (!c || c->payments.empty())
        return fail(tr("No payment to remove."));
    if (!require(perm::Settle, tr("Removing payments")))
        return false;
    // The selected payment, else the most recent one.
    if (!c->removePayment(selectedPayment_))
        c->removePayment(c->payments.back().id);
    selectedPayment_ = 0;
    emit notice(tr("Payment removed"));
    changed(*c);
    return true;
}

bool PosService::closeCheck()
{
    if (!require(perm::Settle, tr("Closing checks")))
        return false;
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    const Totals t = c->totals(settings_.tax);
    if (t.balance.cents() > 0)
        return fail(tr("%1 is still due.").arg(format(t.balance)));
    const bool cash = t.cashPaid.cents() > 0;
    if (cash && !(drawer_ && drawer_->open()))
        return fail(tr("Open the cash drawer first (Manager → Drawer)."));

    if (c->unsentCount() > 0) {
        std::vector<OrderLine> fresh;
        for (const OrderLine &l : c->lines) {
            if (!l.sent && !l.voided)
                fresh.push_back(l);
        }
        c->sendAll(now());
        if (printer_ && !fresh.empty())
            printer_->printKitchen(settings_, *c, fresh, false);
    }
    c->status = CheckStatus::Closed;
    c->closedAt = now();
    c->businessDay = day_.id;
    if (cash)
        c->drawerSession = drawer_->id;
    if (sink_)
        sink_->saveCheck(*c);
    if (cash && printer_)
        printer_->openDrawer(settings_);
    closedToday_.push_back(*c);
    lastClosedId_ = c->id;
    const qint64 id = c->id;
    open_.erase(id);
    currentId_ = 0;
    selectedLine_ = 0;
    selectedPayment_ = 0;
    emit notice(t.change.cents() > 0 ? tr("Check closed. Change: %1").arg(format(t.change)) : tr("Check closed"));
    emit checkChanged();
    emit openChecksChanged();
    emit checkClosed(id);
    emit dayChanged();
    if (cash)
        emit drawerChanged();
    return true;
}

// --- QML-facing state ------------------------------------------------------------------

QString PosService::userName() const { return user_ ? qs(user_->name) : QString(); }
QString PosService::userRole() const { return user_ ? qs(user_->role) : QString(); }
QString PosService::storeName() const { return qs(settings_.storeName); }

bool PosService::clockedIn() const
{
    return user_ && std::ranges::any_of(punches_, [&](const TimePunch &p) { return p.employeeId == user_->id && p.open(); });
}

QString PosService::clockedInSince() const
{
    if (!user_)
        return {};
    for (const TimePunch &p : punches_) {
        if (p.employeeId == user_->id && p.open())
            return timeOfDay(p.clockIn);
    }
    return {};
}

QString PosService::entryAmount() const
{
    return format(Money::fromCents(entry_.toLongLong()));
}

int PosService::entryGuests() const
{
    return std::clamp(entry_.isEmpty() ? 1 : entry_.toInt(), 1, kMaxGuests);
}

QString PosService::pendingQualifier() const
{
    return qs(toString(qualifier_));
}

QVariantMap PosService::checkInfo() const
{
    const Check *c = currentCheck();
    if (!c)
        return {};
    return {
        {u"id"_s, qint64(c->id)}, {u"label"_s, qs(c->label)}, {u"guests"_s, c->guests},
        {u"server"_s, qs(c->serverName)}, {u"type"_s, qs(toString(c->type))},
        {u"opened"_s, timeOfDay(c->openedAt)},
    };
}

QVariantList PosService::lines() const
{
    QVariantList out;
    const Check *c = currentCheck();
    if (!c)
        return out;
    for (const OrderLine &l : c->lines) {
        QVariantList mods;
        for (const Modifier &m : l.modifiers) {
            mods.append(QVariantMap{{u"name"_s, qs(m.displayName())},
                                    {u"price"_s, m.price().cents() ? format(m.price()) : QString()}});
        }
        out.append(QVariantMap{
            {u"id"_s, qint64(l.id)}, {u"name"_s, qs(l.displayName())}, {u"quantity"_s, l.quantity},
            {u"price"_s, l.isComment() ? QString() : format(l.total())}, {u"comment"_s, l.isComment()},
            {u"sent"_s, l.sent}, {u"voided"_s, l.voided}, {u"modifiers"_s, mods},
            {u"selected"_s, qint64(l.id) == selectedLine_},
        });
    }
    return out;
}

QVariantMap PosService::totals() const
{
    const Check *c = currentCheck();
    if (!c)
        return {};
    const Totals t = c->totals(settings_.tax);
    QVariantList taxLines;
    for (const auto &[cls, amount] : t.taxByClass) {
        QString name = qs(toString(cls));
        name[0] = name[0].toUpper();
        taxLines.append(QVariantMap{{u"name"_s, tr("%1 tax").arg(name)}, {u"amount"_s, format(amount)}});
    }
    return {
        {u"items"_s, format(t.items)}, {u"discounts"_s, format(-t.discounts)},
        {u"hasDiscount"_s, t.discounts.cents() > 0},
        {u"subtotal"_s, format(t.subtotal)}, {u"tax"_s, format(t.tax)}, {u"taxLines"_s, taxLines},
        {u"total"_s, format(t.total)}, {u"paid"_s, format(t.paid)},
        {u"balance"_s, format(t.balance.cents() > 0 ? t.balance : Money())},
        {u"balanceCents"_s, qint64(t.balance.cents())},
        {u"change"_s, format(t.change)}, {u"hasChange"_s, t.change.cents() > 0},
    };
}

QVariantList PosService::payments() const
{
    QVariantList out;
    const Check *c = currentCheck();
    if (!c)
        return out;
    const Money items = c->totals(settings_.tax).items;
    for (const Payment &p : c->payments) {
        const QString amount = p.kind == TenderKind::Discount
            ? u"%1 (%2%)"_s.arg(format(-items.percent(p.percentBp))).arg(double(p.percentBp) / 100.0)
            : format(p.amount);
        out.append(QVariantMap{{u"id"_s, qint64(p.id)}, {u"name"_s, qs(p.tenderName)}, {u"amount"_s, amount},
                               {u"selected"_s, qint64(p.id) == selectedPayment_}});
    }
    return out;
}

QVariantList PosService::openChecks() const
{
    QVariantList out;
    const std::int64_t t = now();
    for (const auto &[id, c] : open_) {
        out.append(QVariantMap{
            {u"id"_s, qint64(id)}, {u"label"_s, qs(c.label)}, {u"server"_s, qs(c.serverName)},
            {u"guests"_s, c.guests}, {u"total"_s, format(c.totals(settings_.tax).total)},
            {u"minutes"_s, qint64((t - c.openedAt) / 60000)}, {u"type"_s, qs(toString(c.type))},
            {u"mine"_s, user_ && c.serverId == user_->id}, {u"current"_s, id == currentId_},
        });
    }
    return out;
}

} // namespace vt::app
