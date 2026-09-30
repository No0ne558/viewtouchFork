#pragma once

#include "core/check.hh"
#include "core/day.hh"
#include "core/employee.hh"
#include "core/menu.hh"
#include "core/report.hh"
#include "core/settings.hh"
#include "app/pos_session.hh"

#include <QJsonObject>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace vt::app {

// Where the service hands off changes. Implementations must not block: the
// SQLite one queues writes to a worker thread.
class PosSink {
public:
    virtual ~PosSink() = default;
    virtual void saveCheck(const core::Check &check) = 0;
    virtual void savePunch(const core::TimePunch &punch) = 0;
    // `reports` holds the day's final reports (set when the day is closed).
    virtual void saveDay(const core::BusinessDay &, const QJsonObject &reports) { Q_UNUSED(reports) }
    virtual void saveDrawer(const core::DrawerSession &) {}
    virtual void saveSettings(const core::PosSettings &) {}
    virtual void saveMenuItem(const core::MenuItem &, int position) { Q_UNUSED(position) }
    virtual void deleteMenuItem(const std::string &id) { Q_UNUSED(id) }
    virtual void saveEmployee(const core::Employee &) {}
};

// Where tickets go. Implementations must not block (see print::PrintSpooler).
class PosPrinter {
public:
    virtual ~PosPrinter() = default;
    // `voids`: the lines were cancelled after being sent.
    virtual void printKitchen(const core::PosSettings &settings, const core::Check &check,
                              const std::vector<core::OrderLine> &lines, bool voids) = 0;
    // `printerId`: the terminal's receipt printer (see PosSettings::receiptPrinterFor).
    virtual void printReceipt(const core::PosSettings &settings, const core::Check &check,
                              const std::string &printerId) = 0;
    virtual void printReport(const core::PosSettings &settings, const core::Report &report,
                             const std::string &printerId) = 0;
    virtual void openDrawer(const core::PosSettings &settings, const std::string &printerId) = 0;
};

// A closed business day and its final reports (report id -> report JSON).
struct PastDay {
    core::BusinessDay day;
    QJsonObject reports;
};

// Everything the running POS needs at startup.
struct PosData {
    core::PosSettings settings;
    std::vector<core::MenuItem> menu;
    std::vector<core::Employee> employees;
    std::vector<core::Check> openChecks;
    std::vector<core::TimePunch> punches;        // today's, plus any still open
    std::int64_t lastCheckId = 0;
    std::int64_t lastPunchId = 0;
    std::optional<core::BusinessDay> currentDay;  // none: the service opens one
    std::int64_t lastDayId = 0;
    std::vector<core::Check> closedToday;
    std::vector<core::DrawerSession> drawers;     // today's, plus any still open
    std::int64_t lastDrawerId = 0;
    std::vector<PastDay> pastDays;                // newest first
};

// State shared by every terminal of one store: open checks, today's closed
// checks, menu, staff, settings, the drawer and business day, and which
// terminal holds which check. Sessions change it and it tells all of them.
class PosShared : public QObject {
    Q_OBJECT

public:
    PosShared(PosData data, PosSink *sink, QObject *parent = nullptr);

    std::int64_t now() const { return now_(); }
    void setClock(std::function<std::int64_t()> now) { now_ = std::move(now); }

    const core::Employee *employee(const std::string &id) const;

    core::PosSettings settings;
    std::vector<core::MenuItem> menu;
    std::vector<core::Employee> employees;
    std::map<std::int64_t, core::Check> open;
    std::vector<core::TimePunch> punches;   // today's, plus any still open
    std::int64_t lastCheckId = 0;
    std::int64_t lastPunchId = 0;
    PosSink *sink = nullptr;
    PosPrinter *printer = nullptr;

    core::BusinessDay day;
    std::int64_t lastDayId = 0;
    std::vector<core::Check> closedToday;
    std::vector<core::DrawerSession> drawers;   // drawers and server banks; today's plus open ones
    std::int64_t lastDrawerId = 0;
    std::vector<PastDay> pastDays;

    // The terminal's open drawer / its most recent one today.
    core::DrawerSession *openDrawerFor(const std::string &terminal);
    const core::DrawerSession *latestDrawerFor(const std::string &terminal) const;
    // An employee's open server bank / their most recent one today.
    core::DrawerSession *openBankFor(const std::string &employeeId);
    const core::DrawerSession *latestBankFor(const std::string &employeeId) const;
    int adminRevision = 0;

    // Check locks: a check open on one terminal cannot be opened on another.
    std::map<std::int64_t, const QObject *> lockedBy;
    // Bumped kitchen tickets, newest last, for Recall.
    struct Bump { std::int64_t checkId; std::int64_t sentAt; std::string station; };
    std::vector<Bump> bumped;

    void startDay();

signals:
    void checksChanged();    // any check: open, closed, made
    void dayChanged();
    void drawerChanged();
    void adminChanged();     // menu, settings, tenders, printers, taxes
    void staffChanged();

private:
    std::function<std::int64_t()> now_;
};

// One terminal's POS session: who is logged in, the check being worked on,
// the keypad entry, and every order/payment operation, over the shared
// store. Pages reach it through zone actions and widgets (PosSession API);
// C++ callers and tests may use the typed methods directly.
class PosService : public PosSession {
    Q_OBJECT

public:
    // Single-terminal convenience: owns its own shared state.
    PosService(PosData data, PosSink *sink, QObject *parent = nullptr);
    // One of several terminals on a shared store.
    PosService(PosShared *shared, QString terminalName, QObject *parent = nullptr);
    ~PosService() override;

    PosShared *shared() const { return s_; }
    void setPrinter(PosPrinter *printer) { s_->printer = printer; }
    // Test hook: replace the wall clock (epoch ms) for the shared store.
    void setClock(std::function<std::int64_t()> now) { s_->setClock(std::move(now)); }

    void invoke(const QString &method, const QVariantList &args = {}, Reply reply = {}) override;

    const core::PosSettings &settings() const { return s_->settings; }
    const core::Check *currentCheck() const;
    const core::Employee *user() const;
    const core::MenuItem *findItem(const QString &idOrName) const;
    QString format(Money amount) const;

    // --- session -----------------------------------------------------------
    void pinKey(const QString &key);   // "0".."9", "clear", "back"
    bool login();                      // with the PIN entered
    bool loginWithPin(const QString &pin);
    void logout();
    bool clockIn();                    // logged-in user, else PIN entered
    bool clockOut();

    // --- keypads -------------------------------------------------------------
    void entryKey(const QString &key);   // digits, "00", "clear", "back"
    void adjustGuests(int delta);
    void textKey(const QString &key);    // characters, "space", "back", "clear"
    void clearEntry();

    // --- checks ---------------------------------------------------------------
    TableResult selectTable(const QString &label);
    bool startCheck(core::CheckType type);   // dine-in uses the pending table + guest entry
    bool openCheck(std::int64_t checkId);
    void releaseCheck();

    bool addItem(const QString &idOrName);
    void setQualifier(const QString &qualifier);   // same one again clears it
    void selectLine(qint64 lineId) override;
    void selectPayment(qint64 paymentId) override;
    bool voidItem();
    bool sendOrder();
    bool addComment();
    // Amount: explicit cents, else the keypad entry, else the balance due.
    bool tender(const QString &tenderId, std::optional<std::int64_t> amountCents = std::nullopt);
    bool removePayment();
    bool closeCheck();
    // The current check, else the last one closed on this terminal.
    bool printReceipt();
    bool noSale();   // open the cash drawer without a sale
    bool setCustomer(const QVariantMap &customer);

    // --- tips, gratuity, cash in and out ------------------------------------------
    // Tip on the selected card payment (else the last one): a percentage of
    // the check (bp, e.g. 1800) or, with percentBp 0, the keypad amount.
    bool addTip(std::int64_t percentBp);
    // Gratuity on the current check (bp; 0 removes). Removing an automatic
    // one needs a manager.
    bool setGratuity(std::int64_t percentBp);
    // Pay out of / into this terminal's drawer: keypad amount, typed reason.
    bool payout(core::CashMovement::Kind kind);
    // Pay the logged-in employee the tips they are owed, from this drawer.
    bool cashOutTips();

    // --- split check -------------------------------------------------------------
    // Move the selected line to another check (0 = a new one at the table).
    bool splitLine(qint64 targetCheckId);
    QString checkFilter() const override { return checkFilter_; }
    void setCheckFilter(const QString &label) override;

    // --- kitchen display -----------------------------------------------------------
    // Mark the lines sent at `sentAt` on the check as made; with a station,
    // only the lines printed there (a bar screen leaves kitchen lines alone).
    bool bumpTicket(qint64 checkId, qint64 sentAt, const QString &station = {});
    bool recallTicket();   // undo the latest bump

    // --- drawer and business day ---------------------------------------------------
    // "My drawer" is this terminal's drawer, or with server banks the
    // logged-in employee's own bank.
    bool openDrawerSession();   // starting cash from the keypad entry
    bool countDrawer();         // counted cash from the keypad entry (bank: check out)
    // Manager: count someone else's drawer or bank (a server who left).
    bool countDrawerById(qint64 drawerId);
    bool endOfDay();
    const core::BusinessDay &currentDay() const { return s_->day; }
    const std::vector<core::Check> &closedToday() const { return s_->closedToday; }

    // --- reports -------------------------------------------------------------------
    // id: sales | items | servers | labor | drawer. dayId 0 = today (live).
    QVariantMap report(const QString &id, qint64 dayId = 0) override;
    bool printReport(const QString &id, qint64 dayId = 0);
    core::Report buildReport(const QString &id) const;

    // --- admin (manager) -------------------------------------------------------------
    // panel: menu | employees | tenders | printers | taxes | store
    QVariantList adminFields(const QString &panel) override;
    QVariantList adminRecords(const QString &panel) override;
    QVariantMap adminNewRecord(const QString &panel) override;
    // index -1 adds a record. Returns false (with a notice) when invalid.
    bool adminSave(const QString &panel, int index, const QVariantMap &record);
    bool adminDelete(const QString &panel, int index);
    const std::vector<core::MenuItem> &menu() const { return s_->menu; }
    const std::vector<core::Employee> &employees() const { return s_->employees; }

    // --- PosSession state ------------------------------------------------------------
    QString terminalName() const override { return terminal_; }
    bool loggedIn() const override { return user() != nullptr; }
    QString userName() const override;
    QString userRole() const override;
    QStringList permissions() const override;
    bool clockedIn() const override;
    QString clockedInSince() const override;
    QString storeName() const override;
    QString currencySymbol() const override;
    int pinLength() const override { return int(pin_.size()); }
    QString entry() const override { return entry_; }
    QString entryAmount() const override;
    int entryGuests() const override;
    QString textEntry() const override { return text_; }
    QString pendingQualifier() const override;
    QString pendingTable() const override { return pendingTable_; }
    bool hasCheck() const override { return currentCheck() != nullptr; }
    QVariantMap checkInfo() const override;
    QVariantList lines() const override;
    QVariantMap totals() const override;
    QVariantList payments() const override;
    qint64 selectedLine() const override { return selectedLine_; }
    qint64 selectedPayment() const override { return selectedPayment_; }
    QVariantList openChecks() const override;
    QVariantList kitchenTickets() const override;
    QVariantMap drawerInfo() const override;
    QVariantMap dayInfo() const override;
    QVariantList days() const override;
    int adminRevision() const override { return s_->adminRevision; }
    QString tipsOwed() const override;
    QVariantList mealPeriods() const override;

private:
    std::string receiptPrinter() const { return s_->settings.receiptPrinterFor(terminal_.toStdString()); }
    bool serverBank() const { return s_->settings.cashMode == core::CashMode::ServerBank; }
    core::DrawerSession *myDrawer();
    const core::DrawerSession *latestMyDrawer() const;
    // Server banks: my open bank, started at `start` if I have none.
    core::DrawerSession *ensureMyBank(Money start = {});
    bool closeDrawer(core::DrawerSession &d);   // counted = keypad entry
    Money expectedNow(const core::DrawerSession &d) const;
    void connectShared();
    core::Check *current();
    bool require(const char *permission, const QString &action);
    bool fail(const QString &message);
    void changed(core::Check &check);   // persist + notify
    const core::Employee *employeeByPin(const QString &pin) const;
    core::TimePunch *openPunch(const std::string &employeeId);
    std::int64_t now() const { return s_->now(); }
    bool lockCheck(std::int64_t checkId);   // false: open on another terminal
    void unlockCheck(std::int64_t checkId);
    QString lockHolder(std::int64_t checkId) const;
    core::ReportContext reportContext(const QString &period) const;
    QString dayLabel(const core::BusinessDay &day) const;
    bool saveMenuRecord(int index, const QVariantMap &record);
    bool saveEmployeeRecord(int index, const QVariantMap &record);
    bool saveTenderRecord(int index, const QVariantMap &record);
    bool saveMealPeriodRecord(int index, const QVariantMap &record);
    bool savePrinterRecord(int index, const QVariantMap &record);
    void settingsChanged();

    PosShared *s_;
    std::unique_ptr<PosShared> owned_;
    QString terminal_;

    std::string userId_;
    std::int64_t currentId_ = 0;
    qint64 selectedLine_ = 0;
    qint64 selectedPayment_ = 0;
    core::Qualifier qualifier_ = core::Qualifier::None;
    std::int64_t lastClosedId_ = 0;
    QString checkFilter_;
    QString pin_;
    QString entry_;
    QString text_;
    QString pendingTable_;
};

} // namespace vt::app
