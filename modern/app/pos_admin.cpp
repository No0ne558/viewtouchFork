// PosService: manager screens for the menu, staff, payment types, printers,
// taxes and store settings. Records travel to QML as maps; the field lists
// use the same schema format as the page editor's inspector.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <QDateTime>
#include <QLocale>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace vt::core;

namespace vt::app {

namespace {

QVariantMap field(const QString &path, const QString &label, const QString &type, const QString &hint = {})
{
    QVariantMap f{{u"path"_s, path}, {u"label"_s, label}, {u"type"_s, type}, {u"group"_s, QString()}};
    if (!hint.isEmpty())
        f.insert(u"hint"_s, hint);
    return f;
}

QVariantMap with(QVariantMap f, const QString &key, const QVariant &value)
{
    f.insert(key, value);
    return f;
}

QVariantList options(std::initializer_list<std::pair<const char *, const char *>> values)
{
    QVariantList out;
    for (const auto &[value, text] : values)
        out.append(QVariantMap{{u"value"_s, QString::fromUtf8(value)}, {u"text"_s, QString::fromUtf8(text)}});
    return out;
}

QString slug(const QString &text)
{
    static const QRegularExpression nonWord(u"[^a-z0-9]+"_s);
    QString s = text.toLower().normalized(QString::NormalizationForm_KD);
    s.replace(nonWord, u"-"_s);
    while (s.startsWith(u'-')) s.remove(0, 1);
    while (s.endsWith(u'-')) s.chop(1);
    return s.isEmpty() ? u"item"_s : s;
}

template <typename Container, typename IdOf>
QString uniqueId(const QString &wanted, const Container &items, IdOf idOf, int skipIndex)
{
    auto taken = [&](const QString &id) {
        for (int i = 0; i < int(items.size()); ++i) {
            if (i != skipIndex && qs(idOf(items[i])) == id)
                return true;
        }
        return false;
    };
    QString id = slug(wanted);
    for (int n = 2; taken(id); ++n)
        id = slug(wanted) + u'-' + QString::number(n);
    return id;
}

double number(const QVariantMap &r, const char16_t *key)
{
    return r.value(QString::fromUtf16(key)).toString().toDouble();
}

} // namespace

QVariantList PosService::adminFields(const QString &panel)
{
    const QVariantMap readonlyId = with(field(u"id"_s, tr("ID"), u"string"_s,
                                              tr("How buttons refer to it. Fixed once saved.")),
                                        u"readonlyExisting"_s, true);
    if (panel == u"menu") {
        QVariantList printers = options({{"", "(no ticket)"}});
        for (const PrinterConfig &p : s_->settings.printers)
            printers.append(QVariantMap{{u"value"_s, qs(p.id)}, {u"text"_s, qs(p.name)}});
        return {
            field(u"name"_s, tr("Name"), u"string"_s), readonlyId,
            field(u"price"_s, tr("Price"), u"money"_s),
            field(u"family"_s, tr("Family"), u"string"_s, tr("Groups items on the sales report")),
            with(field(u"taxClass"_s, tr("Tax"), u"enum"_s), u"options"_s,
                 options({{"food", "Food"}, {"alcohol", "Alcohol"}, {"merchandise", "Merchandise"},
                          {"room", "Room"}, {"none", "No tax"}})),
            with(field(u"printer"_s, tr("Kitchen ticket goes to"), u"enum"_s), u"options"_s, printers),
            field(u"modifier"_s, tr("Is a modifier (attaches to the item before it)"), u"bool"_s),
            field(u"available"_s, tr("Available (untick when sold out / 86'd)"), u"bool"_s),
        };
    }
    if (panel == u"employees") {
        return {
            field(u"name"_s, tr("Name"), u"string"_s), readonlyId,
            with(field(u"role"_s, tr("Role"), u"enum"_s), u"options"_s,
                 options({{"server", "Server"}, {"cashier", "Cashier"}, {"manager", "Manager"}, {"admin", "Admin"}})),
            field(u"pin"_s, tr("New PIN"), u"pin"_s, tr("4 to 8 digits. Leave empty to keep the current PIN.")),
            field(u"active"_s, tr("Active (can log in)"), u"bool"_s),
            with(field(u"cashMode"_s, tr("Cash handling"), u"enum"_s,
                       tr("Where the cash this person takes goes. Store setting: %1.")
                           .arg(s_->settings.cashMode == CashMode::ServerBank ? tr("server bank") : tr("terminal drawer"))),
                 u"options"_s, options({{"", "Store setting"}, {"serverBank", "Own bank (carries their cash)"},
                                        {"drawer", "Terminal's cash drawer"}})),
            with(field(u"checkout"_s, tr("Checking out with open checks"), u"enum"_s,
                       tr("Store setting: %1.").arg(s_->settings.checkoutNeedsClosedChecks
                                                         ? tr("close all checks first") : tr("allowed"))),
                 u"options"_s, options({{"", "Store setting"}, {"closeChecks", "Must close all checks first"},
                                        {"anyTime", "May leave checks open"}})),
        };
    }
    if (panel == u"tenders") {
        return {
            field(u"name"_s, tr("Name"), u"string"_s), readonlyId,
            with(field(u"kind"_s, tr("Kind"), u"enum"_s), u"options"_s,
                 options({{"cash", "Cash (gives change, goes in the drawer)"},
                          {"card", "Card (capped at the balance)"},
                          {"discount", "Discount / comp"}})),
            field(u"percent"_s, tr("Discount %"), u"percent"_s, tr("Discounts only. 100 = comp.")),
        };
    }
    if (panel == u"printers") {
        return {
            field(u"name"_s, tr("Name"), u"string"_s), readonlyId,
            with(field(u"type"_s, tr("Connection"), u"enum"_s), u"options"_s,
                 options({{"network", "Network (raw TCP, port 9100)"}, {"cups", "CUPS queue"},
                          {"file", "Text file"}, {"none", "Off"}})),
            field(u"host"_s, tr("Address"), u"string"_s, tr("Network printers: IP address or host name")),
            with(field(u"port"_s, tr("Port"), u"int"_s), u"min"_s, 1),
            field(u"path"_s, tr("File or queue"), u"string"_s, tr("File path, or the CUPS queue name")),
            with(field(u"format"_s, tr("Format"), u"enum"_s), u"options"_s,
                 options({{"", "Automatic"}, {"escpos", "ESC/POS (thermal)"}, {"text", "Plain text"}})),
            with(with(field(u"width"_s, tr("Characters per line"), u"int"_s), u"min"_s, 16), u"max"_s, 80),
            field(u"cutter"_s, tr("Cut paper after each ticket"), u"bool"_s),
            field(u"drawerKick"_s, tr("Cash drawer is connected to this printer"), u"bool"_s),
        };
    }
    if (panel == u"taxes") {
        return {
            field(u"food"_s, tr("Food tax %"), u"percent"_s),
            field(u"alcohol"_s, tr("Alcohol tax %"), u"percent"_s),
            field(u"merchandise"_s, tr("Merchandise tax %"), u"percent"_s),
            field(u"room"_s, tr("Room tax %"), u"percent"_s),
            field(u"taxTakeoutFood"_s, tr("Tax takeout food"), u"bool"_s),
        };
    }
    if (panel == u"store") {
        return {
            field(u"storeName"_s, tr("Store name"), u"string"_s),
            field(u"currencySymbol"_s, tr("Currency symbol"), u"string"_s),
            field(u"receiptHeader"_s, tr("Receipt header"), u"text"_s, tr("Address, phone… one per line")),
            field(u"receiptFooter"_s, tr("Receipt footer"), u"text"_s),
            with(field(u"cashMode"_s, tr("Cash handling"), u"enum"_s,
                       tr("Server bank: whoever takes cash keeps it and turns it in at check out, so anyone can "
                          "use any terminal. Drawer: each terminal has a cash drawer.")),
                 u"options"_s, options({{"serverBank", "Server bank (each person carries their own cash)"},
                                        {"drawer", "Cash drawer on each terminal"}})),
            field(u"terminalsHaveDrawer"_s, tr("Terminals have a cash drawer"), u"bool"_s,
                  tr("Unless set for a terminal in Terminals.")),
            field(u"checkoutNeedsClosedChecks"_s, tr("Close all checks before checking out"), u"bool"_s,
                  tr("Servers must close or hand over their checks before they check out their bank. "
                     "Can be set per employee.")),
            field(u"gratuityPercent"_s, tr("Party gratuity %"), u"percent"_s, tr("Added to large tables. 0 = off.")),
            with(with(field(u"gratuityMinGuests"_s, tr("…for tables of at least"), u"int"_s), u"min"_s, 1), u"max"_s, 99),
        };
    }
    if (panel == u"mealPeriods") {
        return {
            field(u"name"_s, tr("Name"), u"string"_s), readonlyId,
            field(u"start"_s, tr("Starts at"), u"string"_s,
                  tr("24-hour time, e.g. 11:00. Runs until the next period starts.")),
        };
    }
    if (panel == u"terminals") {
        QVariantList printers = options({{"", "Receipt (default)"}});
        for (const PrinterConfig &p : s_->settings.printers)
            printers.append(QVariantMap{{u"value"_s, qs(p.id)}, {u"text"_s, qs(p.name)}});
        return {
            field(u"name"_s, tr("Terminal name"), u"string"_s, tr("As given with --terminal (this one: %1)").arg(terminal_)),
            with(field(u"receiptPrinter"_s, tr("Receipts and cash drawer on"), u"enum"_s), u"options"_s, printers),
            with(field(u"drawer"_s, tr("Cash drawer"), u"enum"_s,
                       tr("Store setting: %1.").arg(s_->settings.terminalsHaveDrawer ? tr("has a drawer") : tr("no drawer"))),
                 u"options"_s, options({{"", "Store setting"}, {"yes", "Has a cash drawer"}, {"no", "No cash drawer"}})),
            with(field(u"screen"_s, tr("Screen layout"), u"enum"_s,
                       tr("Phone pages: big buttons in portrait, for phones and small handhelds.")),
                 u"options"_s, options({{"", "Automatic (phone pages on phones)"}, {"standard", "Standard pages"},
                                        {"phone", "Phone pages"}})),
        };
    }
    return {};
}

QVariantList PosService::mealPeriods() const
{
    QVariantList out;
    for (const MealPeriod &m : s_->settings.mealPeriods)
        out.append(QVariantMap{{u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)}, {u"start"_s, m.start}});
    return out;
}

QVariantList PosService::adminRecords(const QString &panel)
{
    QVariantList out;
    auto add = [&](QVariantMap r, const QString &title, const QString &detail) {
        r.insert(u"_title"_s, title);
        r.insert(u"_detail"_s, detail);
        out.append(r);
    };
    if (panel == u"menu") {
        for (const MenuItem &m : s_->menu)
            add(toJson(m).toVariantMap(), qs(m.name),
                format(m.price) + (m.isModifier ? tr(" · modifier") : QString())
                    + (m.available ? QString() : tr(" · SOLD OUT")));
    } else if (panel == u"employees") {
        for (const Employee &e : s_->employees) {
            QVariantMap r{{u"id"_s, qs(e.id)}, {u"name"_s, qs(e.name)}, {u"role"_s, qs(e.role)},
                          {u"active"_s, e.active}, {u"pin"_s, QString()}, {u"cashMode"_s, qs(e.cashMode)},
                          {u"checkout"_s, qs(e.checkout)}};
            const QString cash = e.cashMode == "serverBank" ? tr(" · own bank")
                                 : e.cashMode == "drawer"   ? tr(" · drawer") : QString();
            add(r, qs(e.name), qs(e.role) + cash + (e.active ? QString() : tr(" · inactive")));
        }
    } else if (panel == u"tenders") {
        for (const Tender &t : s_->settings.tenders) {
            QVariantMap r{{u"id"_s, qs(t.id)}, {u"name"_s, qs(t.name)}, {u"kind"_s, qs(toString(t.kind))},
                          {u"percent"_s, double(t.percentBp) / 100.0}};
            add(r, qs(t.name), t.kind == TenderKind::Discount ? u"%1%"_s.arg(double(t.percentBp) / 100.0)
                                                                : qs(toString(t.kind)));
        }
    } else if (panel == u"printers") {
        for (const PrinterConfig &p : s_->settings.printers) {
            QVariantMap r = toJson(p).toVariantMap();
            for (const char16_t *k : {u"host", u"path", u"format"}) {
                if (!r.contains(QString::fromUtf16(k)))
                    r.insert(QString::fromUtf16(k), QString());
            }
            r.insert(u"port"_s, p.port);
            add(r, qs(p.name), qs(p.type) + (p.host.empty() ? QString() : u" · "_s + qs(p.host)));
        }
    } else if (panel == u"taxes") {
        const TaxRates &t = s_->settings.tax;
        add({{u"food"_s, percentFromPpm(t.foodPpm)}, {u"alcohol"_s, percentFromPpm(t.alcoholPpm)},
             {u"merchandise"_s, percentFromPpm(t.merchandisePpm)}, {u"room"_s, percentFromPpm(t.roomPpm)},
             {u"taxTakeoutFood"_s, t.taxTakeoutFood}},
            tr("Tax rates"), QString());
    } else if (panel == u"store") {
        add({{u"storeName"_s, qs(s_->settings.storeName)}, {u"currencySymbol"_s, qs(s_->settings.currencySymbol)},
             {u"receiptHeader"_s, qs(s_->settings.receiptHeader)}, {u"receiptFooter"_s, qs(s_->settings.receiptFooter)},
             {u"gratuityPercent"_s, double(s_->settings.gratuityBp) / 100.0},
             {u"gratuityMinGuests"_s, s_->settings.gratuityMinGuests},
             {u"cashMode"_s, qs(toString(s_->settings.cashMode))},
             {u"terminalsHaveDrawer"_s, s_->settings.terminalsHaveDrawer},
             {u"checkoutNeedsClosedChecks"_s, s_->settings.checkoutNeedsClosedChecks}},
            tr("Store"), QString());
    } else if (panel == u"mealPeriods") {
        for (const MealPeriod &m : s_->settings.mealPeriods)
            add({{u"id"_s, qs(m.id)}, {u"name"_s, qs(m.name)}, {u"start"_s, clockText(m.start)}}, qs(m.name),
                tr("from %1").arg(clockText(m.start)));
    } else if (panel == u"terminals") {
        for (const TerminalConfig &t : s_->settings.terminals) {
            const PrinterConfig *p = s_->settings.printer(t.receiptPrinter);
            add({{u"name"_s, qs(t.name)}, {u"receiptPrinter"_s, qs(t.receiptPrinter)}, {u"drawer"_s, qs(t.drawer)},
                 {u"screen"_s, qs(t.screen)}},
                qs(t.name), (p ? qs(p->name) : tr("Receipt (default)"))
                                + (s_->settings.hasDrawer(t.name) ? QString() : tr(" · no drawer"))
                                + (t.key.empty() ? QString() : tr(" · paired device")));
        }
    }
    return out;
}

QVariantMap PosService::adminNewRecord(const QString &panel)
{
    if (panel == u"menu")
        return {{u"id"_s, QString()}, {u"name"_s, QString()}, {u"price"_s, 0.0}, {u"family"_s, QString()},
                {u"taxClass"_s, u"food"_s}, {u"printer"_s, u"kitchen"_s}, {u"modifier"_s, false}, {u"available"_s, true}};
    if (panel == u"employees")
        return {{u"id"_s, QString()}, {u"name"_s, QString()}, {u"role"_s, u"server"_s}, {u"pin"_s, QString()},
                {u"active"_s, true}, {u"cashMode"_s, QString()}, {u"checkout"_s, QString()}};
    if (panel == u"tenders")
        return {{u"id"_s, QString()}, {u"name"_s, QString()}, {u"kind"_s, u"card"_s}, {u"percent"_s, 0.0}};
    if (panel == u"terminals")
        return {{u"name"_s, terminal_}, {u"receiptPrinter"_s, QString()}, {u"drawer"_s, QString()},
                {u"screen"_s, QString()}};
    if (panel == u"mealPeriods")
        return {{u"id"_s, QString()}, {u"name"_s, QString()}, {u"start"_s, u"17:00"_s}};
    if (panel == u"printers")
        return {{u"id"_s, QString()}, {u"name"_s, QString()}, {u"type"_s, u"network"_s}, {u"host"_s, QString()},
                {u"port"_s, 9100}, {u"path"_s, QString()}, {u"format"_s, QString()}, {u"width"_s, 42},
                {u"cutter"_s, true}, {u"drawerKick"_s, false}};
    return {};
}

bool PosService::adminSave(const QString &panel, int index, const QVariantMap &record)
{
    if (!require(perm::Manager, tr("Changing settings")))
        return false;
    bool ok = false;
    if (panel == u"menu") {
        ok = saveMenuRecord(index, record);
    } else if (panel == u"employees") {
        ok = saveEmployeeRecord(index, record);
    } else if (panel == u"tenders") {
        ok = saveTenderRecord(index, record);
    } else if (panel == u"printers") {
        ok = savePrinterRecord(index, record);
    } else if (panel == u"mealPeriods") {
        ok = saveMealPeriodRecord(index, record);
    } else if (panel == u"terminals") {
        const QString name = record.value(u"name"_s).toString().trimmed();
        if (name.isEmpty())
            return fail(tr("The terminal needs a name."));
        auto &list = s_->settings.terminals;
        for (int i = 0; i < int(list.size()); ++i) {
            if (i != index && qs(list[i].name) == name)
                return fail(tr("%1 is already set up.").arg(name));
        }
        const QString drawer = record.value(u"drawer"_s).toString();
        if (!QStringList{QString(), u"yes"_s, u"no"_s}.contains(drawer))
            return fail(tr("Choose whether the terminal has a cash drawer."));
        // Editing keeps a paired device's id and key.
        TerminalConfig t = index >= 0 && index < int(list.size()) ? list[index] : TerminalConfig{};
        t.name = ss(name);
        t.receiptPrinter = ss(record.value(u"receiptPrinter"_s).toString());
        t.drawer = ss(drawer);
        const QString screen = record.value(u"screen"_s).toString();
        if (!QStringList{QString(), u"standard"_s, u"phone"_s}.contains(screen))
            return fail(tr("Choose the terminal's screen layout."));
        t.screen = ss(screen);
        if (index >= 0 && index < int(list.size()))
            list[index] = t;
        else
            list.push_back(t);
        settingsChanged();
        ok = true;
    } else if (panel == u"taxes") {
        const double rates[] = {number(record, u"food"), number(record, u"alcohol"),
                                number(record, u"merchandise"), number(record, u"room")};
        for (double r : rates) {
            if (r < 0 || r > 100)
                return fail(tr("Tax rates must be between 0 and 100%."));
        }
        s_->settings.tax.foodPpm = ppmFromPercent(rates[0]);
        s_->settings.tax.alcoholPpm = ppmFromPercent(rates[1]);
        s_->settings.tax.merchandisePpm = ppmFromPercent(rates[2]);
        s_->settings.tax.roomPpm = ppmFromPercent(rates[3]);
        s_->settings.tax.taxTakeoutFood = record.value(u"taxTakeoutFood"_s).toBool();
        settingsChanged();
        ok = true;
    } else if (panel == u"store") {
        const QString name = record.value(u"storeName"_s).toString().trimmed();
        if (name.isEmpty())
            return fail(tr("The store needs a name."));
        s_->settings.storeName = ss(name);
        s_->settings.currencySymbol = ss(record.value(u"currencySymbol"_s).toString());
        s_->settings.receiptHeader = ss(record.value(u"receiptHeader"_s).toString());
        s_->settings.receiptFooter = ss(record.value(u"receiptFooter"_s).toString());
        const double gratuity = number(record, u"gratuityPercent");
        if (gratuity < 0 || gratuity > 100)
            return fail(tr("Gratuity is between 0 and 100%."));
        s_->settings.gratuityBp = std::llround(gratuity * 100.0);
        s_->settings.gratuityMinGuests = std::max(1, record.value(u"gratuityMinGuests"_s, 6).toInt());
        if (record.contains(u"cashMode"_s))
            s_->settings.cashMode = cashModeFromString(ss(record.value(u"cashMode"_s).toString()));
        if (record.contains(u"terminalsHaveDrawer"_s))
            s_->settings.terminalsHaveDrawer = record.value(u"terminalsHaveDrawer"_s).toBool();
        if (record.contains(u"checkoutNeedsClosedChecks"_s))
            s_->settings.checkoutNeedsClosedChecks = record.value(u"checkoutNeedsClosedChecks"_s).toBool();
        settingsChanged();
        ok = true;
    }
    if (ok) {
        ++s_->adminRevision;
        emit s_->adminChanged();
        emit notice(tr("Saved"));
    }
    return ok;
}

bool PosService::saveMenuRecord(int index, const QVariantMap &record)
{
    if (index >= int(s_->menu.size()))
        return false;
    const QString name = record.value(u"name"_s).toString().trimmed();
    if (name.isEmpty())
        return fail(tr("The item needs a name."));
    if (number(record, u"price") < 0)
        return fail(tr("Prices cannot be negative."));
    MenuItem item = menuItemFromJson(QJsonObject::fromVariantMap(record));
    item.name = ss(name);
    if (index >= 0) {
        item.id = s_->menu[index].id;
        s_->menu[index] = item;
    } else {
        const QString wanted = record.value(u"id"_s).toString().trimmed();
        item.id = ss(uniqueId(wanted.isEmpty() ? name : wanted, s_->menu, [](const MenuItem &m) { return m.id; }, -1));
        s_->menu.push_back(item);
        index = int(s_->menu.size()) - 1;
    }
    if (s_->sink)
        s_->sink->saveMenuItem(s_->menu[index], index);
    return true;
}

bool PosService::saveEmployeeRecord(int index, const QVariantMap &record)
{
    if (index >= int(s_->employees.size()))
        return false;
    const QString name = record.value(u"name"_s).toString().trimmed();
    const QString pin = record.value(u"pin"_s).toString();
    const QString role = record.value(u"role"_s).toString();
    const bool active = record.value(u"active"_s, true).toBool();
    if (name.isEmpty())
        return fail(tr("The employee needs a name."));
    if (!QStringList{u"server"_s, u"cashier"_s, u"manager"_s, u"admin"_s}.contains(role))
        return fail(tr("Choose a role."));
    if (index < 0 && pin.isEmpty())
        return fail(tr("New employees need a PIN."));
    static const QRegularExpression digits(u"^[0-9]{4,8}$"_s);
    if (!pin.isEmpty() && !digits.match(pin).hasMatch())
        return fail(tr("PINs are 4 to 8 digits."));
    if (!pin.isEmpty()) {
        for (int i = 0; i < int(s_->employees.size()); ++i) {
            if (i != index && s_->employees[i].pinHash == hashPin(pin, s_->employees[i].pinSalt))
                return fail(tr("Someone else already uses that PIN."));
        }
    }
    const bool isSelf = index >= 0 && user() && s_->employees[index].id == user()->id;
    if (isSelf && (!active || !permissionsForRole(ss(role)).contains(perm::Manager)))
        return fail(tr("You cannot lock yourself out. Ask another manager."));

    const QString cashMode = record.value(u"cashMode"_s).toString();
    if (!QStringList{QString(), u"serverBank"_s, u"drawer"_s}.contains(cashMode))
        return fail(tr("Choose how this person handles cash."));
    const QString checkout = record.value(u"checkout"_s).toString();
    if (!QStringList{QString(), u"closeChecks"_s, u"anyTime"_s}.contains(checkout))
        return fail(tr("Choose whether this person may check out with open checks."));
    Employee e = index >= 0 ? s_->employees[index] : Employee{};
    e.cashMode = ss(cashMode);
    e.checkout = ss(checkout);
    e.name = ss(name);
    e.role = ss(role);
    e.active = active;
    if (index < 0)
        e.id = ss(uniqueId(record.value(u"id"_s).toString().trimmed().isEmpty() ? name : record.value(u"id"_s).toString(),
                           s_->employees, [](const Employee &x) { return x.id; }, -1));
    if (!pin.isEmpty()) {
        e.pinSalt = newSalt();
        e.pinHash = hashPin(pin, e.pinSalt);
    }
    if (index >= 0) {
        s_->employees[index] = e;
    } else {
        s_->employees.push_back(e);
        index = int(s_->employees.size()) - 1;
    }
    if (s_->sink)
        s_->sink->saveEmployee(s_->employees[index]);
    emit s_->staffChanged();
    return true;
}

bool PosService::saveMealPeriodRecord(int index, const QVariantMap &record)
{
    auto &list = s_->settings.mealPeriods;
    if (index >= int(list.size()))
        return false;
    const QString name = record.value(u"name"_s).toString().trimmed();
    if (name.isEmpty())
        return fail(tr("The meal period needs a name."));
    const int start = clockMinutes(record.value(u"start"_s).toString());
    if (start < 0)
        return fail(tr("Type the start as a 24-hour time, like 16:30."));
    for (int i = 0; i < int(list.size()); ++i) {
        if (i != index && list[i].start == start)
            return fail(tr("%1 already starts at %2.").arg(qs(list[i].name), clockText(start)));
    }
    MealPeriod m{{}, ss(name), start};
    if (index >= 0) {
        m.id = list[index].id;
        list[index] = m;
    } else {
        const QString wanted = record.value(u"id"_s).toString().trimmed();
        m.id = ss(uniqueId(wanted.isEmpty() ? name : wanted, list, [](const MealPeriod &x) { return x.id; }, -1));
        list.push_back(m);
    }
    std::ranges::sort(list, {}, &MealPeriod::start);
    settingsChanged();
    return true;
}

bool PosService::saveTenderRecord(int index, const QVariantMap &record)
{
    if (index >= int(s_->settings.tenders.size()))
        return false;
    const QString name = record.value(u"name"_s).toString().trimmed();
    if (name.isEmpty())
        return fail(tr("The payment type needs a name."));
    const double percent = number(record, u"percent");
    const TenderKind kind = tenderKindFromString(ss(record.value(u"kind"_s).toString()));
    if (kind == TenderKind::Discount && (percent <= 0 || percent > 100))
        return fail(tr("Discounts are between 0 and 100%."));
    Tender t;
    t.name = ss(name);
    t.kind = kind;
    t.percentBp = kind == TenderKind::Discount ? std::llround(percent * 100.0) : 0;
    if (index >= 0) {
        t.id = s_->settings.tenders[index].id;
        s_->settings.tenders[index] = t;
    } else {
        const QString wanted = record.value(u"id"_s).toString().trimmed();
        t.id = ss(uniqueId(wanted.isEmpty() ? name : wanted, s_->settings.tenders, [](const Tender &x) { return x.id; }, -1));
        s_->settings.tenders.push_back(t);
    }
    settingsChanged();
    return true;
}

bool PosService::savePrinterRecord(int index, const QVariantMap &record)
{
    if (index >= int(s_->settings.printers.size()))
        return false;
    PrinterConfig p = printerFromJson(QJsonObject::fromVariantMap(record));
    if (p.name.empty() || record.value(u"name"_s).toString().trimmed().isEmpty())
        return fail(tr("The printer needs a name."));
    if (!QStringList{u"network"_s, u"cups"_s, u"file"_s, u"none"_s}.contains(qs(p.type)))
        return fail(tr("Choose how the printer is connected."));
    if (p.type == "network" && p.host.empty())
        return fail(tr("Network printers need an address."));
    if (p.type == "file" && p.path.empty())
        return fail(tr("Choose a file to print into."));
    if (index >= 0) {
        p.id = s_->settings.printers[index].id;
        s_->settings.printers[index] = p;
    } else {
        const QString wanted = record.value(u"id"_s).toString().trimmed();
        p.id = ss(uniqueId(wanted.isEmpty() ? qs(p.name) : wanted, s_->settings.printers,
                           [](const PrinterConfig &x) { return x.id; }, -1));
        s_->settings.printers.push_back(p);
    }
    settingsChanged();
    return true;
}

bool PosService::adminDelete(const QString &panel, int index)
{
    if (!require(perm::Manager, tr("Changing settings")))
        return false;
    if (panel == u"menu" && index >= 0 && index < int(s_->menu.size())) {
        const std::string id = s_->menu[index].id;
        s_->menu.erase(s_->menu.begin() + index);
        if (s_->sink) {
            s_->sink->deleteMenuItem(id);
            for (int i = index; i < int(s_->menu.size()); ++i)
                s_->sink->saveMenuItem(s_->menu[i], i);   // positions shifted
        }
    } else if (panel == u"employees" && index >= 0 && index < int(s_->employees.size())) {
        // Staff are deactivated, not erased: their sales and hours keep a name.
        if (user() && s_->employees[index].id == user()->id)
            return fail(tr("You cannot remove yourself."));
        s_->employees[index].active = false;
        if (s_->sink)
            s_->sink->saveEmployee(s_->employees[index]);
        emit s_->staffChanged();
    } else if (panel == u"tenders" && index >= 0 && index < int(s_->settings.tenders.size())) {
        s_->settings.tenders.erase(s_->settings.tenders.begin() + index);
        settingsChanged();
    } else if (panel == u"mealPeriods" && index >= 0 && index < int(s_->settings.mealPeriods.size())) {
        s_->settings.mealPeriods.erase(s_->settings.mealPeriods.begin() + index);
        settingsChanged();
    } else if (panel == u"terminals" && index >= 0 && index < int(s_->settings.terminals.size())) {
        s_->settings.terminals.erase(s_->settings.terminals.begin() + index);
        settingsChanged();
    } else if (panel == u"printers" && index >= 0 && index < int(s_->settings.printers.size())) {
        s_->settings.printers.erase(s_->settings.printers.begin() + index);
        settingsChanged();
    } else {
        return fail(tr("That cannot be removed."));
    }
    ++s_->adminRevision;
    emit s_->adminChanged();
    emit notice(panel == u"employees" ? tr("Deactivated") : tr("Removed"));
    return true;
}

void PosShared::saveSettings()
{
    if (sink)
        sink->saveSettings(settings);
    ++adminRevision;
    emit adminChanged();
}

// --- pairing devices -------------------------------------------------------------

namespace {
// Crockford base32: no I, L, O or U to mix up. 10 characters = 50 bits,
// stretched by the key derivation on both sides (see net/pairing).
QString newPairingCode()
{
    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    QString code;
    for (int i = 0; i < 10; ++i) {
        if (i == 5)
            code += u'-';
        code += QChar::fromLatin1(alphabet[QRandomGenerator::system()->bounded(32)]);
    }
    return code;
}
} // namespace

QString PosShared::startPairing()
{
    pairing = Pairing{newPairingCode(), now() + 10 * 60 * 1000};
    ++adminRevision;
    emit adminChanged();
    // Take the code off the manager screens when it runs out.
    QTimer::singleShot(10 * 60 * 1000 + 500, this, [this, code = pairing->code] {
        if (pairing && pairing->code == code && !activePairing()) {
            pairing.reset();
            ++adminRevision;
            emit adminChanged();
        }
    });
    return pairing->code;
}

bool PosService::startPairing()
{
    if (!require(perm::Manager, tr("Pairing a device")))
        return false;
    s_->startPairing();
    emit notice(tr("Type %1 on the new device. The code works once, for 10 minutes.").arg(s_->pairing->code));
    return true;
}

bool PosService::stopPairing()
{
    if (!require(perm::Manager, tr("Pairing a device")))
        return false;
    s_->pairing.reset();
    ++s_->adminRevision;
    emit s_->adminChanged();
    return true;
}

QString PosService::screenMode() const
{
    for (const TerminalConfig &t : s_->settings.terminals) {
        if (qs(t.name) == terminal_)
            return qs(t.screen);
    }
    return {};
}

QVariantMap PosService::pairingInfo() const
{
    const PosShared::Pairing *p = s_->activePairing();
    if (!p || !can(QString::fromLatin1(perm::Manager)))
        return {{u"active"_s, false}};
    const QString until = QLocale().toString(QDateTime::fromMSecsSinceEpoch(p->expires).time(), QLocale::ShortFormat);
    return {{u"active"_s, true}, {u"code"_s, p->code}, {u"until"_s, until}};
}

void PosService::settingsChanged()
{
    if (s_->sink)
        s_->sink->saveSettings(s_->settings);
    // Tax changes re-total open checks.
    emit checkChanged();
    emit s_->checksChanged();
}

} // namespace vt::app
