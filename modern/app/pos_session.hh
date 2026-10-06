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
    // A table with separate checks: [{id, number (1, 2…), total, lines, current}].
    Q_PROPERTY(QVariantList tableChecks READ tableChecks NOTIFY checkChanged)
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
    Q_PROPERTY(int screenSaverMinutes READ screenSaverMinutes NOTIFY adminChanged)
    Q_PROPERTY(QVariantList kitchenStations READ kitchenStations NOTIFY adminChanged)
    Q_PROPERTY(QString kitchenStation READ kitchenStation NOTIFY adminChanged)
    // Messages between screens, the last hour's, newest first.
    Q_PROPERTY(QVariantList messages READ messages NOTIFY dayChanged)
    Q_PROPERTY(bool standbyReady READ standbyReady NOTIFY sessionChanged)
    // This screen's language: the logged-in person's, else the store's.
    Q_PROPERTY(QString language READ language NOTIFY sessionChanged)
    // The logged-in person's own screen: {textSize (percent), leftHanded, startPage}.
    Q_PROPERTY(QVariantMap userPrefs READ userPrefs NOTIFY sessionChanged)
    // The store's: the customer display's language.
    Q_PROPERTY(QString storeLanguage READ storeLanguage NOTIFY adminChanged)
    // Self-order kiosk: {on, toGo, ordering, idleSeconds, lastOrder: {number,
    // name, sent}}; and what guests can order: {families, items: [{id, name,
    // family, price, description, image, available, choices}]}.
    Q_PROPERTY(QVariantMap selfOrder READ selfOrderInfo NOTIFY checkChanged)
    Q_PROPERTY(QVariantMap kioskMenu READ kioskMenu NOTIFY adminChanged)
    // Clocking in with more than one job: {who, jobs: [{role, name}]}, else empty.
    Q_PROPERTY(QVariantMap clockInJobs READ clockInJobs NOTIFY sessionChanged)
    // Receiving deliveries (managers): {vendors: [{id, name}], ingredients:
    // [{id, name, unit, onHand, cost, vendor}], recent: [{when, vendor,
    // invoice, items, total, by}]}.
    Q_PROPERTY(QVariantMap receiving READ receiving NOTIFY adminChanged)
    // Finding checks: {query, loading, more, results: [{id, label, when,
    // server, customer, total, status}], selected: {..., lines, payments}}.
    Q_PROPERTY(QVariantMap checkSearch READ checkSearch NOTIFY sessionChanged)
    // Manager -> Network: {role: main|single, term, terminals: [{name,
    // address, user, since}], standby: {address, since} | null, printers:
    // [{name, type, where, status: ok|failed|unknown, error, at}]}.
    Q_PROPERTY(QVariantMap network READ networkInfo NOTIFY dayChanged)
    // Modifiers being chosen for the item just ordered: {active, item, groups:
    // [{id, name, rule, chosen, done, options: [{index, name, price, chosen}]}]}.
    Q_PROPERTY(QVariantMap choosing READ choosingInfo NOTIFY checkChanged)
    Q_PROPERTY(QVariantMap weighing READ weighingInfo NOTIFY checkChanged)
    // The setup guide: {done, storeName, receiptHeader, logo, receiptLogo, foodTax,
    // alcoholTax, families, items, staff: [{name, role, sample}], samples} (managers).
    Q_PROPERTY(QVariantMap setup READ setupInfo NOTIFY adminChanged)
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
    // The store's pictures: [{name, ref ("store:logo.png"), hash, bytes, url}].
    Q_PROPERTY(QVariantList storeImages READ storeImages NOTIFY adminChanged)
    // The store's logo as a picture ref ("store:logo.png"), or "".
    Q_PROPERTY(QString storeLogo READ storeLogo NOTIFY adminChanged)
    // Changes when pictures do (bind to it with imageUrl()).
    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY adminChanged)
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
    virtual QVariantList tableChecks() const = 0;
    virtual QVariantList closedChecks() const = 0;
    virtual QVariantList staff() const = 0;
    virtual QVariantList checkHistory() const = 0;
    virtual QVariantMap choosingInfo() const = 0;
    virtual QVariantMap weighingInfo() const = 0;
    virtual QVariantMap setupInfo() const = 0;
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
    virtual QVariantList storeImages() const = 0;
    virtual QString storeLogo() const = 0;
    virtual int imageRevision() const { return adminRevision(); }
    // A picture this screen can show, as a URL: "store:logo.png" (the store's
    // pictures), "logo:" (the store's logo), a path, or qrc:/file:/http as is.
    Q_INVOKABLE virtual QString imageUrl(const QString &ref) const = 0;
    // A picture file on this computer, added to the store's pictures (managers):
    // returns its ref ("store:logo.png") at once; the store keeps it.
    Q_INVOKABLE QString addImageFile(const QString &fileOrUrl);
    Q_INVOKABLE void removeStoreImage(const QString &ref) { invoke(QStringLiteral("removeStoreImage"), {ref}); }
    // "Logo Final.PNG" -> "store:logo-final.png" ("" if not a picture type).
    static QString storeImageRef(const QString &fileName);
    virtual int screenSaverMinutes() const = 0;
    virtual QVariantList kitchenStations() const = 0;
    virtual QString kitchenStation() const = 0;
    virtual QVariantList messages() const = 0;
    // A screen that lost its server: the store's standby is there, ready.
    virtual bool standbyReady() const { return false; }
    virtual QString language() const = 0;
    virtual QVariantMap userPrefs() const = 0;
    virtual QString storeLanguage() const = 0;
    virtual QVariantMap selfOrderInfo() const = 0;
    virtual QVariantMap kioskMenu() const = 0;
    virtual QVariantMap clockInJobs() const = 0;
    virtual QVariantMap receiving() const = 0;
    virtual QVariantMap checkSearch() const = 0;
    virtual QVariantMap networkInfo() const = 0;
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
    // "server" -> "Server" (in the screen's language).
    Q_INVOKABLE QString roleName(const QString &role) const;
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
    // Another check at this table (a guest paying on their own), made current.
    // How many of a line (0: the selected one); − / + (a sent line: + is a new line); Again.
    Q_INVOKABLE void setLineQuantity(qint64 lineId, int quantity) { invoke(QStringLiteral("setLineQuantity"), {lineId, quantity}); }
    Q_INVOKABLE void lineMore(qint64 lineId = 0) { invoke(QStringLiteral("lineMore"), {lineId}); }
    Q_INVOKABLE void lineLess(qint64 lineId = 0) { invoke(QStringLiteral("lineLess"), {lineId}); }
    Q_INVOKABLE void repeatLine(qint64 lineId = 0) { invoke(QStringLiteral("repeatLine"), {lineId}); }
    Q_INVOKABLE void splitBySeat() { invoke(QStringLiteral("splitBySeat"), {}); }
    Q_INVOKABLE void printTableChecks() { invoke(QStringLiteral("printTableChecks"), {}); }
    Q_INVOKABLE void combineTableChecks() { invoke(QStringLiteral("combineTableChecks"), {}); }
    Q_INVOKABLE void newTableCheck() { invoke(QStringLiteral("newTableCheck"), {}); }
    // Another open check made current, staying on this page (the table's checks).
    Q_INVOKABLE void switchCheck(qint64 checkId) { invoke(QStringLiteral("openCheck"), {checkId}); }
    Q_INVOKABLE void setCourse(int course) { invoke(QStringLiteral("setCourse"), {course}); }
    Q_INVOKABLE void fireCourse() { invoke(QStringLiteral("fireCourse")); }
    Q_INVOKABLE void chooseOption(const QString &groupId, int index) { invoke(QStringLiteral("chooseOption"), {groupId, index}); }
    Q_INVOKABLE void finishChoosing() { invoke(QStringLiteral("finishChoosing")); }
    Q_INVOKABLE void cancelChoosing() { invoke(QStringLiteral("cancelChoosing")); }
    Q_INVOKABLE void backupNow() { invoke(QStringLiteral("backupNow")); }
    Q_INVOKABLE void leaveSelfOrder(const QString &pin) { invoke(QStringLiteral("leaveSelfOrder"), {pin}); }
    Q_INVOKABLE void clockInAs(const QString &role) { invoke(QStringLiteral("clockInAs"), {role}); }
    Q_INVOKABLE void setExpenseCategory(const QString &category) { invoke(QStringLiteral("setExpenseCategory"), {category}); }
    Q_INVOKABLE void receiveDelivery(const QVariantMap &delivery) { invoke(QStringLiteral("receiveDelivery"), {delivery}); }
    Q_INVOKABLE void searchChecks(const QString &query) { invoke(QStringLiteral("searchChecks"), {query}); }
    // Orders for later: ready at this time (ms since 1970; 0 = as soon as possible).
    Q_INVOKABLE void setDueAt(double at) { invoke(QStringLiteral("setDueAt"), {qint64(at)}); }
    Q_INVOKABLE void setKitchenStation(const QString &id) { invoke(QStringLiteral("setKitchenStation"), {id}); }
    // The setup guide's steps.
    Q_INVOKABLE void setupStore(const QString &name, const QString &lines) { invoke(QStringLiteral("setupStore"), {name, lines}); }
    Q_INVOKABLE void setupLogo(const QString &ref, bool onReceipts) { invoke(QStringLiteral("setupLogo"), {ref, onReceipts}); }
    Q_INVOKABLE void setupTaxes(double food, double alcohol) { invoke(QStringLiteral("setupTaxes"), {food, alcohol}); }
    Q_INVOKABLE void setupAddItem(const QString &name, double price, const QString &family)
    {
        invoke(QStringLiteral("setupAddItem"), {name, price, family});
    }
    Q_INVOKABLE void setupAddEmployee(const QString &name, const QString &role, const QString &pin)
    {
        invoke(QStringLiteral("setupAddEmployee"), {name, role, pin});
    }
    Q_INVOKABLE void setupRetireSamples() { invoke(QStringLiteral("setupRetireSamples")); }
    Q_INVOKABLE void setupFinish(bool done) { invoke(QStringLiteral("setupFinish"), {done}); }
    Q_INVOKABLE void selectFoundCheck(qint64 id) { invoke(QStringLiteral("selectFoundCheck"), {id}); }
    Q_INVOKABLE void reprintCheck(qint64 id) { invoke(QStringLiteral("reprintCheck"), {id}); }
    Q_INVOKABLE void cancelClockIn() { invoke(QStringLiteral("cancelClockIn")); }
    Q_INVOKABLE void kioskStart(bool toGo) { invoke(QStringLiteral("kioskStart"), {toGo}); }
    Q_INVOKABLE void kioskAdd(const QString &itemId) { invoke(QStringLiteral("kioskAdd"), {itemId}); }
    Q_INVOKABLE void kioskRemove(qint64 lineId) { invoke(QStringLiteral("kioskRemove"), {lineId}); }
    Q_INVOKABLE void kioskFinish(const QVariantMap &guest) { invoke(QStringLiteral("kioskFinish"), {guest}); }
    Q_INVOKABLE void kioskCancel() { invoke(QStringLiteral("kioskCancel")); }
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
    // Ask the standby server to take over (a manager's PIN). Only screens
    // of a remote store have one.
    Q_INVOKABLE virtual void takeOver(const QString &pin) { Q_UNUSED(pin) }
    Q_INVOKABLE void sendMessage(const QString &to, const QString &text)
    {
        invoke(QStringLiteral("sendMessage"), {to, text});
    }
    // Posted until `until` (ms since 1970): shown to everyone it's for until then.
    Q_INVOKABLE void postMessage(const QString &to, const QString &text, double until)
    {
        invoke(QStringLiteral("sendMessage"), {to, text, qint64(until)});
    }
    Q_INVOKABLE void removeMessage(const QString &id) { invoke(QStringLiteral("removeMessage"), {id}); }
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
