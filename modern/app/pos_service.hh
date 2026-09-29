#pragma once

#include "core/check.hh"
#include "core/day.hh"
#include "core/employee.hh"
#include "core/menu.hh"
#include "core/report.hh"
#include "core/settings.hh"

#include <QJsonObject>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <map>
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
    virtual void printReceipt(const core::PosSettings &settings, const core::Check &check) = 0;
    virtual void printReport(const core::PosSettings &settings, const core::Report &report) = 0;
    virtual void openDrawer(const core::PosSettings &settings) = 0;
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
    std::optional<core::DrawerSession> drawer;    // today's latest drawer
    std::int64_t lastDrawerId = 0;
    std::vector<PastDay> pastDays;                // newest first
};

// The POS session for one terminal: who is logged in, the check being worked
// on, the keypad entry, and every order/payment operation. Pages call it
// through zone actions and widgets; it never touches the UI or the database
// directly. Results are reported with `notice` (for the status toast).
class PosService : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY sessionChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY sessionChanged)
    Q_PROPERTY(QString userRole READ userRole NOTIFY sessionChanged)
    Q_PROPERTY(bool clockedIn READ clockedIn NOTIFY sessionChanged)
    Q_PROPERTY(QString clockedInSince READ clockedInSince NOTIFY sessionChanged)
    Q_PROPERTY(QString storeName READ storeName CONSTANT)

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
    Q_PROPERTY(QString checkFilter READ checkFilter WRITE setCheckFilter NOTIFY openChecksChanged)
    Q_PROPERTY(QVariantMap drawer READ drawerInfo NOTIFY drawerChanged)
    Q_PROPERTY(QVariantMap day READ dayInfo NOTIFY dayChanged)
    Q_PROPERTY(QVariantList days READ days NOTIFY dayChanged)
    Q_PROPERTY(int adminRevision READ adminRevision NOTIFY adminChanged)

public:
    enum class TableResult { Failed, OpenedExisting, NeedsGuestCount, ChooseCheck };

    PosService(PosData data, PosSink *sink, QObject *parent = nullptr);

    void setPrinter(PosPrinter *printer) { printer_ = printer; }

    // Test hook: replace the wall clock (epoch ms).
    void setClock(std::function<std::int64_t()> now) { now_ = std::move(now); }

    const core::PosSettings &settings() const { return settings_; }
    const core::Check *currentCheck() const;
    const core::Employee *user() const { return user_; }
    const core::MenuItem *findItem(const QString &idOrName) const;
    Q_INVOKABLE bool can(const QString &permission) const;
    QString format(Money amount) const;

    // --- session -----------------------------------------------------------
    Q_INVOKABLE void pinKey(const QString &key);   // "0".."9", "clear", "back"
    bool login();                                  // with the PIN entered
    bool loginWithPin(const QString &pin);
    void logout();
    bool clockIn();                                // logged-in user, else PIN entered
    bool clockOut();

    // --- keypads -------------------------------------------------------------
    Q_INVOKABLE void entryKey(const QString &key);   // digits, "00", "clear", "back"
    Q_INVOKABLE void adjustGuests(int delta);
    Q_INVOKABLE void textKey(const QString &key);    // characters, "space", "back", "clear"
    void clearEntry();

    // --- checks ---------------------------------------------------------------
    Q_INVOKABLE QVariantMap tableStatus(const QString &label) const;
    TableResult selectTable(const QString &label);
    bool startCheck(core::CheckType type);   // dine-in uses the pending table + guest entry
    bool openCheck(std::int64_t checkId);
    void releaseCheck();

    bool addItem(const QString &idOrName);
    void setQualifier(const QString &qualifier);   // same one again clears it
    void selectLine(qint64 lineId);
    void selectPayment(qint64 paymentId);
    bool voidItem();
    bool sendOrder();
    bool addComment();
    // Amount: explicit cents, else the keypad entry, else the balance due.
    bool tender(const QString &tenderId, std::optional<std::int64_t> amountCents = std::nullopt);
    bool removePayment();
    bool closeCheck();
    // The current check, else the last one closed on this terminal.
    Q_INVOKABLE bool printReceipt();
    Q_INVOKABLE bool noSale();   // open the cash drawer without a sale

    // --- split check -------------------------------------------------------------
    // Other open checks at the same table, plus {id: 0, label: "New check"}.
    Q_INVOKABLE QVariantList splitTargets() const;
    // Move the selected line to another check (0 = a new one at the table).
    Q_INVOKABLE bool splitLine(qint64 targetCheckId);
    QString checkFilter() const { return checkFilter_; }
    void setCheckFilter(const QString &label);

    // --- drawer and business day ---------------------------------------------------
    Q_INVOKABLE bool openDrawerSession();   // starting cash from the keypad entry
    Q_INVOKABLE bool countDrawer();         // counted cash from the keypad entry
    Q_INVOKABLE bool endOfDay();
    QVariantMap drawerInfo() const;
    QVariantMap dayInfo() const;
    QVariantList days() const;
    const core::BusinessDay &currentDay() const { return day_; }
    const std::vector<core::Check> &closedToday() const { return closedToday_; }

    // --- reports -------------------------------------------------------------------
    // id: sales | items | servers | labor | drawer. dayId 0 = today (live).
    Q_INVOKABLE QVariantMap report(const QString &id, qint64 dayId = 0) const;
    Q_INVOKABLE bool printReport(const QString &id, qint64 dayId = 0);
    core::Report buildReport(const QString &id) const;

    // --- admin (manager) -------------------------------------------------------------
    // panel: menu | employees | tenders | printers | taxes | store
    Q_INVOKABLE QVariantList adminFields(const QString &panel) const;
    Q_INVOKABLE QVariantList adminRecords(const QString &panel) const;
    Q_INVOKABLE QVariantMap adminNewRecord(const QString &panel) const;
    // index -1 adds a record. Returns false (with a notice) when invalid.
    Q_INVOKABLE bool adminSave(const QString &panel, int index, const QVariantMap &record);
    Q_INVOKABLE bool adminDelete(const QString &panel, int index);
    int adminRevision() const { return adminRevision_; }
    const std::vector<core::MenuItem> &menu() const { return menu_; }
    const std::vector<core::Employee> &employees() const { return employees_; }

    // --- QML-facing state -------------------------------------------------------
    bool loggedIn() const { return user_ != nullptr; }
    QString userName() const;
    QString userRole() const;
    bool clockedIn() const;
    QString clockedInSince() const;
    QString storeName() const;
    int pinLength() const { return int(pin_.size()); }
    QString entry() const { return entry_; }
    QString entryAmount() const;
    int entryGuests() const;
    QString textEntry() const { return text_; }
    QString pendingQualifier() const;
    QString pendingTable() const { return pendingTable_; }
    bool hasCheck() const { return currentCheck() != nullptr; }
    QVariantMap checkInfo() const;
    QVariantList lines() const;
    QVariantMap totals() const;
    QVariantList payments() const;
    qint64 selectedLine() const { return selectedLine_; }
    qint64 selectedPayment() const { return selectedPayment_; }
    QVariantList openChecks() const;

signals:
    void sessionChanged();
    void entryChanged();
    void qualifierChanged();
    void checkChanged();
    void openChecksChanged();
    void notice(const QString &message);
    void loggedInChanged(bool loggedIn);
    void checkClosed(qint64 checkId);
    void drawerChanged();
    void dayChanged();
    void adminChanged();

private:
    core::Check *current();
    bool require(const char *permission, const QString &action);
    bool fail(const QString &message);
    void changed(core::Check &check);   // persist + notify
    const core::Employee *employeeByPin(const QString &pin) const;
    core::TimePunch *openPunch(const std::string &employeeId);
    std::int64_t now() const { return now_(); }
    void startDay();
    core::ReportContext reportContext(const QString &period) const;
    QString dayLabel(const core::BusinessDay &day) const;
    bool saveMenuRecord(int index, const QVariantMap &record);
    bool saveEmployeeRecord(int index, const QVariantMap &record);
    bool saveTenderRecord(int index, const QVariantMap &record);
    bool savePrinterRecord(int index, const QVariantMap &record);
    void settingsChanged();

    core::PosSettings settings_;
    std::vector<core::MenuItem> menu_;
    std::vector<core::Employee> employees_;
    std::map<std::int64_t, core::Check> open_;
    std::vector<core::TimePunch> punches_;   // today's, plus any still open
    std::int64_t lastCheckId_ = 0;
    std::int64_t lastPunchId_ = 0;
    PosSink *sink_;
    PosPrinter *printer_ = nullptr;
    std::function<std::int64_t()> now_;

    core::BusinessDay day_;
    std::int64_t lastDayId_ = 0;
    std::vector<core::Check> closedToday_;
    std::optional<core::DrawerSession> drawer_;
    std::int64_t lastDrawerId_ = 0;
    std::vector<PastDay> pastDays_;
    std::int64_t lastClosedId_ = 0;
    QString checkFilter_;
    int adminRevision_ = 0;

    const core::Employee *user_ = nullptr;
    std::int64_t currentId_ = 0;
    qint64 selectedLine_ = 0;
    qint64 selectedPayment_ = 0;
    core::Qualifier qualifier_ = core::Qualifier::None;
    QString pin_;
    QString entry_;
    QString text_;
    QString pendingTable_;
};

} // namespace vt::app
