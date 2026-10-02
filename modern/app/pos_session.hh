#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace vt::app {

// What pages and widgets see of a POS terminal session. Implemented by
// PosService (in-process) and RemoteSession (a terminal talking to a
// server), so the same QML runs on both.
//
// Operations go through invoke(): a local session answers before invoke()
// returns; a remote one answers when the server replies. Either way the UI
// thread never waits. Results are reported with `notice`.
class PosSession : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString terminalName READ terminalName CONSTANT)
    // False while a remote terminal has lost its server (always true locally).
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY sessionChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY sessionChanged)
    Q_PROPERTY(QString userRole READ userRole NOTIFY sessionChanged)
    Q_PROPERTY(QStringList permissions READ permissions NOTIFY sessionChanged)
    Q_PROPERTY(bool clockedIn READ clockedIn NOTIFY sessionChanged)
    Q_PROPERTY(QString clockedInSince READ clockedInSince NOTIFY sessionChanged)
    Q_PROPERTY(QString storeName READ storeName NOTIFY adminChanged)
    Q_PROPERTY(QString currencySymbol READ currencySymbol NOTIFY adminChanged)

    Q_PROPERTY(int pinLength READ pinLength NOTIFY entryChanged)
    Q_PROPERTY(QString entry READ entry NOTIFY entryChanged)
    Q_PROPERTY(QString entryAmount READ entryAmount NOTIFY entryChanged)
    Q_PROPERTY(int entryGuests READ entryGuests NOTIFY entryChanged)
    Q_PROPERTY(QString textEntry READ textEntry NOTIFY entryChanged)
    Q_PROPERTY(QString pendingQualifier READ pendingQualifier NOTIFY qualifierChanged)
    Q_PROPERTY(QString pendingTable READ pendingTable NOTIFY checkChanged)

    Q_PROPERTY(bool hasCheck READ hasCheck NOTIFY checkChanged)
    Q_PROPERTY(QVariantMap check READ checkInfo NOTIFY checkChanged)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY checkChanged)
    Q_PROPERTY(QVariantMap totals READ totals NOTIFY checkChanged)
    Q_PROPERTY(QVariantList payments READ payments NOTIFY checkChanged)
    Q_PROPERTY(qint64 selectedLine READ selectedLine WRITE selectLine NOTIFY checkChanged)
    Q_PROPERTY(qint64 selectedPayment READ selectedPayment WRITE selectPayment NOTIFY checkChanged)
    Q_PROPERTY(QVariantList openChecks READ openChecks NOTIFY openChecksChanged)
    // Checks closed today, newest first (managers, to reopen one).
    Q_PROPERTY(QVariantList closedChecks READ closedChecks NOTIFY dayChanged)
    // Active employees [{id, name, role, clockedIn, me}], to transfer checks to.
    Q_PROPERTY(QVariantList staff READ staff NOTIFY sessionChanged)
    // When the logged-in person's break began ("" when not on one).
    Q_PROPERTY(QString onBreakSince READ onBreakSince NOTIFY sessionChanged)
    // Customer search results, the chosen customer, the gift card looked up.
    Q_PROPERTY(QVariantList customers READ customerResults NOTIFY checkChanged)
    Q_PROPERTY(QVariantMap customer READ customerInfo NOTIFY checkChanged)
    Q_PROPERTY(QVariantMap giftCard READ giftCardInfo NOTIFY checkChanged)
    // The host stand: {waiting, booked, seatedToday, averageWait, noShows, nextQuote}.
    Q_PROPERTY(QVariantMap waitlist READ waitlistInfo NOTIFY dayChanged)
    // What the customer display asks the guest: {askingTip, tipChosen, tip, choices}.
    Q_PROPERTY(QVariantMap customerPrompt READ customerPrompt NOTIFY checkChanged)
    // The week's schedule ({title, days, totals, staff}) and your next shift.
    Q_PROPERTY(QVariantMap schedule READ scheduleInfo NOTIFY sessionChanged)
    Q_PROPERTY(QString nextShift READ nextShift NOTIFY sessionChanged)
    // A report over several days, when it has been read: {loading, label, report}.
    Q_PROPERTY(QVariantMap rangeReport READ rangeReport NOTIFY sessionChanged)
    // Waiting for a manager's PIN: {needed, action, who}.
    Q_PROPERTY(QVariantMap approval READ approvalInfo NOTIFY sessionChanged)
    // This screen's checks are practice (training).
    Q_PROPERTY(bool training READ training NOTIFY sessionChanged)
    Q_PROPERTY(int autoLogoutMinutes READ autoLogoutMinutes NOTIFY adminChanged)
    // Messages between screens, the last hour's, newest first.
    Q_PROPERTY(QVariantList messages READ messages NOTIFY dayChanged)
    // Modifiers being chosen for the item just ordered: {active, item, groups:
    // [{id, name, rule, chosen, done, options: [{index, name, price, chosen}]}]}.
    Q_PROPERTY(QVariantMap choosing READ choosingInfo NOTIFY checkChanged)
    // Sold-out (86'd) items: their ids and lower-case names.
    Q_PROPERTY(QStringList soldOut READ soldOut NOTIFY adminChanged)
    // The menu for the 86 list [{id, name, family, price, modifier, available}].
    Q_PROPERTY(QVariantList menuItems READ menuItems NOTIFY adminChanged)
    // The current check's history [{time, who, what}].
    Q_PROPERTY(QVariantList checkHistory READ checkHistory NOTIFY checkChanged)
    Q_PROPERTY(QString checkFilter READ checkFilter WRITE setCheckFilter NOTIFY openChecksChanged)
    Q_PROPERTY(QVariantList kitchenTickets READ kitchenTickets NOTIFY kitchenChanged)
    Q_PROPERTY(QVariantList expoTickets READ expoTickets NOTIFY kitchenChanged)
    Q_PROPERTY(QVariantMap drawer READ drawerInfo NOTIFY drawerChanged)
    Q_PROPERTY(QVariantMap day READ dayInfo NOTIFY dayChanged)
    Q_PROPERTY(QVariantList days READ days NOTIFY dayChanged)
    Q_PROPERTY(int adminRevision READ adminRevision NOTIFY adminChanged)
    // The logged-in employee's card tips + gratuity not yet paid out.
    Q_PROPERTY(QString tipsOwed READ tipsOwed NOTIFY dayChanged)
    // This terminal's Screen layout setting: "phone", "standard" or "" (automatic).
    Q_PROPERTY(QString screenMode READ screenMode NOTIFY adminChanged)
    // A device pairing in progress (managers only): {active, code, until}.
    Q_PROPERTY(QVariantMap pairing READ pairingInfo NOTIFY adminChanged)
    // The store's meal periods: [{id, name, start (minutes after midnight)}].
    Q_PROPERTY(QVariantList mealPeriods READ mealPeriods NOTIFY adminChanged)
    // Bumps when an asynchronous query (report, admin records) has an answer.
    Q_PROPERTY(int queryRevision READ queryRevision NOTIFY queriesChanged)

public:
    // selectTable results.
    enum TableResult { TableFailed = 0, TableOpened = 1, TableNeedsGuests = 2, TableChooseCheck = 3 };
    Q_ENUM(TableResult)

    using Reply = std::function<void(const QVariant &result)>;

    using QObject::QObject;

    // Run an operation by name (see PosService::invoke for the list).
    virtual void invoke(const QString &method, const QVariantList &args = {}, Reply reply = {}) = 0;

    virtual QString terminalName() const = 0;
    virtual bool online() const { return true; }
    virtual bool loggedIn() const = 0;
    virtual QString userName() const = 0;
    virtual QString userRole() const = 0;
    virtual QStringList permissions() const = 0;
    virtual bool clockedIn() const = 0;
    virtual QString clockedInSince() const = 0;
    virtual QString storeName() const = 0;
    virtual QString currencySymbol() const = 0;
    virtual int pinLength() const = 0;
    virtual QString entry() const = 0;
    virtual QString entryAmount() const = 0;
    virtual int entryGuests() const = 0;
    virtual QString textEntry() const = 0;
    virtual QString pendingQualifier() const = 0;
    virtual QString pendingTable() const = 0;
    virtual bool hasCheck() const = 0;
    virtual QVariantMap checkInfo() const = 0;
    virtual QVariantList lines() const = 0;
    virtual QVariantMap totals() const = 0;
    virtual QVariantList payments() const = 0;
    virtual qint64 selectedLine() const = 0;
    virtual qint64 selectedPayment() const = 0;
    virtual QVariantList openChecks() const = 0;
    virtual QVariantList closedChecks() const = 0;
    virtual QVariantList staff() const = 0;
    virtual QVariantList checkHistory() const = 0;
    virtual QVariantMap choosingInfo() const = 0;
    virtual QString onBreakSince() const = 0;
    virtual QVariantList customerResults() const = 0;
    virtual QVariantMap customerInfo() const = 0;
    virtual QVariantMap giftCardInfo() const = 0;
    virtual QVariantMap waitlistInfo() const = 0;
    virtual QVariantMap customerPrompt() const = 0;
    virtual QVariantMap scheduleInfo() const = 0;
    virtual QString nextShift() const = 0;
    virtual QVariantMap rangeReport() const = 0;
    virtual QVariantMap approvalInfo() const = 0;
    virtual int autoLogoutMinutes() const = 0;
    virtual QVariantList messages() const = 0;
    virtual bool training() const = 0;
    virtual QStringList soldOut() const = 0;
    virtual QVariantList menuItems() const = 0;
    virtual QString checkFilter() const = 0;
    virtual QVariantList kitchenTickets() const = 0;
    virtual QVariantList expoTickets() const = 0;
    virtual QVariantMap drawerInfo() const = 0;
    virtual QVariantMap dayInfo() const = 0;
    virtual QVariantList days() const = 0;
    virtual int adminRevision() const = 0;
    virtual QString tipsOwed() const = 0;
    virtual QVariantList mealPeriods() const = 0;
    virtual QVariantMap pairingInfo() const = 0;
    virtual QString screenMode() const = 0;
    virtual int queryRevision() const { return 0; }

    virtual void selectLine(qint64 lineId) { invoke(QStringLiteral("selectLine"), {lineId}); }
    virtual void selectPayment(qint64 paymentId) { invoke(QStringLiteral("selectPayment"), {paymentId}); }
    virtual void setCheckFilter(const QString &label) { invoke(QStringLiteral("setCheckFilter"), {label}); }

    // --- queries ---------------------------------------------------------------
    Q_INVOKABLE bool can(const QString &permission) const { return permissions().contains(permission); }
    // Status of a table on the floor plan, from the open checks.
    Q_INVOKABLE QVariantMap tableStatus(const QString &label) const;
    // Other open checks at the current check's table, plus {id: 0, "New check"}.
    Q_INVOKABLE QVariantList splitTargets() const;
    // Reports and admin screens. Remote sessions answer from a cache and
    // bump queryRevision when the server's answer arrives.
    Q_INVOKABLE virtual QVariantMap report(const QString &id, qint64 dayId = 0) = 0;
    Q_INVOKABLE virtual QVariantList adminFields(const QString &panel) = 0;
    Q_INVOKABLE virtual QVariantList adminRecords(const QString &panel) = 0;
    Q_INVOKABLE virtual QVariantMap adminNewRecord(const QString &panel) = 0;

    QString formatCents(qint64 cents) const;

    // Every state property by name, as sent to remote terminals.
    QVariantMap snapshot() const;
    static const QStringList &stateKeys();

    // --- touch conveniences for widgets (fire and forget) ------------------------
    Q_INVOKABLE void pinKey(const QString &key) { invoke(QStringLiteral("pinKey"), {key}); }
    Q_INVOKABLE void entryKey(const QString &key) { invoke(QStringLiteral("entryKey"), {key}); }
    Q_INVOKABLE void adjustGuests(int delta) { invoke(QStringLiteral("adjustGuests"), {delta}); }
    Q_INVOKABLE void textKey(const QString &key) { invoke(QStringLiteral("textKey"), {key}); }
    Q_INVOKABLE void splitLine(qint64 targetCheckId) { invoke(QStringLiteral("splitLine"), {targetCheckId}); }
    Q_INVOKABLE void printReceipt() { invoke(QStringLiteral("printReceipt")); }
    Q_INVOKABLE void noSale() { invoke(QStringLiteral("noSale")); }
    Q_INVOKABLE void openDrawerSession() { invoke(QStringLiteral("openDrawerSession")); }
    Q_INVOKABLE void countDrawer() { invoke(QStringLiteral("countDrawer")); }
    Q_INVOKABLE void countDrawerById(qint64 drawerId) { invoke(QStringLiteral("countDrawerById"), {drawerId}); }
    Q_INVOKABLE void endOfDay() { invoke(QStringLiteral("endOfDay")); }
    Q_INVOKABLE void printReport(const QString &id, qint64 dayId = 0) { invoke(QStringLiteral("printReport"), {id, dayId}); }
    Q_INVOKABLE void adminSave(const QString &panel, int index, const QVariantMap &record)
    {
        invoke(QStringLiteral("adminSave"), {panel, index, record});
    }
    Q_INVOKABLE void adminDelete(const QString &panel, int index) { invoke(QStringLiteral("adminDelete"), {panel, index}); }
    // station: only that printer's lines ("kitchen", "bar"); empty = all.
    Q_INVOKABLE void bumpTicket(qint64 checkId, qint64 sentAt, const QString &station = {})
    {
        invoke(QStringLiteral("bumpTicket"), {checkId, sentAt, station});
    }
    Q_INVOKABLE void recallTicket() { invoke(QStringLiteral("recallTicket")); }
    Q_INVOKABLE void setCustomer(const QVariantMap &customer) { invoke(QStringLiteral("setCustomer"), {customer}); }
    Q_INVOKABLE void setSeat(int seat) { invoke(QStringLiteral("setSeat"), {seat}); }
    Q_INVOKABLE void setCourse(int course) { invoke(QStringLiteral("setCourse"), {course}); }
    Q_INVOKABLE void fireCourse() { invoke(QStringLiteral("fireCourse")); }
    Q_INVOKABLE void chooseOption(const QString &groupId, int index) { invoke(QStringLiteral("chooseOption"), {groupId, index}); }
    Q_INVOKABLE void finishChoosing() { invoke(QStringLiteral("finishChoosing")); }
    Q_INVOKABLE void cancelChoosing() { invoke(QStringLiteral("cancelChoosing")); }
    Q_INVOKABLE void backupNow() { invoke(QStringLiteral("backupNow")); }
    Q_INVOKABLE void factoryReset(const QString &confirm) { invoke(QStringLiteral("factoryReset"), {confirm}); }
    Q_INVOKABLE void findCustomers(const QString &query) { invoke(QStringLiteral("findCustomers"), {query}); }
    Q_INVOKABLE void selectCustomer(const QString &id) { invoke(QStringLiteral("selectCustomer"), {id}); }
    Q_INVOKABLE void useCustomer(const QString &id = {}) { invoke(QStringLiteral("useCustomer"), {id}); }
    Q_INVOKABLE void saveCustomer(const QVariantMap &record) { invoke(QStringLiteral("saveCustomer"), {record}); }
    Q_INVOKABLE void sellGiftCard(const QString &number, qint64 amountCents = 0)
    {
        invoke(QStringLiteral("sellGiftCard"), {number, amountCents});
    }
    Q_INVOKABLE void lookupGiftCard(const QString &number) { invoke(QStringLiteral("lookupGiftCard"), {number}); }
    Q_INVOKABLE void payWithGiftCard(const QString &number = {}, qint64 amountCents = 0)
    {
        invoke(QStringLiteral("payWithGiftCard"), {number, amountCents});
    }
    Q_INVOKABLE void askForTip() { invoke(QStringLiteral("askForTip")); }
    Q_INVOKABLE void approve(const QString &pin) { invoke(QStringLiteral("approve"), {pin}); }
    Q_INVOKABLE void sendMessage(const QString &to, const QString &text)
    {
        invoke(QStringLiteral("sendMessage"), {to, text});
    }
    Q_INVOKABLE void cancelApproval() { invoke(QStringLiteral("cancelApproval")); }
    Q_INVOKABLE void setTraining(bool on) { invoke(QStringLiteral("setTraining"), {on}); }
    Q_INVOKABLE void redeemReward(int index) { invoke(QStringLiteral("redeemReward"), {index}); }
    Q_INVOKABLE void customerJoin(const QString &phone) { invoke(QStringLiteral("customerJoin"), {phone}); }
    Q_INVOKABLE void sendReceipt(const QString &how, const QString &to = {})
    {
        invoke(QStringLiteral("sendReceipt"), {how, to});
    }
    Q_INVOKABLE void expoBump(qint64 checkId, qint64 sentAt) { invoke(QStringLiteral("expoBump"), {checkId, sentAt}); }
    Q_INVOKABLE void expoRecall() { invoke(QStringLiteral("expoRecall")); }
    Q_INVOKABLE void requestRangeReport(const QString &id, const QString &period, const QString &from = {},
                                        const QString &to = {}, bool compare = false)
    {
        invoke(QStringLiteral("requestRangeReport"), {id, period, from, to, compare});
    }
    Q_INVOKABLE void addShift(const QVariantMap &shift) { invoke(QStringLiteral("addShift"), {shift}); }
    Q_INVOKABLE void removeShift(qint64 id) { invoke(QStringLiteral("removeShift"), {id}); }
    Q_INVOKABLE void clockInEmployee(const QString &employeeId) { invoke(QStringLiteral("clockInEmployee"), {employeeId}); }
    Q_INVOKABLE void setScheduleWeek(int offset) { invoke(QStringLiteral("setScheduleWeek"), {offset}); }
    // kind: "percent" (value 1800 = 18%) | "amount" (cents) | "none"
    Q_INVOKABLE void customerTip(const QString &kind, qint64 value = 0)
    {
        invoke(QStringLiteral("customerTip"), {kind, value});
    }
    Q_INVOKABLE void addToWaitlist(const QVariantMap &party) { invoke(QStringLiteral("addToWaitlist"), {party}); }
    Q_INVOKABLE void addReservation(const QVariantMap &party) { invoke(QStringLiteral("addReservation"), {party}); }
    Q_INVOKABLE void updateParty(qint64 id, const QVariantMap &changes)
    {
        invoke(QStringLiteral("updateParty"), {id, changes});
    }
    Q_INVOKABLE void checkInParty(qint64 id) { invoke(QStringLiteral("checkInParty"), {id}); }
    Q_INVOKABLE void notifyParty(qint64 id) { invoke(QStringLiteral("notifyParty"), {id}); }
    Q_INVOKABLE void seatParty(qint64 id, const QString &table, const QString &serverId = {})
    {
        invoke(QStringLiteral("seatParty"), {id, table, serverId});
    }
    Q_INVOKABLE void partyGone(qint64 id, bool noShow = false) { invoke(QStringLiteral("partyGone"), {id, noShow}); }
    Q_INVOKABLE void payOnAccount(const QString &method, qint64 amountCents = 0)
    {
        invoke(QStringLiteral("payOnAccount"), {method, amountCents});
    }
    Q_INVOKABLE void chooseLine(qint64 lineId) { invoke(QStringLiteral("chooseLine"), {lineId}); }
    Q_INVOKABLE void setAvailable(const QString &itemId, bool available) { invoke(QStringLiteral("setAvailable"), {itemId, available}); }
    Q_INVOKABLE void transferCheck(const QString &employeeId) { invoke(QStringLiteral("transferCheck"), {employeeId}); }
    Q_INVOKABLE void moveCheck(const QString &table) { invoke(QStringLiteral("moveCheck"), {table}); }
    Q_INVOKABLE void mergeCheck(qint64 otherId) { invoke(QStringLiteral("mergeCheck"), {otherId}); }
    Q_INVOKABLE void reopenCheck(qint64 checkId) { invoke(QStringLiteral("reopenCheck"), {checkId}); }
    // percent: 15, 18, 20...; 0 = the keypad amount.
    Q_INVOKABLE void addTip(double percent) { invoke(QStringLiteral("addTip"), {qint64(percent * 100 + 0.5)}); }
    Q_INVOKABLE void setGratuity(double percent) { invoke(QStringLiteral("setGratuity"), {qint64(percent * 100 + 0.5)}); }
    // kind: "payout" | "paidIn"
    Q_INVOKABLE void payout(const QString &kind) { invoke(QStringLiteral("payout"), {kind}); }
    Q_INVOKABLE void cashOutTips() { invoke(QStringLiteral("cashOutTips")); }
    Q_INVOKABLE void startPairing() { invoke(QStringLiteral("startPairing")); }
    Q_INVOKABLE void stopPairing() { invoke(QStringLiteral("stopPairing")); }

signals:
    void sessionChanged();
    void entryChanged();
    void qualifierChanged();
    void checkChanged();
    void openChecksChanged();
    void kitchenChanged();
    void drawerChanged();
    void dayChanged();
    void adminChanged();
    void queriesChanged();
    void onlineChanged();
    void notice(const QString &message);
    void loggedInChanged(bool loggedIn);
    void checkClosed(qint64 checkId);
};

} // namespace vt::app
