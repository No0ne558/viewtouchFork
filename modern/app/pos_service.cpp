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

PosShared::PosShared(PosData data, PosSink *sink, QObject *parent)
    : QObject(parent)
    , settings(std::move(data.settings))
    , menu(std::move(data.menu))
    , employees(std::move(data.employees))
    , punches(std::move(data.punches))
    , earlierPunches(std::move(data.earlierPunches))
    , customers(std::move(data.customers))
    , giftCards(std::move(data.giftCards))
    , parties(std::move(data.parties))
    , lastPartyId(data.lastPartyId)
    , lastCheckId(data.lastCheckId)
    , lastPunchId(data.lastPunchId)
    , sink(sink)
    , lastDayId(data.lastDayId)
    , closedToday(std::move(data.closedToday))
    , drawers(std::move(data.drawers))
    , lastDrawerId(data.lastDrawerId)
    , pastDays(std::move(data.pastDays))
    , now_([] { return QDateTime::currentMSecsSinceEpoch(); })
{
    for (Check &c : data.openChecks) {
        lastCheckId = std::max(lastCheckId, c.id);
        open.emplace(c.id, std::move(c));
    }
    for (const Check &c : closedToday)
        lastCheckId = std::max(lastCheckId, c.id);
    for (const DrawerSession &d : drawers)
        lastDrawerId = std::max(lastDrawerId, d.id);
    if (data.currentDay && data.currentDay->open()) {
        day = *data.currentDay;
        lastDayId = std::max(lastDayId, day.id);
    } else {
        startDay();
    }
}

const Employee *PosShared::employee(const std::string &id) const
{
    if (id.empty())
        return nullptr;
    for (const Employee &e : employees) {
        if (e.id == id)
            return &e;
    }
    return nullptr;
}

PosService::PosService(PosData data, PosSink *sink, QObject *parent)
    : PosSession(parent)
    , owned_(std::make_unique<PosShared>(std::move(data), sink))
    , terminal_(tr("Terminal"))
{
    s_ = owned_.get();
    connectShared();
}

PosService::PosService(PosShared *shared, QString terminalName, QObject *parent)
    : PosSession(parent)
    , s_(shared)
    , terminal_(std::move(terminalName))
{
    connectShared();
}

PosService::~PosService()
{
    // A terminal going away (or disconnecting) lets go of its check.
    std::erase_if(s_->lockedBy, [this](const auto &kv) { return kv.second == this; });
    disconnect(s_, nullptr, this, nullptr);
}

void PosService::connectShared()
{
    // Changes made by any terminal refresh every terminal's view.
    connect(s_, &PosShared::checksChanged, this, [this] {
        emit openChecksChanged();
        emit checkChanged();
        emit kitchenChanged();
        emit dayChanged();
    });
    connect(s_, &PosShared::dayChanged, this, &PosSession::dayChanged);
    connect(s_, &PosShared::drawerChanged, this, &PosSession::drawerChanged);
    connect(s_, &PosShared::adminChanged, this, [this] {
        emit adminChanged();
        emit checkChanged();         // tax changes re-total
        emit openChecksChanged();
    });
    connect(s_, &PosShared::customersChanged, this, &PosSession::checkChanged);
    connect(s_, &PosShared::staffChanged, this, [this] {
        if (!userId_.empty() && !user()) {   // deactivated or removed elsewhere
            userId_.clear();
            emit loggedInChanged(false);
        }
        emit sessionChanged();
    });
}

const Employee *PosService::user() const
{
    const Employee *e = s_->employee(userId_);
    return e && e->active ? e : nullptr;
}

bool PosService::lockCheck(std::int64_t checkId)
{
    auto it = s_->lockedBy.find(checkId);
    if (it != s_->lockedBy.end() && it->second != this)
        return false;
    s_->lockedBy[checkId] = this;
    return true;
}

void PosService::unlockCheck(std::int64_t checkId)
{
    auto it = s_->lockedBy.find(checkId);
    if (it != s_->lockedBy.end() && it->second == this)
        s_->lockedBy.erase(it);
}

QString PosService::lockHolder(std::int64_t checkId) const
{
    auto it = s_->lockedBy.find(checkId);
    if (it == s_->lockedBy.end() || it->second == this)
        return {};
    const auto *other = qobject_cast<const PosService *>(it->second);
    return other ? other->terminalName() : tr("another terminal");
}

// --- helpers ---------------------------------------------------------------------

const Check *PosService::currentCheck() const
{
    auto it = s_->open.find(currentId_);
    return it == s_->open.end() ? nullptr : &it->second;
}

Check *PosService::current()
{
    auto it = s_->open.find(currentId_);
    return it == s_->open.end() ? nullptr : &it->second;
}

const MenuItem *PosService::findItem(const QString &idOrName) const
{
    const std::string key = ss(idOrName);
    for (const MenuItem &m : s_->menu) {
        if (m.id == key)
            return &m;
    }
    for (const MenuItem &m : s_->menu) {
        if (QString::compare(qs(m.name), idOrName, Qt::CaseInsensitive) == 0)
            return &m;
    }
    return nullptr;
}

bool PosService::require(const char *permission, const QString &action)
{
    if (!user())
        return fail(tr("Log in first."));
    if (!user()->can(permission))
        return fail(tr("%1 is not allowed for %2.").arg(action, qs(user()->name)));
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
    return s.startsWith(u'-') ? u"-"_s + qs(s_->settings.currencySymbol) + s.mid(1)
                              : qs(s_->settings.currencySymbol) + s;
}

void PosService::changed(Check &check)
{
    if (s_->sink)
        s_->sink->saveCheck(check);
    emit checkChanged();
    emit s_->checksChanged();
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
    for (const Employee &e : s_->employees) {
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
    userId_ = e->id;
    emit sessionChanged();
    emit loggedInChanged(true);
    emit notice(tr("Welcome, %1").arg(qs(e->name)));
    return true;
}

void PosService::logout()
{
    if (!user())
        return;
    releaseCheck();
    userId_.clear();
    pin_.clear();
    clearEntry();
    qualifier_ = Qualifier::None;
    emit qualifierChanged();
    emit sessionChanged();
    emit loggedInChanged(false);
}

TimePunch *PosService::openPunch(const std::string &employeeId)
{
    for (TimePunch &p : s_->punches) {
        if (p.employeeId == employeeId && p.open())
            return &p;
    }
    return nullptr;
}

bool PosService::clockIn()
{
    const Employee *e = user() ? user() : employeeByPin(pin_);
    pin_.clear();
    emit entryChanged();
    if (!e)
        return fail(tr("Enter your PIN, then Clock In."));
    if (openPunch(e->id))
        return fail(tr("%1 is already clocked in.").arg(qs(e->name)));
    TimePunch p{++s_->lastPunchId, e->id, now(), 0};
    s_->punches.push_back(p);
    if (s_->sink)
        s_->sink->savePunch(p);
    emit sessionChanged();
    emit s_->dayChanged();
    emit notice(tr("%1 clocked in at %2").arg(qs(e->name), timeOfDay(p.clockIn)));
    return true;
}

bool PosService::clockOut()
{
    const Employee *e = user() ? user() : employeeByPin(pin_);
    pin_.clear();
    emit entryChanged();
    if (!e)
        return fail(tr("Enter your PIN, then Clock Out."));
    TimePunch *p = openPunch(e->id);
    if (!p)
        return fail(tr("%1 is not clocked in.").arg(qs(e->name)));
    p->clockOut = now();
    if (p->onBreak())   // clocking out ends a break
        p->breaks.back().end = p->clockOut;
    if (s_->sink)
        s_->sink->savePunch(*p);
    const double hours = double(p->workedMs(p->clockOut, s_->settings.paidBreaks)) / 3'600'000.0;
    emit sessionChanged();
    emit s_->dayChanged();
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

PosSession::TableResult PosService::selectTable(const QString &label)
{
    if (!require(perm::Order, tr("Opening tables")))
        return TableFailed;
    std::vector<std::int64_t> atTable;
    for (const auto &[id, c] : s_->open) {
        if (c.type == CheckType::DineIn && qs(c.label) == label)
            atTable.push_back(id);
    }
    if (atTable.size() == 1)
        return openCheck(atTable.front()) ? TableOpened : TableFailed;
    if (atTable.size() > 1) {
        releaseCheck();
        setCheckFilter(label);
        return TableChooseCheck;
    }
    releaseCheck();
    pendingTable_ = label;
    entry_.clear();
    emit entryChanged();
    emit checkChanged();
    return TableNeedsGuests;
}

bool PosService::startCheck(CheckType type)
{
    if (!require(perm::Order, tr("Starting a check")))
        return false;
    if (type == CheckType::DineIn && pendingTable_.isEmpty())
        return fail(tr("Choose a table first."));

    Check c;
    c.id = ++s_->lastCheckId;
    c.type = type;
    c.serverId = user()->id;
    c.serverName = user()->name;
    c.openedAt = now();
    switch (type) {
    case CheckType::DineIn:
        c.label = ss(pendingTable_);
        c.guests = entryGuests();
        if (s_->settings.gratuityBp > 0 && c.guests >= s_->settings.gratuityMinGuests) {
            c.gratuityBp = s_->settings.gratuityBp;
            c.autoGratuity = true;
        }
        break;
    case CheckType::Takeout:
        c.label = ss(tr("Takeout %1").arg(c.id));
        break;
    case CheckType::Quick:
        c.label = ss(tr("Quick %1").arg(c.id));
        break;
    case CheckType::Delivery:
        c.label = ss(tr("Delivery %1").arg(c.id));
        break;
    }
    pendingTable_.clear();
    entry_.clear();
    emit entryChanged();

    const auto id = c.id;
    s_->open.emplace(id, std::move(c));
    if (currentId_ != 0)
        unlockCheck(currentId_);
    lockCheck(id);
    currentId_ = id;
    selectedLine_ = 0;
    selectedPayment_ = 0;
    changed(s_->open.at(id));
    return true;
}

bool PosService::openCheck(std::int64_t checkId)
{
    if (!s_->open.contains(checkId))
        return fail(tr("That check is no longer open."));
    if (const QString holder = lockHolder(checkId); !holder.isEmpty())
        return fail(tr("%1 is open on %2.").arg(qs(s_->open.at(checkId).label), holder));
    if (currentId_ != 0 && currentId_ != checkId)
        unlockCheck(currentId_);
    lockCheck(checkId);
    currentId_ = checkId;
    seat_ = 0;
    course_ = 1;
    choosingLine_ = 0;
    selectedLine_ = 0;
    selectedPayment_ = 0;
    pendingTable_.clear();
    checkFilter_.clear();
    emit checkChanged();
    emit s_->checksChanged();
    return true;
}

void PosService::releaseCheck()
{
    if (currentId_ == 0 && pendingTable_.isEmpty() && checkFilter_.isEmpty())
        return;
    checkFilter_.clear();
    // An empty takeout / delivery / quick check that is put away was never
    // really started: discard it instead of leaving it open. (An empty table
    // check stays: the guests may be seated before they order.)
    if (Check *c = current(); c && c->type != CheckType::DineIn && c->lines.empty() && c->payments.empty()) {
        c->status = CheckStatus::Discarded;
        c->closedAt = now();
        if (s_->sink)
            s_->sink->saveCheck(*c);
        s_->open.erase(c->id);
    }
    unlockCheck(currentId_);
    currentId_ = 0;
    seat_ = 0;
    course_ = 1;
    choosingLine_ = 0;
    selectedLine_ = 0;
    selectedPayment_ = 0;
    pendingTable_.clear();
    qualifier_ = Qualifier::None;
    emit qualifierChanged();
    emit checkChanged();
    emit s_->checksChanged();
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
        MenuItem priced = *item;   // the price for this meal period (dinner, happy hour...)
        priced.price = item->priceDuring(currentMealPeriod());
        OrderLine &line = c.addItem(priced, q);
        line.seat = seat_;
        line.course = course_;
        selectedLine_ = line.id;
        lineTouched_ = false;
        // Items with modifier groups ask for their choices next.
        choosingLine_ = 0;
        for (const std::string &g : item->modifierGroups) {
            if (s_->settings.modifierGroup(g))
                choosingLine_ = line.id;
        }
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
    lineTouched_ = lineId != 0;   // seat / course now apply to it
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
        noteEvent(*c, tr("Voided %1 (%2)").arg(name, format(l->unitPrice * l->quantity)), "void");
        if (s_->printer)
            s_->printer->printKitchen(s_->settings, *c, {*l}, true);
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
    const std::vector<OrderLine> fresh = c->sendable();
    if (const QString missing = missingChoice(fresh); !missing.isEmpty())
        return fail(missing);   // the kitchen needs the whole order
    const int n = c->sendAll(now());
    const int held = c->heldCount();
    if (n == 0)
        return fail(held > 0 ? tr("The rest is on hold: Fire the next course when it's time.")
                             : tr("Nothing new to send."));
    if (s_->printer)
        s_->printer->printKitchen(s_->settings, *c, fresh, false);
    const QString sent = n == 1 ? tr("Sent 1 item to the kitchen") : tr("Sent %1 items to the kitchen").arg(n);
    emit notice(held > 0 ? tr("%1; %2 on hold for a later course").arg(sent).arg(held) : sent);
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
    OrderLine &note = c.addComment(ss(text));
    note.seat = seat_;
    note.course = course_;
    selectedLine_ = note.id;
    lineTouched_ = false;
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
    const Tender *t = s_->settings.tender(ss(tenderId));
    if (!t)
        return fail(tr("Payment type '%1' is not set up.").arg(tenderId));
    if (t->kind == TenderKind::GiftCard)   // needs the card: the Gift Card page
        return giftCardNumber_.isEmpty() ? fail(tr("Open Gift Card and enter or swipe the card first."))
                                         : payWithGiftCard({}, amountCents.value_or(0));

    const Totals before = c->totals(s_->settings.tax);
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
        if (t->kind != TenderKind::Cash && amount > before.balance)
            amount = before.balance;
    }
    if (t->kind == TenderKind::HouseAccount) {
        entry_.clear();
        emit entryChanged();
        return chargeHouseAccount(*c, *t, amount);
    }
    if (t->kind == TenderKind::Discount && !require(perm::Discount, tr("Discounts and comps")))
        return false;
    Payment &paid = c->addPayment(*t, amount);
    // A tip the guest chose on the customer display goes on their card.
    if (t->kind == TenderKind::Card && tipChoice_.chosen && tipChoice_.checkId == c->id)
        paid.tip = tipFor(*c);
    if (t->kind == TenderKind::Discount)   // for the audit trail
        noteEvent(*c, tr("Discount: %1").arg(qs(t->name)), "discount");
    entry_.clear();
    emit entryChanged();
    const Totals after = c->totals(s_->settings.tax);
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
    const auto chosen = std::ranges::find_if(c->payments, [&](const Payment &p) { return p.id == selectedPayment_; });
    const Payment removed = chosen != c->payments.end() ? *chosen : c->payments.back();
    c->removePayment(removed.id);
    returnPayment(*c, removed);
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
    const Totals t = c->totals(s_->settings.tax);
    if (t.balance.cents() > 0)
        return fail(tr("%1 is still due.").arg(format(t.balance)));
    const bool cash = t.cashPaid.cents() > 0;
    // With server banks the cash stays with whoever closes the check (their
    // bank starts with the first cash sale); otherwise it goes in this
    // terminal's drawer, which must be open.
    DrawerSession *drawer = !cash ? nullptr : serverBank() ? ensureMyBank() : myDrawer();
    if (cash && !drawer)
        return fail(noDrawerMessage());

    if (c->unsentCount() > 0) {
        // Closing sends whatever is left, held courses too.
        const std::vector<OrderLine> fresh = c->sendable(true);
        if (const QString missing = missingChoice(fresh); !missing.isEmpty())
            return fail(missing);
        c->sendAll(now(), true);
        if (s_->printer && !fresh.empty())
            s_->printer->printKitchen(s_->settings, *c, fresh, false);
    }
    if (c->type == CheckType::Takeout || c->type == CheckType::Delivery)
        rememberCustomer(*c);   // on file for next time
    c->status = CheckStatus::Closed;
    c->closedAt = now();
    c->businessDay = s_->day.id;
    applyCloseEffects(*c);
    if (cash)
        c->drawerSession = drawer->id;
    if (s_->sink)
        s_->sink->saveCheck(*c);
    if (cash && s_->printer && !serverBank() && terminalHasDrawer())
        s_->printer->openDrawer(s_->settings, receiptPrinter());
    s_->closedToday.push_back(*c);
    lastClosedId_ = c->id;
    const qint64 id = c->id;
    unlockCheck(id);
    s_->open.erase(id);
    currentId_ = 0;
    selectedLine_ = 0;
    selectedPayment_ = 0;
    emit notice(t.change.cents() > 0 ? tr("Check closed. Change: %1").arg(format(t.change)) : tr("Check closed"));
    emit checkChanged();
    emit s_->checksChanged();
    emit checkClosed(id);
    emit s_->dayChanged();
    if (cash)
        emit s_->drawerChanged();
    return true;
}

// --- QML-facing state ------------------------------------------------------------------

QString PosService::userName() const { return user() ? qs(user()->name) : QString(); }
QString PosService::userRole() const { return user() ? qs(user()->role) : QString(); }
QString PosService::storeName() const { return qs(s_->settings.storeName); }

bool PosService::clockedIn() const
{
    return user() && std::ranges::any_of(s_->punches, [&](const TimePunch &p) { return p.employeeId == user()->id && p.open(); });
}

bool PosService::toggleBreak()
{
    const Employee *e = user();
    if (!e)
        return fail(tr("Log in first."));
    TimePunch *p = openPunch(e->id);
    if (!p)
        return fail(tr("Clock in first."));
    if (p->onBreak()) {
        p->breaks.back().end = now();
        emit notice(tr("Welcome back, %1").arg(qs(e->name)));
    } else {
        p->breaks.push_back({now(), 0});
        emit notice(tr("%1 is on break").arg(qs(e->name)));
    }
    if (s_->sink)
        s_->sink->savePunch(*p);
    emit sessionChanged();
    emit s_->dayChanged();
    return true;
}

QString PosService::onBreakSince() const
{
    if (!user())
        return {};
    for (const TimePunch &p : s_->punches) {
        if (p.employeeId == user()->id && p.open() && p.onBreak())
            return timeOfDay(p.breaks.back().start);
    }
    return {};
}

QString PosService::clockedInSince() const
{
    if (!user())
        return {};
    for (const TimePunch &p : s_->punches) {
        if (p.employeeId == user()->id && p.open())
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
        {u"seat"_s, seat_}, {u"course"_s, course_}, {u"firedCourse"_s, c->firedCourse},
        {u"heldCount"_s, c->heldCount()}, {u"rush"_s, c->rush}, {u"vip"_s, c->vip},
        {u"opened"_s, timeOfDay(c->openedAt)},
        {u"customer"_s, QVariantMap{{u"name"_s, qs(c->customer.name)}, {u"phone"_s, qs(c->customer.phone)},
                                    {u"address"_s, qs(c->customer.address)}, {u"note"_s, qs(c->customer.note)}}},
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
            {u"seat"_s, l.seat}, {u"course"_s, l.course}, {u"held"_s, c->held(l)},
            // Its modifier groups can still be changed / a required one is missing.
            {u"choices"_s, !l.sent && !l.isComment() && [&] {
                 const MenuItem *m = findItem(qs(l.itemId));
                 return m && !m->modifierGroups.empty(); }()},
            {u"needsChoice"_s, !l.sent && !missingChoice({l}).isEmpty()},
        });
    }
    return out;
}

QVariantMap PosService::totals() const
{
    const Check *c = currentCheck();
    if (!c)
        return {};
    const Totals t = c->totals(s_->settings.tax);
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
        {u"gratuity"_s, format(t.gratuity)}, {u"hasGratuity"_s, t.gratuity.cents() > 0},
        {u"gratuityPercent"_s, double(c->gratuityBp) / 100.0}, {u"autoGratuity"_s, c->autoGratuity},
        // What the Add gratuity key offers: the store's party rate, else 18%.
        {u"storeGratuityPercent"_s, double(s_->settings.gratuityBp > 0 ? s_->settings.gratuityBp : 1800) / 100.0},
        {u"tips"_s, format(t.tips)}, {u"hasTips"_s, t.tips.cents() > 0},
        {u"hasCard"_s, std::ranges::any_of(c->payments, [](const Payment &p) { return p.kind == TenderKind::Card; })},
    };
}

QVariantList PosService::payments() const
{
    QVariantList out;
    const Check *c = currentCheck();
    if (!c)
        return out;
    const Money items = c->totals(s_->settings.tax).items;
    for (const Payment &p : c->payments) {
        const QString amount = p.kind == TenderKind::Discount
            ? u"%1 (%2%)"_s.arg(format(-items.percent(p.percentBp))).arg(double(p.percentBp) / 100.0)
            : format(p.amount);
        out.append(QVariantMap{{u"id"_s, qint64(p.id)}, {u"name"_s, qs(p.tenderName)}, {u"amount"_s, amount},
                               {u"tip"_s, p.tip.cents() ? format(p.tip) : QString()},
                               {u"card"_s, p.kind == TenderKind::Card},
                               {u"selected"_s, qint64(p.id) == selectedPayment_}});
    }
    return out;
}

QVariantList PosService::openChecks() const
{
    QVariantList out;
    const std::int64_t t = now();
    for (const auto &[id, c] : s_->open) {
        const Money total = c.totals(s_->settings.tax).total;
        out.append(QVariantMap{
            {u"id"_s, qint64(id)}, {u"label"_s, qs(c.label)}, {u"server"_s, qs(c.serverName)},
            {u"guests"_s, c.guests}, {u"total"_s, format(total)}, {u"totalCents"_s, qint64(total.cents())},
            {u"minutes"_s, qint64((t - c.openedAt) / 60000)}, {u"type"_s, qs(toString(c.type))},
            {u"mine"_s, user() && c.serverId == user()->id}, {u"current"_s, id == currentId_},
            {u"lineCount"_s, int(c.lines.size())}, {u"busyOn"_s, lockHolder(id)},
            {u"customer"_s, qs(c.customer.name)},
        });
    }
    return out;
}

QStringList PosService::permissions() const
{
    QStringList out;
    if (const Employee *e = user()) {
        for (const std::string &p : e->permissions())
            out << qs(p);
    }
    return out;
}

QString PosService::currencySymbol() const
{
    return qs(s_->settings.currencySymbol);
}

// --- customers ---------------------------------------------------------------------------

bool PosService::setCustomer(const QVariantMap &customer)
{
    if (!require(perm::Order, tr("Changing customer details")))
        return false;
    Check *c = current();
    if (!c)
        return fail(tr("No check is open."));
    c->customer = {ss(customer.value(u"name"_s).toString().trimmed()), ss(customer.value(u"phone"_s).toString().trimmed()),
                   ss(customer.value(u"address"_s).toString().trimmed()), ss(customer.value(u"note"_s).toString().trimmed())};
    emit notice(tr("Customer saved"));
    changed(*c);
    return true;
}

// --- kitchen display -----------------------------------------------------------------------

QVariantList PosService::kitchenTickets() const
{
    // A ticket is everything sent in one go from one check that the kitchen
    // has not bumped yet. Closed checks count too (pay-first counters).
    struct Ticket { const Check *check; std::int64_t sentAt; std::vector<const OrderLine *> lines; };
    std::vector<Ticket> tickets;
    auto collect = [&](const Check &c) {
        std::map<std::int64_t, std::vector<const OrderLine *>> bySend;
        for (const OrderLine &l : c.lines) {
            if (l.sent && !l.made && !l.voided && !l.isGiftCard())
                bySend[l.sentAt].push_back(&l);
        }
        for (auto &[sentAt, lines] : bySend)
            tickets.push_back({&c, sentAt, std::move(lines)});
    };
    for (const auto &[id, c] : s_->open)
        collect(c);
    for (const Check &c : s_->closedToday)
        collect(c);
    // Rush orders first, then the oldest.
    std::ranges::sort(tickets, [](const Ticket &a, const Ticket &b) {
        return a.check->rush != b.check->rush ? a.check->rush : a.sentAt < b.sentAt;
    });

    QVariantList out;
    for (const Ticket &t : tickets) {
        QVariantList lines;
        for (const OrderLine *l : t.lines) {
            QStringList mods;
            for (const Modifier &m : l->modifiers)
                mods << qs(m.displayName());
            lines.append(QVariantMap{{u"name"_s, qs(l->displayName())}, {u"quantity"_s, l->quantity},
                                     {u"modifiers"_s, mods}, {u"comment"_s, l->isComment()},
                                     {u"printer"_s, qs(l->printer.empty() ? std::string("kitchen") : l->printer)},
                                     {u"seat"_s, l->seat}, {u"course"_s, l->course}});
        }
        out.append(QVariantMap{
            {u"checkId"_s, qint64(t.check->id)}, {u"sentAt"_s, qint64(t.sentAt)},
            {u"label"_s, qs(t.check->label)}, {u"server"_s, qs(t.check->serverName)},
            {u"type"_s, qs(toString(t.check->type))}, {u"customer"_s, qs(t.check->customer.name)},
            {u"note"_s, qs(t.check->customer.note)}, {u"lines"_s, lines},
            {u"rush"_s, t.check->rush}, {u"vip"_s, t.check->vip},
            {u"warnMinutes"_s, s_->settings.kitchenWarnMinutes}, {u"lateMinutes"_s, s_->settings.kitchenLateMinutes},
        });
    }
    return out;
}

namespace {
// The check with this id, open or closed today.
Check *findAnyCheck(PosShared *s, std::int64_t id)
{
    if (auto it = s->open.find(id); it != s->open.end())
        return &it->second;
    for (Check &c : s->closedToday) {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}
} // namespace

namespace {
bool atStation(const OrderLine &l, const std::string &station)
{
    return station.empty() || (l.printer.empty() ? std::string("kitchen") : l.printer) == station;
}
} // namespace

bool PosService::bumpTicket(qint64 checkId, qint64 sentAt, const QString &station)
{
    Check *c = findAnyCheck(s_, checkId);
    if (!c)
        return fail(tr("That ticket is gone."));
    const std::string where = ss(station);
    int n = 0;
    for (OrderLine &l : c->lines) {
        if (l.sent && !l.made && !l.voided && l.sentAt == sentAt && atStation(l, where)) {
            l.made = true;
            l.madeAt = now();
            ++n;
        }
    }
    if (n == 0)
        return fail(tr("That ticket was already bumped."));
    s_->bumped.push_back({checkId, sentAt, where});
    if (s_->sink)
        s_->sink->saveCheck(*c);
    emit s_->checksChanged();
    return true;
}

bool PosService::recallTicket()
{
    while (!s_->bumped.empty()) {
        const PosShared::Bump b = s_->bumped.back();
        s_->bumped.pop_back();
        Check *c = findAnyCheck(s_, b.checkId);
        if (!c)
            continue;
        for (OrderLine &l : c->lines) {
            if (l.sentAt == b.sentAt && l.made && atStation(l, b.station)) {
                l.made = false;
                l.madeAt = 0;
            }
        }
        if (s_->sink)
            s_->sink->saveCheck(*c);
        emit s_->checksChanged();
        emit notice(tr("Recalled %1").arg(qs(c->label)));
        return true;
    }
    return fail(tr("Nothing to recall."));
}

// --- invoke: operations by name (widgets, remote terminals) ----------------------------------

void PosService::invoke(const QString &method, const QVariantList &args, Reply reply)
{
    using Fn = std::function<QVariant(PosService &, const QVariantList &)>;
    static const QHash<QString, Fn> table = {
        {u"pinKey"_s, [](PosService &p, const QVariantList &a) { p.pinKey(a.value(0).toString()); return QVariant(true); }},
        {u"entryKey"_s, [](PosService &p, const QVariantList &a) { p.entryKey(a.value(0).toString()); return QVariant(true); }},
        {u"adjustGuests"_s, [](PosService &p, const QVariantList &a) { p.adjustGuests(a.value(0).toInt()); return QVariant(true); }},
        {u"textKey"_s, [](PosService &p, const QVariantList &a) { p.textKey(a.value(0).toString()); return QVariant(true); }},
        {u"clearEntry"_s, [](PosService &p, const QVariantList &) { p.clearEntry(); return QVariant(true); }},
        {u"login"_s, [](PosService &p, const QVariantList &) { return QVariant(p.login()); }},
        {u"loginWithPin"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.loginWithPin(a.value(0).toString())); }},
        {u"logout"_s, [](PosService &p, const QVariantList &) { p.logout(); return QVariant(true); }},
        {u"clockIn"_s, [](PosService &p, const QVariantList &) { return QVariant(p.clockIn()); }},
        {u"clockOut"_s, [](PosService &p, const QVariantList &) { return QVariant(p.clockOut()); }},
        {u"selectTable"_s, [](PosService &p, const QVariantList &a) { return QVariant(int(p.selectTable(a.value(0).toString()))); }},
        {u"startCheck"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.startCheck(checkTypeFromString(ss(a.value(0).toString())))); }},
        {u"openCheck"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.openCheck(a.value(0).toLongLong())); }},
        {u"releaseCheck"_s, [](PosService &p, const QVariantList &) { p.releaseCheck(); return QVariant(true); }},
        {u"addItem"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.addItem(a.value(0).toString())); }},
        {u"setQualifier"_s, [](PosService &p, const QVariantList &a) { p.setQualifier(a.value(0).toString()); return QVariant(true); }},
        {u"selectLine"_s, [](PosService &p, const QVariantList &a) { p.selectLine(a.value(0).toLongLong()); return QVariant(true); }},
        {u"selectPayment"_s, [](PosService &p, const QVariantList &a) { p.selectPayment(a.value(0).toLongLong()); return QVariant(true); }},
        {u"setCheckFilter"_s, [](PosService &p, const QVariantList &a) { p.setCheckFilter(a.value(0).toString()); return QVariant(true); }},
        {u"voidItem"_s, [](PosService &p, const QVariantList &) { return QVariant(p.voidItem()); }},
        {u"sendOrder"_s, [](PosService &p, const QVariantList &) { return QVariant(p.sendOrder()); }},
        {u"addComment"_s, [](PosService &p, const QVariantList &) { return QVariant(p.addComment()); }},
        {u"tender"_s, [](PosService &p, const QVariantList &a) {
             const QVariant amount = a.value(1);
             return QVariant(p.tender(a.value(0).toString(), amount.isValid() && !amount.isNull()
                                          ? std::optional<std::int64_t>(amount.toLongLong()) : std::nullopt)); }},
        {u"removePayment"_s, [](PosService &p, const QVariantList &) { return QVariant(p.removePayment()); }},
        {u"closeCheck"_s, [](PosService &p, const QVariantList &) { return QVariant(p.closeCheck()); }},
        {u"printReceipt"_s, [](PosService &p, const QVariantList &) { return QVariant(p.printReceipt()); }},
        {u"noSale"_s, [](PosService &p, const QVariantList &) { return QVariant(p.noSale()); }},
        {u"setCustomer"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.setCustomer(a.value(0).toMap())); }},
        {u"splitLine"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.splitLine(a.value(0).toLongLong())); }},
        {u"bumpTicket"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.bumpTicket(a.value(0).toLongLong(), a.value(1).toLongLong(), a.value(2).toString())); }},
        {u"recallTicket"_s, [](PosService &p, const QVariantList &) { return QVariant(p.recallTicket()); }},
        {u"addTip"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.addTip(a.value(0).toLongLong())); }},
        {u"setGratuity"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.setGratuity(a.value(0).toLongLong())); }},
        {u"payout"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.payout(cashMovementKindFromString(ss(a.value(0).toString())))); }},
        {u"cashOutTips"_s, [](PosService &p, const QVariantList &) { return QVariant(p.cashOutTips()); }},
        {u"toggleBreak"_s, [](PosService &p, const QVariantList &) { return QVariant(p.toggleBreak()); }},
        {u"askForTip"_s, [](PosService &p, const QVariantList &) { return QVariant(p.askForTip()); }},
        {u"toggleFlag"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.toggleFlag(a.value(0).toString())); }},
        {u"customerTip"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.customerTip(a.value(0).toString(), a.value(1).toLongLong())); }},
        {u"backupNow"_s, [](PosService &p, const QVariantList &) { return QVariant(p.backupNow()); }},
        {u"findCustomers"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.findCustomers(a.value(0).toString())); }},
        {u"selectCustomer"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.selectCustomer(a.value(0).toString())); }},
        {u"useCustomer"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.useCustomer(a.value(0).toString())); }},
        {u"saveCustomer"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.saveCustomer(a.value(0).toMap())); }},
        {u"sellGiftCard"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.sellGiftCard(a.value(0).toString(), a.value(1).toLongLong())); }},
        {u"lookupGiftCard"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.lookupGiftCard(a.value(0).toString())); }},
        {u"payWithGiftCard"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.payWithGiftCard(a.value(0).toString(), a.value(1).toLongLong())); }},
        {u"addToWaitlist"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.addToWaitlist(a.value(0).toMap())); }},
        {u"addReservation"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.addReservation(a.value(0).toMap())); }},
        {u"updateParty"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.updateParty(a.value(0).toLongLong(), a.value(1).toMap())); }},
        {u"checkInParty"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.checkInParty(a.value(0).toLongLong())); }},
        {u"notifyParty"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.notifyParty(a.value(0).toLongLong())); }},
        {u"seatParty"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.seatParty(a.value(0).toLongLong(), a.value(1).toString(), a.value(2).toString())); }},
        {u"partyGone"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.partyGone(a.value(0).toLongLong(), a.value(1).toBool())); }},
        {u"payOnAccount"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.payOnAccount(a.value(0).toString(), a.value(1).toLongLong())); }},
        {u"setSeat"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.setSeat(a.value(0).toInt())); }},
        {u"chooseOption"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.chooseOption(a.value(0).toString(), a.value(1).toInt())); }},
        {u"finishChoosing"_s, [](PosService &p, const QVariantList &) { return QVariant(p.finishChoosing()); }},
        {u"cancelChoosing"_s, [](PosService &p, const QVariantList &) { return QVariant(p.cancelChoosing()); }},
        {u"chooseLine"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.chooseLine(a.value(0).toLongLong())); }},
        {u"setAvailable"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.setAvailable(a.value(0).toString(), a.value(1).toBool())); }},
        {u"setCourse"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.setCourse(a.value(0).toInt())); }},
        {u"fireCourse"_s, [](PosService &p, const QVariantList &) { return QVariant(p.fireCourse()); }},
        {u"startPairing"_s, [](PosService &p, const QVariantList &) { return QVariant(p.startPairing()); }},
        {u"transferCheck"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.transferCheck(a.value(0).toString())); }},
        {u"moveCheck"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.moveCheck(a.value(0).toString())); }},
        {u"mergeCheck"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.mergeCheck(a.value(0).toLongLong())); }},
        {u"reopenCheck"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.reopenCheck(a.value(0).toLongLong())); }},
        {u"stopPairing"_s, [](PosService &p, const QVariantList &) { return QVariant(p.stopPairing()); }},
        {u"openDrawerSession"_s, [](PosService &p, const QVariantList &) { return QVariant(p.openDrawerSession()); }},
        {u"countDrawer"_s, [](PosService &p, const QVariantList &) { return QVariant(p.countDrawer()); }},
        {u"countDrawerById"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.countDrawerById(a.value(0).toLongLong())); }},
        {u"endOfDay"_s, [](PosService &p, const QVariantList &) { return QVariant(p.endOfDay()); }},
        {u"printReport"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.printReport(a.value(0).toString(), a.value(1).toLongLong())); }},
        {u"adminSave"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.adminSave(a.value(0).toString(), a.value(1).toInt(), a.value(2).toMap())); }},
        {u"adminDelete"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.adminDelete(a.value(0).toString(), a.value(1).toInt())); }},
        // Queries (remote terminals fetch these).
        {u"report"_s, [](PosService &p, const QVariantList &a) {
             return QVariant(p.report(a.value(0).toString(), a.value(1).toLongLong())); }},
        {u"adminFields"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.adminFields(a.value(0).toString())); }},
        {u"adminRecords"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.adminRecords(a.value(0).toString())); }},
        {u"adminNewRecord"_s, [](PosService &p, const QVariantList &a) { return QVariant(p.adminNewRecord(a.value(0).toString())); }},
    };
    const auto it = table.constFind(method);
    if (it == table.cend()) {
        emit notice(tr("Unknown operation '%1'").arg(method));
        if (reply)
            reply(QVariant(false));
        return;
    }
    const QVariant result = (*it)(*this, args);
    if (reply)
        reply(result);
}

} // namespace vt::app
