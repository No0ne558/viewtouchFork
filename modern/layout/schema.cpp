#include "layout/schema.hh"

#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace vt::layout::schema {

namespace {

QJsonArray options(std::initializer_list<std::pair<const char *, const char *>> values)
{
    QJsonArray arr;
    for (const auto &[value, text] : values)
        arr.append(QJsonObject{{u"value"_s, QString::fromUtf8(value)}, {u"text"_s, QString::fromUtf8(text)}});
    return arr;
}

QJsonArray options(const QStringList &values)
{
    QJsonArray arr;
    for (const QString &v : values)
        arr.append(QJsonObject{{u"value"_s, v}, {u"text"_s, v}});
    return arr;
}

QJsonObject field(const QString &path, const QString &label, const QString &type, const QString &group)
{
    return {{u"path"_s, path}, {u"label"_s, label}, {u"type"_s, type}, {u"group"_s, group}};
}

QJsonObject with(QJsonObject f, const QString &key, const QJsonValue &v)
{
    f.insert(key, v);
    return f;
}

QJsonObject intField(const QString &path, const QString &label, const QString &group, int min, int max)
{
    QJsonObject f = field(path, label, u"int"_s, group);
    f.insert(u"min"_s, min);
    f.insert(u"max"_s, max);
    return f;
}

const QJsonArray kShapes = options({{"rect", "Rectangle"}, {"rounded", "Rounded"}, {"circle", "Circle"},
                                    {"diamond", "Diamond"}, {"hexagon", "Hexagon"}, {"octagon", "Octagon"}});
const QJsonArray kBehaviors = options({{"blink", "Blink on touch"}, {"toggle", "Toggle on/off"},
                                       {"select", "Stay lit (one per page)"}, {"double", "Double touch"},
                                       {"passthrough", "Ignore touches"}, {"none", "No feedback"}});
const QJsonArray kFrames = options({{"raised", "Raised"}, {"inset", "Inset"}, {"border", "Border"},
                                    {"flat", "Flat"}, {"none", "None"}});
const QJsonArray kTextStyles = options({{"none", "Plain"}, {"embossed", "Embossed"}, {"outline", "Outline"}});
const QJsonArray kBold = QJsonArray{QJsonObject{{u"value"_s, true}, {u"text"_s, u"Bold"_s}},
                                    QJsonObject{{u"value"_s, false}, {u"text"_s, u"Regular"_s}}};
const QJsonArray kMealPeriods = options({{"", "(none)"}, {"breakfast", "Breakfast"}, {"lunch", "Lunch"},
                                         {"dinner", "Dinner"}, {"all", "All day"}});
const QJsonArray kJumpModes = options({{"push", "Go (remember this page)"}, {"replace", "Go (don't remember)"},
                                       {"back", "Back"}, {"home", "Home"}, {"index", "Menu index"},
                                       {"sequence", "Continue modifier sequence"}});
const QJsonArray kQualifiers = options({{"no", "No"}, {"extra", "Extra"}, {"lite", "Lite"}, {"side", "Side"},
                                        {"only", "Only"}, {"double", "Double"}});

// Style fields for one state. Normal gets the full set; selected and
// disabled only the keys that usually differ.
void appendStyleFields(QJsonArray &out, const QString &prefix, const QString &state, const QString &group)
{
    const QString p = prefix + state + u'.';
    auto inh = [](QJsonObject f) { return with(std::move(f), u"inheritable"_s, true); };

    out.append(inh(field(p + u"fill"_s, u"Color"_s, u"color"_s, group)));
    out.append(inh(field(p + u"texture"_s, u"Texture"_s, u"texture"_s, group)));
    out.append(inh(field(p + u"textColor"_s, u"Text color"_s, u"color"_s, group)));
    if (state == u"normal") {
        out.append(inh(field(p + u"font"_s, u"Font"_s, u"font"_s, group)));
        out.append(inh(intField(p + u"fontSize"_s, u"Text size"_s, group, 8, 200)));
        out.append(inh(with(field(p + u"bold"_s, u"Weight"_s, u"enum"_s, group), u"options"_s, kBold)));
        out.append(inh(with(field(p + u"textStyle"_s, u"Text effect"_s, u"enum"_s, group), u"options"_s, kTextStyles)));
    }
    out.append(inh(with(field(p + u"frame"_s, u"Frame"_s, u"enum"_s, group), u"options"_s, kFrames)));
    if (state == u"normal") {
        out.append(inh(intField(p + u"frameWidth"_s, u"Frame width"_s, group, 0, 32)));
        out.append(inh(field(p + u"borderColor"_s, u"Border color"_s, u"color"_s, group)));
        out.append(inh(intField(p + u"radius"_s, u"Corner radius"_s, group, 0, 200)));
    }
    out.append(inh(intField(p + u"shadow"_s, u"Shadow"_s, group, 0, 40)));
    out.append(inh(with(with(intField(p + u"opacity"_s, u"Opacity %"_s, group, 0, 100), u"scale"_s, 100),
                        u"default"_s, 1)));
}

void appendStyleGroups(QJsonArray &out, const QString &prefix, bool allStates)
{
    appendStyleFields(out, prefix, u"normal"_s, u"Look"_s);
    if (allStates) {
        appendStyleFields(out, prefix, u"selected"_s, u"Look when lit"_s);
        appendStyleFields(out, prefix, u"disabled"_s, u"Look when disabled"_s);
    }
}

} // namespace

QStringList basicKinds()
{
    return {u"button"_s, u"label"_s, u"image"_s, u"comment"_s};
}

QStringList widgetKinds()
{
    return {u"table"_s, u"tableGrid"_s, u"orderList"_s, u"loginPad"_s, u"guestCount"_s, u"checkList"_s,
            u"paymentPanel"_s, u"numPad"_s, u"keyboard"_s, u"splitCheck"_s, u"drawerPanel"_s,
            u"reportView"_s, u"endOfDay"_s, u"logoutPanel"_s, u"clock"_s, u"statusBar"_s,
            u"adminPanel"_s, u"kitchenDisplay"_s, u"customerInfo"_s};
}

QStringList allKinds()
{
    return basicKinds() + widgetKinds();
}

bool isWidgetKind(const QString &kind)
{
    return widgetKinds().contains(kind);
}

QStringList pageKinds()
{
    return {u"custom"_s, u"login"_s, u"tables"_s, u"guestCount"_s, u"index"_s, u"items"_s,
            u"modifier"_s, u"settle"_s, u"logout"_s, u"manager"_s, u"kitchen"_s, u"template"_s,
            u"library"_s};
}

QStringList pageRoles()
{
    return {u"login"_s, u"tables"_s, u"guestCount"_s, u"checkList"_s, u"settle"_s, u"logout"_s,
            u"manager"_s, u"bar"_s, u"kitchen"_s};
}

QJsonArray zoneFields(const QString &kind)
{
    const bool touchable = kind == u"button" || kind == u"image";
    const QString general = u"General"_s;
    QJsonArray out;

    if (kind == u"table") {
        out.append(with(field(u"label"_s, u"Table name"_s, u"string"_s, general), u"hint"_s,
                        u"Shown on the table, checks and kitchen tickets. Each table needs its own name."_s));
        out.append(with(intField(u"props.seats"_s, u"Seats"_s, general, 0, 99), u"hint"_s,
                        u"Shown while the table is free. 0 hides it."_s));
    } else {
        out.append(with(field(u"label"_s, kind == u"comment" ? u"Note"_s : u"Text"_s, u"text"_s, general),
                        u"hint"_s, isWidgetKind(kind) ? u"Optional caption"_s : QString()));
    }
    out.append(with(field(u"name"_s, u"Name"_s, u"string"_s, general), u"hint"_s, u"For your reference only"_s));
    if (touchable) {
        out.append(with(field(u"shape"_s, u"Shape"_s, u"enum"_s, general), u"options"_s, kShapes));
        out.append(with(field(u"behavior"_s, u"Touch behavior"_s, u"enum"_s, general), u"options"_s, kBehaviors));
        out.append(with(field(u"group"_s, u"Group"_s, u"string"_s, general), u"hint"_s,
                        u"Zones sharing a group act together"_s));
        out.append(with(field(u"hotkey"_s, u"Keyboard key"_s, u"string"_s, general), u"hint"_s,
                        u"Single key that presses this button"_s));
        out.append(with(field(u"imagePath"_s, u"Image"_s, u"string"_s, general), u"hint"_s,
                        u"qrc:/images/... or file:///path"_s));
        out.append(field(u"enabled"_s, u"Enabled"_s, u"bool"_s, general));
    } else if (kind != u"comment" && kind != u"label") {
        out.append(with(field(u"shape"_s, u"Shape"_s, u"enum"_s, general), u"options"_s, kShapes));
    }

    const QString geo = u"Position and size"_s;
    out.append(intField(u"rect.x"_s, u"X"_s, geo, 0, 7680));
    out.append(intField(u"rect.y"_s, u"Y"_s, geo, 0, 4320));
    out.append(intField(u"rect.w"_s, u"Width"_s, geo, 16, 7680));
    out.append(intField(u"rect.h"_s, u"Height"_s, geo, 16, 4320));
    out.append(with(intField(u"z"_s, u"Layer"_s, geo, -999, 999), u"hint"_s, u"Higher is drawn on top"_s));

    if (touchable)
        out.append(field(u"actions"_s, u"When touched"_s, u"actions"_s, u"Actions"_s));

    appendStyleGroups(out, u"style."_s, touchable);
    return out;
}

QJsonArray pageFields()
{
    const QString general = u"Page"_s;
    QJsonArray out;
    out.append(field(u"name"_s, u"Name"_s, u"string"_s, general));
    out.append(with(field(u"id"_s, u"Page ID"_s, u"string"_s, general), u"hint"_s,
                    u"Renaming updates every button that jumps here"_s));
    out.append(with(field(u"kind"_s, u"Page type"_s, u"enum"_s, general), u"options"_s, options(pageKinds())));
    QJsonArray roles = options({{"", "(none)"}});
    for (const QJsonValue &v : options(pageRoles()))
        roles.append(v);
    out.append(with(with(field(u"role"_s, u"System role"_s, u"enum"_s, general), u"options"_s, roles),
                    u"hint"_s, u"The app opens this page for that job"_s));
    out.append(with(field(u"templateId"_s, u"Template page"_s, u"page"_s, general), u"hint"_s,
                    u"Template zones show behind this page's zones"_s));
    out.append(with(field(u"mealPeriod"_s, u"Meal period"_s, u"enum"_s, general), u"options"_s, kMealPeriods));
    out.append(with(with(field(u"permission"_s, u"Who may open it"_s, u"enum"_s, general), u"options"_s,
                         options({{"", "Anyone logged in"}, {"public", "Anyone, even logged out (kitchen screens)"},
                                  {"check.settle", "Staff who take payments"},
                                  {"order.void", "Staff who void items"}, {"manager", "Managers"},
                                  {"layout.edit", "Page editors"}})),
                    u"hint"_s, u"Others get a message instead of the page"_s));
    out.append(intField(u"grid"_s, u"Snap grid"_s, general, 1, 128));
    out.append(intField(u"canvas.w"_s, u"Canvas width"_s, general, 320, 7680));
    out.append(intField(u"canvas.h"_s, u"Canvas height"_s, general, 240, 4320));

    const QString screens = u"Phones"_s;
    out.append(with(field(u"variantOf"_s, u"Phone version of"_s, u"page"_s, screens), u"hint"_s,
                    u"Phones show this page instead of that one (with For screens: Phones)"_s));
    out.append(with(with(field(u"formFactor"_s, u"For screens"_s, u"enum"_s, screens), u"options"_s,
                         options({{"", "All"}, {"phone", "Phones"}})),
                    u"hint"_s, u"Use a portrait canvas, e.g. 1080 × 2280"_s));
    out.append(with(intField(u"contentArea.x"_s, u"Content area X"_s, screens, 0, 7680), u"hint"_s,
                    u"Phone templates: where pages without a phone version get their buttons"_s));
    out.append(intField(u"contentArea.y"_s, u"Content area Y"_s, screens, 0, 7680));
    out.append(intField(u"contentArea.w"_s, u"Content area width"_s, screens, 0, 7680));
    out.append(intField(u"contentArea.h"_s, u"Content area height"_s, screens, 0, 7680));

    const QString bg = u"Background"_s;
    out.append(with(field(u"background.fill"_s, u"Color"_s, u"color"_s, bg), u"inheritable"_s, true));
    out.append(with(field(u"background.texture"_s, u"Texture"_s, u"texture"_s, bg), u"inheritable"_s, true));

    QJsonArray defaults;
    appendStyleFields(defaults, u"style."_s, u"normal"_s, u"Default button look"_s);
    for (const QJsonValue &v : defaults)
        out.append(v);
    return out;
}

QJsonArray themeFields()
{
    QJsonArray out;
    out.append(field(u"name"_s, u"Theme name"_s, u"string"_s, u"Theme"_s));
    out.append(with(field(u"background.fill"_s, u"Background color"_s, u"color"_s, u"Theme"_s), u"inheritable"_s, true));
    out.append(with(field(u"background.texture"_s, u"Background texture"_s, u"texture"_s, u"Theme"_s), u"inheritable"_s, true));
    appendStyleGroups(out, u"style."_s, true);
    return out;
}

QJsonArray actionTypes()
{
    auto type = [](const QString &t, const QString &label, QJsonArray fields) {
        return QJsonObject{{u"type"_s, t}, {u"label"_s, label}, {u"fields"_s, fields}};
    };
    const QString g;   // no groups inside an action card
    QJsonArray roles = options({{"", "(use page)"}});
    for (const QJsonValue &v : options(pageRoles()))
        roles.append(v);

    return {
        type(u"jump"_s, u"Go to page"_s,
             {with(field(u"mode"_s, u"How"_s, u"enum"_s, g), u"options"_s, kJumpModes),
              field(u"page"_s, u"Page"_s, u"page"_s, g),
              with(with(field(u"role"_s, u"Or system page"_s, u"enum"_s, g), u"options"_s, roles),
                   u"hint"_s, u"Used when no page is chosen"_s)}),
        type(u"addItem"_s, u"Add menu item"_s,
             {field(u"item"_s, u"Item"_s, u"string"_s, g),
              with(field(u"modifierSequence"_s, u"Then show"_s, u"pageList"_s, g), u"hint"_s,
                   u"Modifier pages, in order"_s)}),
        type(u"qualifier"_s, u"Qualifier"_s,
             {with(field(u"qualifier"_s, u"Qualifier"_s, u"enum"_s, g), u"options"_s, kQualifiers)}),
        type(u"tender"_s, u"Payment"_s,
             {field(u"tender"_s, u"Tender"_s, u"string"_s, g),
              with(field(u"amount"_s, u"Amount (cents)"_s, u"int"_s, g), u"hint"_s, u"Empty = balance due"_s)}),
        type(u"command"_s, u"Command"_s,
             {with(field(u"name"_s, u"Command"_s, u"enum"_s, g), u"options"_s,
                   options({{"sendOrder", "Send order"}, {"voidItem", "Void item"}, {"printReceipt", "Print receipt"},
                            {"closeCheck", "Close check"}, {"removePayment", "Undo payment"},
                            {"noSale", "Open drawer (no sale)"}, {"openDrawerSession", "Start drawer (bank)"},
                            {"countDrawer", "Count drawer"}, {"endOfDay", "End of day"},
                            {"login", "Log in"}, {"logout", "Log out"}, {"clockIn", "Clock in"},
                            {"clockOut", "Clock out"}, {"startCheck", "Start table check"},
                            {"startQuick", "Start quick check"}, {"startTakeout", "Start takeout"},
                            {"releaseCheck", "Put check away"}, {"addComment", "Add note"},
                            {"openAdmin", "Admin screen"}, {"editMode", "Edit pages"},
                            {"addTip", "Add tip (args.percent)"}, {"gratuity", "Gratuity (args.percent)"},
                            {"payout", "Pay out of drawer"}, {"paidIn", "Pay into drawer"},
                            {"cashOutTips", "Cash out my tips"}, {"closeApp", "Close ViewTouch (manager; leaves a kiosk)"}, {"clearText", "Clear typed text"}, {"recallTicket", "Recall kitchen ticket"},
                            {"startDelivery", "Start delivery"}}))}),
    };
}

} // namespace vt::layout::schema
