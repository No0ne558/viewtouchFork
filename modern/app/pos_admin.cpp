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

// "As the role" / "Yes" / "No" for one permission of one person.
QVariantMap permField(const char *permission, const QString &label)
{
    return with(with(field(u"perm:"_s + QString::fromLatin1(permission), label, u"enum"_s, u"Permissions"_s),
                     u"options"_s, options({{"", "As the role"}, {"allow", "Yes"}, {"deny", "No"}})),
                u"hint"_s, QString());
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
            field(u"modifierGroups"_s, tr("Modifier groups"), u"string"_s,
                  tr("Choices asked for when it's ordered, by group ID, comma separated: %1")
                      .arg(groupIds().join(u", "))),
            field(u"periodPrices"_s, tr("Prices by meal period"), u"text"_s,
                  tr("One per line, e.g. \"dinner = 14.50\". Meal periods: %1").arg(periodIds().join(u", "))),
            field(u"recipe"_s, tr("Recipe (what one uses up)"), u"text"_s,
                  tr("One ingredient per line with the amount, e.g. \"bun 1\" or \"lettuce 0.5\" (Manager -> Inventory)."
                     " Sold out by itself when one runs short.")),
        };
    }
    if (panel == u"inventory") {
        return {
            field(u"name"_s, tr("Name"), u"string"_s), readonlyId,
            field(u"unit"_s, tr("Unit"), u"string"_s, tr("each, oz, lb, slice…")),
            field(u"onHand"_s, tr("On hand"), u"number"_s, tr("Count it, or add what was delivered.")),
            field(u"lowAt"_s, tr("Low at"), u"number"_s, tr("Warn when it gets down to this.")),
            field(u"cost"_s, tr("Cost per unit"), u"money"_s, tr("For the Food Cost report.")),
        };
    }
    if (panel == u"employees") {
        return {
            field(u"name"_s, tr("Name"), u"string"_s), readonlyId,
            with(field(u"role"_s, tr("Role"), u"enum"_s), u"options"_s,
                 options({{"server", "Server"}, {"bartender", "Bartender"}, {"cashier", "Cashier"}, {"host", "Host"},
                          {"busser", "Busser"}, {"manager", "Manager"}, {"admin", "Admin"}})),
            field(u"pin"_s, tr("New PIN"), u"pin"_s, tr("4 to 8 digits. Leave empty to keep the current PIN.")),
            field(u"active"_s, tr("Active (can log in)"), u"bool"_s),
            with(field(u"cashMode"_s, tr("Cash handling"), u"enum"_s,
                       tr("Where the cash this person takes goes. Store setting: %1.")
                           .arg(s_->settings.cashMode == CashMode::ServerBank ? tr("server bank") : tr("terminal drawer"))),
                 u"options"_s, options({{"", "Store setting"}, {"serverBank", "Own bank (carries their cash)"},
                                        {"drawer", "Terminal's cash drawer"}})),
            permField(perm::Order, tr("Take orders")),
            permField(perm::Settle, tr("Take payments and close checks")),
            permField(perm::Discount, tr("Give discounts and comps")),
            permField(perm::Void, tr("Void items already sent")),
            permField(perm::Manager, tr("Manager screens (reports, settings, staff…)")),
            permField(perm::EditLayout, tr("Edit pages")),
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
            field(u"paidBreaks"_s, tr("Breaks are paid"), u"bool"_s, tr("Otherwise break time isn't counted as worked.")),
            with(with(field(u"overtimeDailyHours"_s, tr("Overtime after hours in a day"), u"int"_s,
                            tr("0 = no daily rule (e.g. 8 in California)")), u"min"_s, 0), u"max"_s, 24),
            with(with(field(u"overtimeWeeklyHours"_s, tr("Overtime after hours in a week"), u"int"_s,
                            tr("0 = no weekly rule (40 under US federal law)")), u"min"_s, 0), u"max"_s, 168),
            with(field(u"weekStartsOn"_s, tr("Pay week starts on"), u"enum"_s), u"options"_s,
                 options({{"0", "Sunday"}, {"1", "Monday"}, {"2", "Tuesday"}, {"3", "Wednesday"},
                          {"4", "Thursday"}, {"5", "Friday"}, {"6", "Saturday"}})),
            field(u"scheduleRequired"_s, tr("Staff clock in only on their schedule"), u"bool"_s,
                  tr("From a little before a scheduled shift until it ends. Managers can always clock in, and can clock "
                     "someone in from Manager → Schedule.")),
            with(with(field(u"clockInEarlyMinutes"_s, tr("…clock in up to (minutes) early"), u"int"_s), u"min"_s, 0),
                 u"max"_s, 240),
            field(u"tipOuts"_s, tr("Tip-outs"), u"text"_s,
                  tr("One per line: role, percent, tips or sales - e.g. \"busser 15 tips\" or \"bartender 2 sales\". "
                     "Shared by everyone of that role who worked today, by hours. Roles: busser, bartender, host, "
                     "server, cashier.")),
            with(with(field(u"kitchenWarnMinutes"_s, tr("Kitchen: ticket turns yellow after (minutes)"), u"int"_s),
                      u"min"_s, 1), u"max"_s, 120),
            with(with(field(u"kitchenLateMinutes"_s, tr("Kitchen: ticket is late (red) after (minutes)"), u"int"_s,
                            tr("Late tickets are counted in the Kitchen report.")), u"min"_s, 1), u"max"_s, 240),
            field(u"tipPercents"_s, tr("Tip choices for guests (%)"), u"text"_s,
                  tr("Shown on the customer display, e.g. 15, 18, 20, 25 (up to 6).")),
            with(with(field(u"waitMinutesPerParty"_s, tr("Waitlist: minutes per party ahead"), u"int"_s,
                            tr("Wait quotes: (parties ahead + 1) x this, rounded up to 5 minutes.")), u"min"_s, 1), u"max"_s, 120),
            field(u"tableReadyText"_s, tr("Waitlist: table-ready text"), u"text"_s,
                  tr("Sent when the host touches Notify. {name} and {store} are filled in.")),
            field(u"textWebhook"_s, tr("Texting service URL"), u"text"_s,
                  tr("Texts are POSTed here as JSON {\"to\", \"message\"} (your SMS provider or a relay). Empty: no texts; the host tells the guest.")),
            field(u"backupCopyDir"_s, tr("Also copy backups to"), u"text"_s,
                  tr("A USB drive or network folder on the server, e.g. /media/usb/viewtouch. Empty = no second copy.")),
            field(u"checkoutNeedsClosedChecks"_s, tr("Close all checks before checking out"), u"bool"_s,
                  tr("Servers must close or hand over their checks before they check out their bank. "
                     "Can be set per employee.")),
            field(u"gratuityPercent"_s, tr("Party gratuity %"), u"percent"_s, tr("Added to large tables. 0 = off.")),
            with(with(field(u"gratuityMinGuests"_s, tr("…for tables of at least"), u"int"_s), u"min"_s, 1), u"max"_s, 99),
        };
    }
    if (panel == u"modifierGroups") {
        return {
            field(u"name"_s, tr("Name"), u"string"_s, tr("What the server sees, e.g. Temperature")), readonlyId,
            with(with(field(u"min"_s, tr("Choices required"), u"int"_s, tr("0 = optional")), u"min"_s, 0), u"max"_s, 20),
            with(with(field(u"max"_s, tr("Most choices allowed"), u"int"_s,
                            tr("1 = pick one (a new choice replaces it), 0 = any number")), u"min"_s, 0), u"max"_s, 20),
            field(u"options"_s, tr("Options"), u"text"_s,
                  tr("One per line; a price after +, e.g. \"Onion Rings + 1.50\"")),
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
            add(menuRecord(m), qs(m.name),
                format(m.price) + (m.isModifier ? tr(" · modifier") : QString())
                    + (m.available ? QString() : tr(" · SOLD OUT")));
    } else if (panel == u"employees") {
        for (const Employee &e : s_->employees) {
            QVariantMap r{{u"id"_s, qs(e.id)}, {u"name"_s, qs(e.name)}, {u"role"_s, qs(e.role)},
                          {u"active"_s, e.active}, {u"pin"_s, QString()}, {u"cashMode"_s, qs(e.cashMode)},
                          {u"checkout"_s, qs(e.checkout)}};
            for (const char *p : AllPermissions)
                r.insert(u"perm:"_s + QString::fromLatin1(p),
                         e.allow.contains(p) ? u"allow"_s : e.deny.contains(p) ? u"deny"_s : QString());
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
             {u"checkoutNeedsClosedChecks"_s, s_->settings.checkoutNeedsClosedChecks},
             {u"backupCopyDir"_s, qs(s_->settings.backupCopyDir)},
             {u"waitMinutesPerParty"_s, s_->settings.waitMinutesPerParty},
             {u"scheduleRequired"_s, s_->settings.scheduleRequired},
             {u"clockInEarlyMinutes"_s, s_->settings.clockInEarlyMinutes},
             {u"tipOuts"_s, [&] {
                  QStringList l;
                  for (const PosSettings::TipOut &t : s_->settings.tipOuts)
                      l << u"%1 %2 %3"_s.arg(qs(t.role), QString::number(double(t.percentBp) / 100.0), qs(t.basis));
                  return l.join(u'\n');
              }()},
             {u"kitchenWarnMinutes"_s, s_->settings.kitchenWarnMinutes},
             {u"kitchenLateMinutes"_s, s_->settings.kitchenLateMinutes},
             {u"tipPercents"_s, [&] { QStringList l; for (int p : s_->settings.tipPercents) l << QString::number(p); return l.join(u", "_s); }()},
             {u"tableReadyText"_s, qs(s_->settings.tableReadyText)}, {u"textWebhook"_s, qs(s_->settings.textWebhook)},
             {u"paidBreaks"_s, s_->settings.paidBreaks}, {u"overtimeDailyHours"_s, s_->settings.overtimeDailyHours},
             {u"overtimeWeeklyHours"_s, s_->settings.overtimeWeeklyHours},
             {u"weekStartsOn"_s, QString::number(s_->settings.weekStartsOn)}},
            tr("Store"), QString());
    } else if (panel == u"inventory") {
        for (const Ingredient &g : s_->ingredients)
            add(toJson(g).toVariantMap(), qs(g.name),
                QString::number(g.onHand, 'g', 8) + u' ' + qs(g.unit)
                    + (g.onHand <= 0 ? tr(" · OUT") : g.low() ? tr(" · LOW") : QString()));
    } else if (panel == u"modifierGroups") {
        for (const ModifierGroup &g : s_->settings.modifierGroups) {
            QStringList lines;
            for (const ModifierOption &o : g.options)
                lines << (o.price.cents() ? u"%1 + %2"_s.arg(qs(o.name), qs(o.price.toString())) : qs(o.name));
            add({{u"id"_s, qs(g.id)}, {u"name"_s, qs(g.name)}, {u"min"_s, g.min}, {u"max"_s, g.max},
                 {u"options"_s, lines.join(u'\n')}},
                qs(g.name), tr("%1 options").arg(g.options.size()) + (g.min > 0 ? tr(" · required") : QString()));
        }
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
                {u"taxClass"_s, u"food"_s}, {u"printer"_s, u"kitchen"_s}, {u"modifier"_s, false}, {u"available"_s, true},
                {u"modifierGroups"_s, QString()}, {u"periodPrices"_s, QString()}, {u"recipe"_s, QString()}};
    if (panel == u"employees")
        return {{u"id"_s, QString()}, {u"name"_s, QString()}, {u"role"_s, u"server"_s}, {u"pin"_s, QString()},
                {u"active"_s, true}, {u"cashMode"_s, QString()}, {u"checkout"_s, QString()},
                {u"perm:order"_s, QString()}, {u"perm:check.settle"_s, QString()}, {u"perm:check.discount"_s, QString()},
                {u"perm:order.void"_s, QString()}, {u"perm:manager"_s, QString()}, {u"perm:layout.edit"_s, QString()}};
    if (panel == u"tenders")
        return {{u"id"_s, QString()}, {u"name"_s, QString()}, {u"kind"_s, u"card"_s}, {u"percent"_s, 0.0}};
    if (panel == u"terminals")
        return {{u"name"_s, terminal_}, {u"receiptPrinter"_s, QString()}, {u"drawer"_s, QString()},
                {u"screen"_s, QString()}};
    if (panel == u"mealPeriods")
        return {{u"id"_s, QString()}, {u"name"_s, QString()}, {u"start"_s, u"17:00"_s}};
    if (panel == u"modifierGroups")
        return {{u"id"_s, QString()}, {u"name"_s, QString()}, {u"min"_s, 1}, {u"max"_s, 1}, {u"options"_s, QString()}};
    if (panel == u"inventory")
        return {{u"id"_s, QString()}, {u"name"_s, QString()}, {u"unit"_s, u"each"_s}, {u"onHand"_s, 0.0},
                {u"lowAt"_s, 0.0}, {u"cost"_s, 0.0}};
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
        if (ok)
            refreshSoldOut();
    } else if (panel == u"employees") {
        ok = saveEmployeeRecord(index, record);
    } else if (panel == u"tenders") {
        ok = saveTenderRecord(index, record);
    } else if (panel == u"printers") {
        ok = savePrinterRecord(index, record);
    } else if (panel == u"mealPeriods") {
        ok = saveMealPeriodRecord(index, record);
    } else if (panel == u"modifierGroups") {
        ok = saveModifierGroupRecord(index, record);
    } else if (panel == u"inventory") {
        const QString name = record.value(u"name"_s).toString().trimmed();
        if (name.isEmpty())
            return fail(tr("The ingredient needs a name."));
        if (index >= int(s_->ingredients.size()))
            return false;
        Ingredient g = ingredientFromJson(QJsonObject::fromVariantMap(record));
        g.name = ss(name);
        if (g.unit.empty())
            g.unit = "each";
        if (g.lowAt < 0)
            return fail(tr("Low at can't be negative."));
        if (index >= 0) {
            g.id = s_->ingredients[index].id;
            s_->ingredients[index] = g;
        } else {
            const QString wanted = record.value(u"id"_s).toString().trimmed();
            g.id = ss(uniqueId(wanted.isEmpty() ? name : wanted, s_->ingredients,
                               [](const Ingredient &i) { return i.id; }, -1));
            s_->ingredients.push_back(g);
            index = int(s_->ingredients.size()) - 1;
        }
        if (s_->sink)
            s_->sink->saveIngredient(s_->ingredients[index], index);
        refreshSoldOut();   // restocked items come back, short ones go
        ok = true;
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
        if (record.contains(u"paidBreaks"_s))
            s_->settings.paidBreaks = record.value(u"paidBreaks"_s).toBool();
        if (record.contains(u"overtimeDailyHours"_s))
            s_->settings.overtimeDailyHours = std::clamp(record.value(u"overtimeDailyHours"_s).toInt(), 0, 24);
        if (record.contains(u"overtimeWeeklyHours"_s))
            s_->settings.overtimeWeeklyHours = std::clamp(record.value(u"overtimeWeeklyHours"_s).toInt(), 0, 168);
        if (record.contains(u"weekStartsOn"_s))
            s_->settings.weekStartsOn = std::clamp(record.value(u"weekStartsOn"_s).toString().toInt(), 0, 6);
        if (record.contains(u"checkoutNeedsClosedChecks"_s))
            s_->settings.checkoutNeedsClosedChecks = record.value(u"checkoutNeedsClosedChecks"_s).toBool();
        if (record.contains(u"tipPercents"_s)) {
            std::vector<int> tips;
            for (const QString &part : record.value(u"tipPercents"_s).toString().split(u',', Qt::SkipEmptyParts)) {
                const int v = part.trimmed().remove(u'%').toInt();
                if (v > 0 && v <= 100 && tips.size() < 6)
                    tips.push_back(v);
            }
            if (!tips.empty())
                s_->settings.tipPercents = tips;
        }
        if (record.contains(u"scheduleRequired"_s))
            s_->settings.scheduleRequired = record.value(u"scheduleRequired"_s).toBool();
        if (record.contains(u"clockInEarlyMinutes"_s))
            s_->settings.clockInEarlyMinutes = std::clamp(record.value(u"clockInEarlyMinutes"_s).toInt(), 0, 240);
        if (record.contains(u"tipOuts"_s)) {
            std::vector<PosSettings::TipOut> outs;
            for (const QString &line : record.value(u"tipOuts"_s).toString().split(u'\n', Qt::SkipEmptyParts)) {
                const QStringList parts = line.simplified().remove(u'%').split(u' ', Qt::SkipEmptyParts);
                if (parts.isEmpty())
                    continue;
                bool ok = false;
                const double percent = parts.value(1).toDouble(&ok);
                const QString role = parts.value(0).toLower();
                const QString basis = parts.value(2, u"tips"_s).toLower();
                if (!QStringList{u"busser"_s, u"bartender"_s, u"host"_s, u"server"_s, u"cashier"_s}.contains(role))
                    return fail(tr("Tip-outs: \"%1\" is not a role (busser, bartender, host, server, cashier).").arg(role));
                if (!ok || percent <= 0 || percent > 100 || (basis != u"tips" && basis != u"sales"))
                    return fail(tr("Write tip-outs like \"busser 15 tips\" or \"bartender 2 sales\"."));
                outs.push_back({ss(role), std::llround(percent * 100.0), ss(basis)});
            }
            s_->settings.tipOuts = outs;
        }
        if (record.contains(u"kitchenWarnMinutes"_s))
            s_->settings.kitchenWarnMinutes = std::clamp(record.value(u"kitchenWarnMinutes"_s).toInt(), 1, 120);
        if (record.contains(u"kitchenLateMinutes"_s))
            s_->settings.kitchenLateMinutes =
                std::clamp(record.value(u"kitchenLateMinutes"_s).toInt(), s_->settings.kitchenWarnMinutes, 240);
        if (record.contains(u"waitMinutesPerParty"_s))
            s_->settings.waitMinutesPerParty = std::clamp(record.value(u"waitMinutesPerParty"_s).toInt(), 1, 120);
        if (record.contains(u"tableReadyText"_s) && !record.value(u"tableReadyText"_s).toString().trimmed().isEmpty())
            s_->settings.tableReadyText = ss(record.value(u"tableReadyText"_s).toString().trimmed());
        if (record.contains(u"textWebhook"_s))
            s_->settings.textWebhook = ss(record.value(u"textWebhook"_s).toString().trimmed());
        if (record.contains(u"backupCopyDir"_s))
            s_->settings.backupCopyDir = ss(record.value(u"backupCopyDir"_s).toString().trimmed());
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
    // The form's text fields, as menu data.
    QVariantMap data = record;
    QVariantList groups;
    for (QString g : record.value(u"modifierGroups"_s).toString().split(u',', Qt::SkipEmptyParts)) {
        g = g.trimmed();
        if (g.isEmpty())
            continue;
        if (!s_->settings.modifierGroup(ss(g)))
            return fail(tr("There is no modifier group '%1' (see Manager → Modifier Groups).").arg(g));
        groups << g;
    }
    data.insert(u"modifierGroups"_s, groups);
    QVariantMap prices;
    for (const QString &line : record.value(u"periodPrices"_s).toString().split(u'\n', Qt::SkipEmptyParts)) {
        const QStringList kv = line.split(u'=');
        bool ok = false;
        const double price = kv.value(1).trimmed().remove(qs(s_->settings.currencySymbol)).toDouble(&ok);
        const QString period = kv.value(0).trimmed();
        if (period.isEmpty() && line.trimmed().isEmpty())
            continue;
        if (kv.size() != 2 || !ok || price < 0 || period.isEmpty())
            return fail(tr("Write meal period prices like \"dinner = 14.50\"."));
        if (!periodIds().contains(period))
            return fail(tr("There is no meal period '%1'.").arg(period));
        prices.insert(period, price);
    }
    data.insert(u"periodPrices"_s, prices);
    // "bun 1" / "Burger Buns 1" / "lettuce 0.5": an ingredient (id or name), then the amount.
    QVariantList recipe;
    for (const QString &line : record.value(u"recipe"_s).toString().split(u'\n', Qt::SkipEmptyParts)) {
        QString text = line.trimmed();
        if (text.isEmpty())
            continue;
        bool ok = false;
        const qsizetype space = text.lastIndexOf(u' ');
        double qty = space > 0 ? text.mid(space + 1).toDouble(&ok) : 0;
        QString what = ok ? text.left(space).trimmed() : text;
        if (!ok)
            qty = 1;
        if (qty <= 0)
            return fail(tr("Recipe amounts must be more than zero: \"%1\".").arg(text));
        const Ingredient *g = s_->ingredient(ss(what));
        if (!g) {
            for (const Ingredient &i : s_->ingredients)
                if (QString::compare(qs(i.name), what, Qt::CaseInsensitive) == 0)
                    g = &i;
        }
        if (!g)
            return fail(tr("There is no ingredient '%1' (see Manager → Inventory).").arg(what));
        recipe.append(QVariantMap{{u"ingredient"_s, qs(g->id)}, {u"qty"_s, qty}});
    }
    data.insert(u"recipe"_s, recipe);
    data.remove(u"autoSoldOut"_s);
    MenuItem item = menuItemFromJson(QJsonObject::fromVariantMap(data));
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
    if (!QStringList{u"server"_s, u"bartender"_s, u"cashier"_s, u"host"_s, u"busser"_s, u"manager"_s, u"admin"_s}
             .contains(role))
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
    std::set<std::string> allow, deny;
    for (const char *p : AllPermissions) {
        const QString v = record.value(u"perm:"_s + QString::fromLatin1(p)).toString();
        if (v == u"allow")
            allow.insert(p);
        else if (v == u"deny")
            deny.insert(p);
    }
    Employee check;
    check.role = ss(role);
    check.allow = allow;
    check.deny = deny;
    const bool isSelf = index >= 0 && user() && s_->employees[index].id == user()->id;
    if (isSelf && (!active || !check.can(perm::Manager)))
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
    e.allow = allow;
    e.deny = deny;
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

QVariantMap PosService::menuRecord(const MenuItem &m) const
{
    QVariantMap r = toJson(m).toVariantMap();
    QStringList groups;
    for (const std::string &g : m.modifierGroups)
        groups << qs(g);
    r.insert(u"modifierGroups"_s, groups.join(u", "));
    QStringList prices;
    for (const auto &[period, price] : m.periodPrices)
        prices << u"%1 = %2"_s.arg(qs(period), qs(price.toString()));
    r.insert(u"periodPrices"_s, prices.join(u'\n'));
    QStringList recipe;
    for (const RecipeLine &line : m.recipe)
        recipe << u"%1 %2"_s.arg(qs(line.ingredientId), QString::number(line.quantity, 'g', 8));
    r.insert(u"recipe"_s, recipe.join(u'\n'));
    r.remove(u"autoSoldOut"_s);
    for (const char16_t *k : {u"family", u"printer"}) {
        if (!r.contains(QString::fromUtf16(k)))
            r.insert(QString::fromUtf16(k), QString());
    }
    r.insert(u"modifier"_s, m.isModifier);
    r.insert(u"available"_s, m.available);
    return r;
}

QStringList PosService::groupIds() const
{
    QStringList out;
    for (const ModifierGroup &g : s_->settings.modifierGroups)
        out << qs(g.id);
    return out;
}

QStringList PosService::periodIds() const
{
    QStringList out;
    for (const MealPeriod &m : s_->settings.mealPeriods)
        out << qs(m.id);
    return out;
}

bool PosService::saveModifierGroupRecord(int index, const QVariantMap &record)
{
    auto &list = s_->settings.modifierGroups;
    if (index >= int(list.size()))
        return false;
    const QString name = record.value(u"name"_s).toString().trimmed();
    if (name.isEmpty())
        return fail(tr("The group needs a name."));
    ModifierGroup g;
    g.name = ss(name);
    g.min = std::max(0, record.value(u"min"_s).toInt());
    g.max = std::max(0, record.value(u"max"_s).toInt());
    if (g.max > 0 && g.min > g.max)
        return fail(tr("It can't require more choices than it allows."));
    for (const QString &line : record.value(u"options"_s).toString().split(u'\n', Qt::SkipEmptyParts)) {
        const qsizetype plus = line.lastIndexOf(u'+');
        QString optName = (plus > 0 ? line.left(plus) : line).trimmed();
        Money price;
        if (plus > 0) {
            bool ok = false;
            const double p = line.mid(plus + 1).trimmed().remove(qs(s_->settings.currencySymbol)).toDouble(&ok);
            if (!ok || p < 0)
                return fail(tr("Write option prices like \"Onion Rings + 1.50\"."));
            price = Money::fromCents(std::llround(p * 100.0));
        }
        if (!optName.isEmpty())
            g.options.push_back({ss(optName), price});
    }
    if (g.options.empty())
        return fail(tr("Add the options, one per line."));
    if (g.min > int(g.options.size()))
        return fail(tr("It requires more choices than it has options."));
    if (index >= 0) {
        g.id = list[index].id;
        list[index] = g;
    } else {
        const QString wanted = record.value(u"id"_s).toString().trimmed();
        g.id = ss(uniqueId(wanted.isEmpty() ? name : wanted, list, [](const ModifierGroup &x) { return x.id; }, -1));
        list.push_back(g);
    }
    settingsChanged();
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
    } else if (panel == u"inventory" && index >= 0 && index < int(s_->ingredients.size())) {
        const std::string id = s_->ingredients[index].id;
        s_->ingredients.erase(s_->ingredients.begin() + index);
        if (s_->sink) {
            s_->sink->deleteIngredient(id);
            for (int i = index; i < int(s_->ingredients.size()); ++i)
                s_->sink->saveIngredient(s_->ingredients[i], i);
        }
        refreshSoldOut();
    } else if (panel == u"modifierGroups" && index >= 0 && index < int(s_->settings.modifierGroups.size())) {
        s_->settings.modifierGroups.erase(s_->settings.modifierGroups.begin() + index);
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

bool PosService::factoryReset(const QString &confirm)
{
    if (!require(perm::Manager, tr("Factory reset")))
        return false;
    if (confirm.trimmed() != u"RESET")
        return fail(tr("Type RESET to confirm."));
    if (!s_->requestFactoryReset)
        return fail(tr("A factory reset is done on the computer that keeps the data."));
    if (!s_->requestFactoryReset())
        return fail(tr("The backup failed, so nothing was reset."));
    emit notice(tr("Backed up. Resetting and restarting..."));
    return true;
}

bool PosService::backupNow()
{
    if (!require(perm::Manager, tr("Backups")))
        return false;
    if (!s_->requestBackup || !s_->requestBackup())
        return fail(tr("Backups aren't set up on this store, or one is already running."));
    emit notice(tr("Backing up..."));
    return true;
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
