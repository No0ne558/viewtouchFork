#include <catch2/catch_test_macros.hpp>

#include "layoutcontroller.hh"
#include "app/i18n.hh"
#include "language.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QDate>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::layout::Layout;

// M3 acceptance: log in -> table -> order with modifiers -> send -> pay ->
// close, through the real pages and zone actions.

namespace {

Layout seedLayout()
{
    auto l = Layout::loadDirectory(QStringLiteral(VTM_SEED_DIR));
    REQUIRE(l);
    return *l;
}

struct Session {
    test::RecordingSink sink;
    app::PosService pos{test::seedPosData(true), &sink};
    LayoutController c{seedLayout()};

    Session()
    {
        c.setMealPeriod(u"lunch"_s);
        c.setPos(&pos);
    }

    void pin(const char *digits)
    {
        for (const char *d = digits; *d; ++d)
            pos.pinKey(QString(QChar(*d)));
    }

    bool zoneCurrent(const QString &zoneId)
    {
        ZoneModel *m = c.zones();
        for (int r = 0; r < m->rowCount(); ++r) {
            if (m->data(m->index(r), ZoneModel::ZoneIdRole).toString() == zoneId)
                return m->data(m->index(r), ZoneModel::CurrentRole).toBool();
        }
        FAIL("no zone " << zoneId.toStdString());
        return false;
    }
};

} // namespace

TEST_CASE("Flow: login, table, order with modifiers, send, pay, close", "[flow]")
{
    Session s;
    CHECK(s.c.pageId() == u"login"_s);

    // Nothing opens before logging in.
    CHECK_FALSE(s.c.showPage(u"tables"_s));
    CHECK(s.c.statusText().contains(u"Log in"_s));

    s.pin("1111");
    s.c.activate(u"start"_s);                        // "Log In"
    REQUIRE(s.pos.loggedIn());
    CHECK(s.c.pageId() == u"tables"_s);
    CHECK_FALSE(s.c.canGoBack());                    // tables is home while logged in

    s.c.selectTable(u"T3"_s);
    CHECK(s.c.pageId() == u"guest-count"_s);
    s.pos.entryKey(u"2"_s);
    s.c.activate(u"start"_s);                        // startCheck + go to menu
    CHECK(s.c.pageId() == u"index-lunch"_s);
    REQUIRE(s.pos.hasCheck());
    CHECK(s.pos.checkInfo()[u"label"_s].toString() == u"T3"_s);
    CHECK(s.pos.checkInfo()[u"guests"_s].toInt() == 2);

    s.c.activate(u"cat-items-burgers"_s);
    s.c.activate(u"item-1"_s);                       // Classic Burger: its choices
    CHECK(s.c.pageId() == u"modifiers"_s);
    REQUIRE(s.pos.chooseOption(u"temperature"_s, 1)); // Medium Rare
    REQUIRE(s.pos.chooseOption(u"side"_s, 2));        // Onion Rings (+1.00)
    s.c.finishChoosing();
    CHECK(s.c.pageId() == u"items-burgers"_s);

    // Qualifier button stays lit while pending, clears after use.
    s.c.activate(u"flow-no"_s);
    CHECK(s.zoneCurrent(u"flow-no"_s));
    CHECK_FALSE(s.zoneCurrent(u"flow-extra"_s));
    s.c.activate(u"flow-no"_s);                      // touching again cancels it
    CHECK_FALSE(s.zoneCurrent(u"flow-no"_s));

    const QVariantList lines = s.pos.lines();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].toMap()[u"modifiers"_s].toList().size() == 2);
    CHECK(lines[0].toMap()[u"price"_s].toString() == u"$12.50"_s);

    s.c.activate(u"flow-send"_s);
    CHECK(s.pos.lines()[0].toMap()[u"sent"_s].toBool());

    REQUIRE(s.pos.openDrawerSession());              // cash needs an open drawer
    s.c.activate(u"flow-pay"_s);
    CHECK(s.c.pageId() == u"settle"_s);
    // 12.50 + 8.25% (1.03125 -> 1.03) = 13.53; pay $20 cash.
    CHECK(s.pos.totals()[u"total"_s].toString() == u"$13.53"_s);
    s.pos.entryKey(u"2000"_s);
    s.c.activate(u"tender-cash"_s);
    CHECK(s.pos.totals()[u"change"_s].toString() == u"$6.47"_s);

    s.c.activate(u"close"_s);
    CHECK_FALSE(s.pos.hasCheck());
    CHECK(s.c.pageId() == u"tables"_s);              // back to the floor
    CHECK_FALSE(s.pos.tableStatus(u"T3"_s)[u"open"_s].toBool());

    REQUIRE(s.sink.checks.size() == 1);
    const core::Check &saved = s.sink.checks.begin()->second;
    CHECK(saved.status == core::CheckStatus::Closed);
    CHECK(saved.label == "T3");
    CHECK(saved.lines[0].modifiers.size() == 2);
}

TEST_CASE("Flow: a failed step stops the rest of a button's actions", "[flow]")
{
    Session s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    // Guest count "Start Order" without choosing a table: startCheck fails,
    // so the jump to the menu must not happen.
    REQUIRE(s.c.showPage(u"guest-count"_s));
    s.c.activate(u"start"_s);
    CHECK(s.c.pageId() == u"guest-count"_s);
    CHECK_FALSE(s.pos.hasCheck());
}

TEST_CASE("Flow: permissions guard pages, voids and editing", "[flow]")
{
    Session s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));   // server
    CHECK(s.c.pageId() == u"tables"_s);

    s.c.activate(u"manager"_s);
    CHECK(s.c.pageId() == u"tables"_s);
    CHECK(s.c.statusText().contains(u"cannot open"_s));

    CHECK_FALSE(s.c.requestEditMode());
    CHECK_FALSE(s.c.editing());

    s.c.activate(u"logout"_s);                // to the logout page
    CHECK(s.c.pageId() == u"logout"_s);
    s.c.activate(u"logout"_s);                // the Log Out command
    CHECK_FALSE(s.pos.loggedIn());
    CHECK(s.c.pageId() == u"login"_s);

    REQUIRE(s.pos.loginWithPin(u"1234"_s));   // manager
    s.c.activate(u"manager"_s);
    CHECK(s.c.pageId() == u"manager"_s);
    s.c.activate(u"edit-pages"_s);
    CHECK(s.c.editing());
}

TEST_CASE("Flow: quick order from the floor, open checks list", "[flow]")
{
    Session s;
    REQUIRE(s.pos.loginWithPin(u"2222"_s));
    s.c.activate(u"quick"_s);
    CHECK(s.c.pageId() == u"index-lunch"_s);
    REQUIRE(s.pos.hasCheck());
    s.c.activate(u"cat-items-drinks"_s);
    s.c.activate(u"item-7"_s);                // Draft Beer
    s.c.activate(u"flow-tables"_s);           // release and go back to the floor
    CHECK(s.c.pageId() == u"tables"_s);
    CHECK_FALSE(s.pos.hasCheck());

    s.c.activate(u"checks"_s);
    CHECK(s.c.pageId() == u"check-list"_s);
    REQUIRE(s.pos.openChecks().size() == 1);
    s.c.openCheck(s.pos.openChecks()[0].toMap()[u"id"_s].toLongLong());
    CHECK(s.c.pageId() == u"index-lunch"_s);
    CHECK(s.pos.lines().size() == 1);
}

// --- through the real widgets -------------------------------------------------

namespace {

struct Screen : Session {
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;

    explicit Screen(bool touchKeyboard = false, int width = 1600, int height = 900, const QString &formFactor = {})
    {
        if (!formFactor.isEmpty())
            c.setFormFactorOverride(formFactor);
        engine.setInitialProperties({{u"controller"_s, QVariant::fromValue(&c)},
                                     {u"width"_s, width}, {u"height"_s, height}, {u"touchKeyboard"_s, touchKeyboard}});
        engine.loadFromModule("ViewTouch", "Main");
        REQUIRE_FALSE(engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        REQUIRE(window);
        REQUIRE(QTest::qWaitForWindowExposed(window));
        QTest::qWait(50);
    }

    QPoint at(qreal cx, qreal cy)
    {
        auto *surface = window->findChild<QQuickItem *>(u"pageSurface"_s);
        REQUIRE(surface);
        return surface->mapToScene(QPointF(cx, cy)).toPoint();
    }

    void tapCanvas(qreal cx, qreal cy)
    {
        QTest::mouseClick(window, Qt::LeftButton, {}, at(cx, cy));
        QTest::qWait(30);
    }

    // Set VTM_SHOTS=<dir> to save screenshots of the flow for review.
    void shot(const char *name)
    {
        const QByteArray dir = qgetenv("VTM_SHOTS");
        if (!dir.isEmpty())
            window->grabWindow().save(QString::fromLocal8Bit(dir) + u'/' + QString::fromLatin1(name) + u".png"_s);
    }

    // Depth-first over the visual tree (Repeater items are not QObject children).
    static QQuickItem *findKey(QQuickItem *root, const QString &text)
    {
        for (QQuickItem *item : root->childItems()) {
            if (!item->isVisible())
                continue;
            if (QByteArray(item->metaObject()->className()).startsWith("WidgetKey")
                && item->property("text").toString() == text)
                return item;
            if (QQuickItem *hit = findKey(item, text))
                return hit;
        }
        return nullptr;
    }

    // A visible item whose `property` is `value` (a Text with that text, a
    // field with that placeholder...).
    static QQuickItem *findBy(QQuickItem *root, const char *property, const QString &value)
    {
        for (QQuickItem *item : root->childItems()) {
            if (!item->isVisible())
                continue;
            if (item->property(property).toString() == value)
                return item;
            if (QQuickItem *hit = findBy(item, property, value))
                return hit;
        }
        return nullptr;
    }

    void tapItem(QQuickItem *item)
    {
        REQUIRE(item);
        const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, {}, centre.toPoint());
        QTest::qWait(30);
    }

    // Tap on-screen keyboard keys (their labels), one per character.
    void typeOnScreen(const QString &labels)
    {
        for (QChar ch : labels)
            tapItem(findBy(window->contentItem(), "text", QString(ch)));
    }

    // Tap a visible keypad key by its text.
    void tapKey(const QString &text)
    {
        QQuickItem *found = findKey(window->contentItem(), text);
        REQUIRE(found);
        const QPointF centre = found->mapToScene(QPointF(found->width() / 2, found->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, {}, centre.toPoint());
        QTest::qWait(30);
    }
};

} // namespace

TEST_CASE("UI flow: keypad login, table map, guest pad, menu, pay", "[flow][ui]")
{
    Screen s;
    s.shot("1-login");
    for (const char *k : {"1", "1", "1", "1"})
        s.tapKey(QString::fromLatin1(k));
    CHECK(s.pos.pinLength() == 4);
    s.tapKey(u"Enter"_s);
    REQUIRE(s.pos.loggedIn());
    CHECK(s.c.pageId() == u"tables"_s);

    // Table zone T3 at (656, 96, 320x200).
    s.tapCanvas(16 + 640 + 160, 16 + 80 + 100);
    CHECK(s.c.pageId() == u"guest-count"_s);
    s.tapKey(u"3"_s);
    CHECK(s.pos.entryGuests() == 3);
    s.tapCanvas(970 + 145, 888 + 60);         // Start Order
    CHECK(s.c.pageId() == u"index-lunch"_s);

    s.tapCanvas(592 + 444 + 200, 192 + 120);  // Salads
    CHECK(s.c.pageId() == u"items-salads"_s);
    s.tapCanvas(592 + 150, 192 + 90);         // House Salad: it asks for a dressing
    CHECK(s.c.pageId() == u"modifiers"_s);
    s.tapKey(u"Done"_s);                      // not without the dressing
    CHECK(s.c.pageId() == u"modifiers"_s);
    s.tapKey(u"Ranch"_s);
    s.shot("2-choose");
    s.tapKey(u"Done"_s);
    CHECK(s.c.pageId() == u"items-salads"_s);
    CHECK(s.pos.lines().size() == 1);
    s.tapCanvas(592 + 444 + 150, 192 + 90);   // Caesar: a protein is optional
    s.tapKey(u"Done"_s);
    CHECK(s.pos.lines().size() == 2);
    s.shot("2-order");

    // Back to the floor: T3 shows as ours; touching it reopens the check.
    s.tapCanvas(16 + 110, 980 + 42);          // Tables
    CHECK(s.c.pageId() == u"tables"_s);
    CHECK_FALSE(s.pos.hasCheck());
    s.shot("4-tables");
    s.tapCanvas(16 + 640 + 160, 16 + 80 + 100);
    CHECK(s.c.pageId() == u"index-lunch"_s);
    CHECK(s.pos.lines().size() == 2);

    s.tapCanvas(16 + 7 * 237 + 110, 980 + 42);   // Pay
    CHECK(s.c.pageId() == u"settle"_s);
    s.tapCanvas(1468 + 218, 16 + 126 + 55);      // Credit Card (exact balance)
    CHECK(s.pos.totals()[u"balanceCents"_s].toLongLong() == 0);
    s.shot("3-settle");
    s.tapCanvas(932 + 260, 788 + 60);            // Close Check
    CHECK_FALSE(s.pos.hasCheck());
    CHECK(s.c.pageId() == u"tables"_s);
    CHECK(s.sink.checks.begin()->second.status == core::CheckStatus::Closed);
}

TEST_CASE("UI: seats and courses on the order screen; Fire sends the held course", "[flow][ui][courses]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.selectTable(u"T2"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    REQUIRE(s.c.jumpTo(u"items-salads"_s));
    QTest::qWait(30);

    s.tapKey(u"+"_s);                                   // seat 1
    CHECK(s.pos.checkInfo()[u"seat"_s] == 1);
    s.tapCanvas(592 + 150, 192 + 90);                   // House Salad, seat 1
    s.tapKey(u"Ranch"_s);
    s.tapKey(u"Done"_s);
    s.tapKey(u"+"_s);                                   // seat 2
    s.tapKey(u"2"_s);                                   // course 2
    s.tapCanvas(592 + 444 + 150, 192 + 90);             // Caesar, seat 2, course 2
    s.tapKey(u"Done"_s);
    const QVariantList lines = s.pos.lines();
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].toMap()[u"seat"_s] == 1);
    CHECK(lines[1].toMap()[u"seat"_s] == 2);
    CHECK(lines[1].toMap()[u"held"_s].toBool());

    REQUIRE(s.pos.sendOrder());
    QTest::qWait(30);
    s.shot("5-courses");
    s.tapKey(u"Fire Course 2"_s);
    CHECK(s.pos.lines()[1].toMap()[u"sent"_s].toBool());
}

TEST_CASE("UI: the on-screen keyboard finds a customer; a gift card by its number", "[flow][ui][customers]")
{
    Screen s(true);
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.pos.saveCustomer({{u"name"_s, u"Dana Lee"_s}, {u"phone"_s, u"555-010-1234"_s}, {u"note"_s, u"no onions"_s}}));
    REQUIRE(s.pos.saveCustomer({{u"name"_s, u"Alex Kim"_s}, {u"phone"_s, u"555-777-0000"_s}}));
    REQUIRE(s.c.jumpTo(u"customers"_s));
    QTest::qWait(50);

    auto *keyboard = s.window->findChild<QQuickItem *>(u"touchKeys"_s);
    REQUIRE(keyboard);
    CHECK_FALSE(keyboard->isVisible());
    s.tapItem(Screen::findBy(s.window->contentItem(), "placeholderText", u"Phone or name…"_s));
    CHECK(keyboard->isVisible());                       // a text field is being typed in
    s.typeOnScreen(u"Dan"_s);                          // capital first, then small letters
    QTest::qWait(400);
    REQUIRE(s.pos.customerResults().size() == 1);
    s.tapItem(Screen::findBy(s.window->contentItem(), "text", u"Dana Lee"_s));
    QTest::qWait(50);
    CHECK(s.pos.customerInfo()[u"name"_s] == u"Dana Lee"_s);
    s.shot("6-customers");

    REQUIRE(s.c.jumpTo(u"gift-card"_s));
    QTest::qWait(50);
    s.tapItem(Screen::findBy(s.window->contentItem(), "placeholderText", u"Type or swipe…"_s));
    REQUIRE(keyboard->isVisible());
    CHECK(Screen::findBy(keyboard, "text", u"Done"_s));   // number fields get the number pad
    s.typeOnScreen(u"60012"_s);
    s.tapItem(Screen::findBy(keyboard, "text", u"Done"_s));   // Enter looks the card up
    CHECK(s.pos.giftCardInfo()[u"number"_s] == u"60012"_s);
    CHECK_FALSE(keyboard->isVisible());
    s.shot("7-gift-card");
}

TEST_CASE("UI: the host stand seats the next party at the best free table", "[flow][ui][waitlist]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.pos.addToWaitlist({{u"name"_s, u"Dana"_s}, {u"size"_s, 4}, {u"phone"_s, u"555-0101"_s}}) > 0);
    REQUIRE(s.pos.addToWaitlist({{u"name"_s, u"Alex"_s}, {u"size"_s, 2}, {u"note"_s, u"booth please"_s}}) > 0);
    const QString tomorrow = QDate::currentDate().addDays(1).toString(u"yyyy-MM-dd"_s) + u" 19:30"_s;
    REQUIRE(s.pos.addReservation({{u"name"_s, u"Lee"_s}, {u"size"_s, 6}, {u"at"_s, tomorrow}}) > 0);
    REQUIRE(s.c.jumpTo(u"host"_s));
    QTest::qWait(50);
    s.shot("8-host");

    s.tapItem(Screen::findBy(s.window->contentItem(), "text", u"Dana  ·  4 people"_s));
    s.tapItem(Screen::findBy(s.window->contentItem(), "text", u"Seat Them…"_s));
    QTest::qWait(50);
    s.shot("9-host-seat");
    // Four people: the smallest free table that fits (T3, 4 seats) comes first.
    s.tapItem(Screen::findBy(s.window->contentItem(), "text", u"T3\n4 seats"_s));
    CHECK(s.pos.tableStatus(u"T3"_s)[u"open"_s].toBool());
    CHECK(s.pos.waitlistInfo()[u"waiting"_s].toList().size() == 1);
}

TEST_CASE("UI: the customer display shows the order, asks for a tip, says thank you", "[flow][ui][display]")
{
    Session s;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{u"pos"_s, QVariant::fromValue(static_cast<app::PosSession *>(&s.pos))}});
    engine.loadFromModule("ViewTouch", "CustomerDisplayWindow");
    REQUIRE_FALSE(engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    REQUIRE(window);
    window->resize(1280, 720);
    REQUIRE(QTest::qWaitForWindowExposed(window));
    const QByteArray dir = qgetenv("VTM_SHOTS");
    const auto shot = [&](const char *name) {
        QTest::qWait(60);
        if (!dir.isEmpty())
            window->grabWindow().save(QString::fromLocal8Bit(dir) + u'/' + QString::fromLatin1(name) + u".png"_s);
    };
    std::function<QQuickItem *(QQuickItem *, const QString &)> find = [&](QQuickItem *root, const QString &text) -> QQuickItem * {
        for (QQuickItem *i : root->childItems()) {
            if (!i->isVisible())
                continue;
            if (i->property("text").toString() == text)
                return i;
            if (QQuickItem *hit = find(i, text))
                return hit;
        }
        return nullptr;
    };
    std::function<QQuickItem *(QQuickItem *, const QString &)> named = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        for (QQuickItem *i : root->childItems()) {
            if (i->isVisible() && i->objectName() == name)
                return i;
            if (QQuickItem *hit = named(i, name))
                return hit;
        }
        return nullptr;
    };
    const auto tap = [&](const QString &text) {
        QQuickItem *item = named(window->contentItem(), u"guestKey-"_s + text);   // a keypad key, else by its text
        if (!item)
            item = find(window->contentItem(), text);
        INFO(text.toStdString());
        REQUIRE(item);
        QTest::mouseClick(window, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        QTest::qWait(30);
    };
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    s.pos.entryKey(u"10000"_s);
    REQUIRE(s.pos.openDrawerSession());
    shot("10-display-welcome");

    REQUIRE(s.pos.selectTable(u"T4"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    s.pos.addItem(u"bacon-burger"_s);
    REQUIRE(s.pos.chooseOption(u"temperature"_s, 1));   // Medium Rare
    REQUIRE(s.pos.chooseOption(u"side"_s, 2));          // Onion Rings (+1.00)
    REQUIRE(s.pos.finishChoosing());
    s.pos.addItem(u"house-salad"_s);
    REQUIRE(s.pos.chooseOption(u"dressing"_s, 0));
    REQUIRE(s.pos.chooseOption(u"salad-protein"_s, 0));
    REQUIRE(s.pos.finishChoosing());
    s.pos.addItem(u"water"_s);
    // The guest joins rewards with their phone number.
    tap(u"Earn rewards: touch to add your phone"_s);
    for (const char *k : {"5", "5", "5", "0", "1", "0", "1", "2", "3", "4"})
        tap(QString::fromLatin1(k));
    tap(u"OK"_s);
    CHECK(s.pos.customerPrompt()[u"loyalty"_s].toMap()[u"member"_s].toString() == u"...1234"_s);   // never the whole number
    shot("11-display-order");

    REQUIRE(s.pos.askForTip());
    shot("12-display-tip");
    tap(u"Custom amount"_s);
    for (const char *k : {"5", "0", "0"})
        tap(QString::fromLatin1(k));
    tap(u"OK"_s);
    CHECK(s.pos.customerPrompt()[u"tip"_s] == u"$5.00"_s);
    const int earning = s.pos.customerPrompt()[u"loyalty"_s].toMap()[u"earning"_s].toInt();
    CHECK(earning > 20);
    REQUIRE(s.pos.tender(u"credit"_s));
    REQUIRE(s.pos.closeCheck());
    shot("13-display-thanks");
    CHECK(find(window->contentItem(), u"You earned %1 points"_s.arg(earning)));
    tap(u"No receipt"_s);
    CHECK_FALSE(find(window->contentItem(), u"Print receipt"_s));
}

TEST_CASE("UI: the kitchen display with a rush ticket and the all-day counts", "[flow][ui][kitchen]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    const auto order = [&](const QString &table, std::initializer_list<const char *> items, bool rush = false) {
        REQUIRE(s.pos.selectTable(table) == app::PosService::TableNeedsGuests);
        REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
        for (const char *i : items) {
            s.pos.addItem(QString::fromLatin1(i));
            for (const QVariant &g : s.pos.choosingInfo()[u"groups"_s].toList())   // the first of what's required
                if (!g.toMap()[u"done"_s].toBool())
                    s.pos.chooseOption(g.toMap()[u"id"_s].toString(), 0);
            s.pos.finishChoosing();
        }
        if (rush)
            REQUIRE(s.pos.toggleFlag(u"rush"_s));
        REQUIRE(s.pos.sendOrder());
        s.pos.releaseCheck();
    };
    order(u"T1"_s, {"cobb", "caesar"});
    order(u"T2"_s, {"cobb", "cobb", "water"});
    order(u"T3"_s, {"caesar"}, true);
    order(u"T4"_s, {"bacon-burger", "cheeseburger"});
    REQUIRE(s.c.jumpTo(u"kitchen"_s));
    QTest::qWait(50);
    s.tapKey(u"All Day"_s);
    QTest::qWait(50);
    s.shot("14-kitchen");
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"RUSH"_s));
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"All day"_s));
}

TEST_CASE("UI: the week's schedule; adding a shift by touch", "[flow][ui][schedule]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    const QDate today = QDate::currentDate();
    const auto day = [&](int d) { return today.addDays(d).toString(u"yyyy-MM-dd"_s); };
    REQUIRE(s.pos.addShift({{u"employeeId"_s, u"sam"_s}, {u"start"_s, day(0) + u" 16:00"_s}, {u"end"_s, day(0) + u" 22:00"_s}}));
    REQUIRE(s.pos.addShift({{u"employeeId"_s, u"riley"_s}, {u"start"_s, day(0) + u" 17:00"_s}, {u"end"_s, day(0) + u" 23:30"_s},
                            {u"note"_s, u"close"_s}}));
    REQUIRE(s.pos.addShift({{u"employeeId"_s, u"jo"_s}, {u"start"_s, day(1) + u" 18:00"_s}, {u"end"_s, day(1) + u" 01:00"_s}}));
    REQUIRE(s.c.jumpTo(u"admin-schedule"_s));
    QTest::qWait(50);
    const int before = int(s.pos.shared()->shifts.size());
    s.tapItem(Screen::findBy(s.window->contentItem(), "text", u"Add Shift"_s));
    CHECK(int(s.pos.shared()->shifts.size()) == before + 1);   // the first person, today, 4 - 10 PM
    QTest::qWait(50);
    s.shot("15-schedule");
}

TEST_CASE("UI: a month's report beside last year", "[flow][ui][range]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    s.pos.entryKey(u"10000"_s);
    REQUIRE(s.pos.openDrawerSession());
    for (const char *item : {"cobb", "caesar", "cobb"}) {
        REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
        s.pos.addItem(QString::fromLatin1(item));
        REQUIRE(s.pos.tender(u"cash"_s));
        REQUIRE(s.pos.closeCheck());
    }
    REQUIRE(s.c.jumpTo(u"reports"_s));
    QTest::qWait(50);
    s.tapKey(u"Items"_s);
    s.tapKey(u"This Month"_s);
    s.tapKey(u"vs Last Year"_s);
    for (int i = 0; i < 100 && s.pos.rangeReport()[u"loading"_s].toBool(); ++i)
        QTest::qWait(20);
    QTest::qWait(50);
    CHECK(s.pos.rangeReport()[u"checks"_s] == 3);
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"Change"_s));
    s.shot("16-range-report");
}

TEST_CASE("UI: a guest orders on the self-order kiosk", "[flow][ui][kiosk]")
{
    Screen s;
    s.pos.enableSelfOrder();
    QTest::qWait(60);
    auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    QQuickItem *kiosk = find(u"selfOrder"_s);
    REQUIRE(kiosk);
    CHECK(kiosk->isVisible());
    s.shot("20-kiosk-welcome");

    s.tapItem(find(u"kioskAttract"_s));                  // "Touch to Order"
    QTest::qWait(60);
    s.tapItem(find(u"kioskForHere"_s));
    REQUIRE(s.pos.selfOrderInfo()[u"ordering"_s].toBool());
    QTest::qWait(60);
    // A burger asks how it's cooked and for a side; the guest touches them.
    const auto burger = [&](const QString &name, const QString &temperature, const QString &side) {
        s.tapItem(Screen::findBy(kiosk, "text", name));   // the kiosk's card, not the page behind
        QTest::qWait(60);
        QQuickItem *done = find(u"kioskChoicesDone"_s);
        REQUIRE(done);
        CHECK(done->isVisible());
        CHECK_FALSE(done->isEnabled());                   // nothing chosen yet
        s.tapItem(Screen::findBy(kiosk, "text", temperature));
        s.tapItem(Screen::findBy(kiosk, "text", side));
        QTest::qWait(30);
        CHECK(done->isEnabled());
        s.tapItem(done);
    };
    burger(u"Classic Burger"_s, u"Medium Rare"_s, u"Fries"_s);
    s.tapItem(Screen::findBy(kiosk, "text", u"Cheeseburger"_s));
    QTest::qWait(60);
    s.tapItem(Screen::findBy(kiosk, "text", u"Well Done"_s));
    s.shot("21b-kiosk-choices");
    s.tapItem(Screen::findBy(kiosk, "text", u"Onion Rings\n+$1.00"_s));
    s.tapItem(find(u"kioskChoicesDone"_s));
    REQUIRE(s.pos.lines().size() == 2);
    CHECK(s.pos.lines()[0].toMap()[u"modifiers"_s].toList().size() == 2);   // Medium Rare, Fries
    s.shot("21-kiosk-menu");
    s.tapItem(find(u"kioskRemove"_s));                   // changed their mind
    REQUIRE(s.pos.lines().size() == 1);

    s.tapItem(find(u"kioskReview"_s));
    QTest::qWait(60);
    for (const char ch : {'L', 'e', 'e'})
        QTest::keyClick(s.window, ch);
    QTest::qWait(30);
    s.shot("22-kiosk-name");
    const qint64 id = s.pos.checkInfo()[u"id"_s].toLongLong();
    s.tapItem(find(u"kioskPlace"_s));
    QTest::qWait(60);
    QQuickItem *number = find(u"kioskNumber"_s);
    REQUIRE(number);
    CHECK(number->isVisible());
    CHECK(number->property("text").toString() == QString::number(id));
    CHECK(s.pos.shared()->open.at(id).customer.name == "Lee");
    s.shot("23-kiosk-number");

    // Staff: hold the top-left corner, then a manager's PIN.
    QTest::mousePress(s.window, Qt::LeftButton, {}, QPoint(10, 10));
    QTest::qWait(3300);
    QTest::mouseRelease(s.window, Qt::LeftButton, {}, QPoint(10, 10));
    QTest::qWait(50);
    for (const char *key : {"approvalKey-1", "approvalKey-2", "approvalKey-3", "approvalKey-4", "approvalKey-OK"})
        s.tapItem(find(QString::fromLatin1(key)));
    QTest::qWait(50);
    CHECK_FALSE(s.pos.selfOrderInfo()[u"on"_s].toBool());
    CHECK_FALSE(kiosk->isVisible());
}

TEST_CASE("UI: clocking in with two jobs asks which one", "[flow][ui][pay]")
{
    Screen s;
    for (const char *k : {"4", "4", "4", "4"})   // Jo: bartender, or server
        s.pos.pinKey(QString::fromLatin1(k));
    REQUIRE(s.pos.clockIn());
    QTest::qWait(60);
    QPointer<QQuickItem> server = Screen::findBy(s.window->contentItem(), "objectName", u"jobKey-server"_s);
    REQUIRE(server);
    CHECK(server->isVisible());
    s.shot("24-which-job");
    s.tapItem(server);
    QTest::qWait(60);
    CHECK(s.pos.clockInJobs().isEmpty());
    CHECK((!server || !server->isVisible()));   // the choice is gone
    REQUIRE_FALSE(s.pos.shared()->punches.empty());
    CHECK(s.pos.shared()->punches.back().job == "server");
}

TEST_CASE("UI: the screen speaks the language of whoever logs in", "[flow][ui][i18n]")
{
    Screen s;
    vt::i18n::install();
    vt::ui::followLanguage(&s.engine, &s.c);
    auto shows = [&](const QString &text) { return Screen::findBy(s.window->contentItem(), "text", text) != nullptr; };
    CHECK(shows(u"Clock In"_s));                          // the store's: English
    REQUIRE(s.pos.loginWithPin(u"5555"_s));               // Rosa
    QTest::qWait(50);
    REQUIRE(s.c.jumpTo(u"tables"_s));
    QTest::qWait(50);
    CHECK(shows(u"Orden rápida"_s));                      // page buttons
    CHECK(shows(u"Cuentas abiertas"_s));
    s.shot("19-spanish-tables");
    s.pos.logout();
    QTest::qWait(50);
    CHECK(s.c.pageId() == u"login"_s);
    CHECK(shows(u"Clock In"_s));                          // back to the store's
    vt::i18n::setLanguage(u"en"_s);
}

TEST_CASE("UI: Manager -> Network shows the standby, the screens and the printers", "[flow][ui][network]")
{
    Screen s;
    QVariant standby;
    s.pos.shared()->network = [&] {
        return QVariantMap{{u"role"_s, u"main"_s}, {u"machine"_s, u"office-pc"_s}, {u"standby"_s, standby},
                           {u"terminals"_s, QVariantList{QVariantMap{{u"name"_s, u"Bar"_s}, {u"address"_s, u"10.0.0.7"_s},
                                                                     {u"user"_s, u"Sam"_s}, {u"since"_s, qint64(1'700'000'000'000)}}}},
                           {u"printers"_s, QVariantList{QVariantMap{{u"name"_s, u"Kitchen"_s}, {u"type"_s, u"network"_s},
                                                                    {u"where"_s, u"10.0.0.50:9100"_s}, {u"status"_s, u"failed"_s},
                                                                    {u"error"_s, u"No answer"_s}, {u"at"_s, qint64(1'700'000'300'000)}}}}};
    };
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.c.jumpTo(u"manager"_s));
    QTest::qWait(30);
    s.c.activate(u"network"_s);                          // Manager -> Network…
    QTest::qWait(30);
    REQUIRE(s.c.pageId() == u"network"_s);
    auto standbyText = [&] {
        QQuickItem *t = Screen::findBy(s.window->contentItem(), "objectName", u"standbyText"_s);
        REQUIRE(t);
        return t->property("text").toString();
    };
    CHECK(standbyText().startsWith(u"No standby"_s));
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"Bar"_s));
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"Sam is logged in"_s));
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"Kitchen"_s));

    standby = QVariantMap{{u"address"_s, u"10.0.0.9"_s}, {u"since"_s, qint64(1'700'000'000'000)}, {u"standby"_s, true}};
    emit s.pos.shared()->networkChanged();
    QTest::qWait(30);
    CHECK(standbyText().startsWith(u"In sync: 10.0.0.9"_s));
    s.shot("18-network");

    // Not for staff without manager pages.
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    CHECK(s.pos.networkInfo().isEmpty());
}

TEST_CASE("UI: Manager -> Factory Reset asks for RESET before it does anything", "[flow][ui][reset]")
{
    Screen s;
    int asked = 0;
    s.pos.shared()->requestFactoryReset = [&] { ++asked; return true; };
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.c.jumpTo(u"manager"_s));
    QTest::qWait(30);
    s.c.activate(u"factory-reset"_s);                    // Manager -> Factory Reset…
    QTest::qWait(30);
    REQUIRE(s.c.pageId() == u"factory-reset"_s);
    QQuickItem *button = Screen::findBy(s.window->contentItem(), "text", u"Back Up and Reset Everything"_s);
    REQUIRE(button);
    CHECK_FALSE(button->isEnabled());
    s.tapItem(Screen::findBy(s.window->contentItem(), "placeholderText", u"RESET"_s));
    for (const char ch : {'R', 'E', 'S', 'E', 'T'})
        QTest::keyClick(s.window, ch);
    QTest::qWait(30);
    CHECK(button->isEnabled());
    s.shot("17-factory-reset");
    s.tapItem(button);
    CHECK(asked == 1);
}

TEST_CASE("UI: the expo screen - kitchen names, what's made, ready in blue", "[flow][ui][expo]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    const auto order = [&](const QString &table, std::initializer_list<const char *> items) {
        REQUIRE(s.pos.selectTable(table) == app::PosService::TableNeedsGuests);
        REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
        for (const char *i : items) {
            s.pos.addItem(QString::fromLatin1(i));
            for (const QVariant &g : s.pos.choosingInfo()[u"groups"_s].toList())   // the first of what's required
                if (!g.toMap()[u"done"_s].toBool())
                    s.pos.chooseOption(g.toMap()[u"id"_s].toString(), 0);
            s.pos.finishChoosing();
        }
        REQUIRE(s.pos.sendOrder());
        s.pos.releaseCheck();
    };
    order(u"T1"_s, {"bacon-burger", "draft-beer"});
    order(u"T2"_s, {"cobb", "kids-burger"});
    const QVariantMap t2 = s.pos.expoTickets().last().toMap();
    REQUIRE(s.pos.bumpTicket(t2[u"checkId"_s].toLongLong(), t2[u"sentAt"_s].toLongLong(), {}));
    const QVariantMap t1 = s.pos.expoTickets().first().toMap();
    REQUIRE(s.pos.bumpTicket(t1[u"checkId"_s].toLongLong(), t1[u"sentAt"_s].toLongLong(), u"kitchen"_s));
    REQUIRE(s.c.jumpTo(u"expo"_s));
    QTest::qWait(60);
    s.shot("18-expo");
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"READY"_s));
}

// --- touch targets ------------------------------------------------------------------------
// Every page on a 1280x800 screen: lists what a finger can touch that is
// smaller than 40 px (about 7 mm on a 10" screen; widgets aim for 46).

namespace {

QString describe(QQuickItem *item)
{
    for (QQuickItem *i = item; i; i = i->parentItem()) {
        for (const char *p : {"text", "placeholderText", "objectName"}) {
            const QString v = i->property(p).toString();
            if (!v.isEmpty())
                return QString::fromLatin1(item->metaObject()->className()) + u" \""_s + v.left(40) + u'"';
        }
    }
    return QString::fromLatin1(item->metaObject()->className());
}

bool touchable(QQuickItem *item)
{
    if (item->inherits("QQuickAbstractButton") || item->inherits("QQuickMouseArea") || item->inherits("QQuickComboBox")
        || item->inherits("QQuickTextField") || item->inherits("QQuickSpinBox") || item->inherits("QQuickScrollBar"))
        return true;
    for (QObject *child : item->children()) {
        if ((child->inherits("QQuickTapHandler") || child->inherits("QQuickDragHandler"))
            && child->property("enabled").toBool())   // a label's handler is off
            return true;
    }
    return false;
}

void smallTargets(QQuickItem *item, QQuickWindow *window, QStringList *out)
{
    if (!item->isVisible() || item->opacity() < 0.05)
        return;
    if (touchable(item) && item->isEnabled()) {
        // The item's box (a text field's boundingRect is just its text).
        const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        const QRectF shown = r.intersected(QRectF(QPointF(), window->size()));
        if (!shown.isEmpty() && (r.width() < 40 || r.height() < 40))
            out->append(u"%1 (%2x%3)"_s.arg(describe(item)).arg(qRound(r.width())).arg(qRound(r.height())));
    }
    for (QQuickItem *child : item->childItems())
        smallTargets(child, window, out);
}

} // namespace

TEST_CASE("Touch: every control on every page is big enough for a finger", "[ui][touch]")
{
    Screen s(false, 1280, 800);
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    QStringList report;
    QDir pages(QStringLiteral(VTM_SEED_DIR "/pages"));
    for (const QString &file : pages.entryList({u"*.json"_s})) {
        const QString id = file.chopped(5);
        if (id.endsWith(u"-phone"_s) || !s.c.jumpTo(id))
            continue;
        QTest::qWait(60);
        s.shot(qPrintable(u"touch-"_s + id));
        QStringList small;
        smallTargets(s.window->contentItem(), s.window, &small);
        small.removeDuplicates();
        for (const QString &x : small)
            report << id + u": "_s + x;
    }
    WARN(report.join(u'\n').toStdString());
    CHECK(report.isEmpty());
}

TEST_CASE("Touch: the phone pages on a phone", "[ui][touch]")
{
    Screen s(false, 412, 915, u"phone"_s);
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    QStringList report;
    QDir pages(QStringLiteral(VTM_SEED_DIR "/pages"));
    for (const QString &file : pages.entryList({u"*-phone.json"_s})) {
        const QString id = file.chopped(5).chopped(6);   // the base page; the phone variant shows
        if (!s.c.jumpTo(id))
            continue;
        QTest::qWait(60);
        s.shot(qPrintable(u"touch-phone-"_s + id));
        QStringList small;
        smallTargets(s.window->contentItem(), s.window, &small);
        small.removeDuplicates();
        for (const QString &x : small)
            report << id + u": "_s + x;
    }
    WARN(report.join(u'\n').toStdString());
    CHECK(report.isEmpty());
}

// --- the user manual's screenshots (hidden: run with "[.manual]" and VTM_SHOTS) ---------------

#include "app/pos_demo.hh"
#include "app/pos_json.hh"

TEST_CASE("Manual: a screenshot of every screen", "[.manual]")
{
    Screen s;
    // A lived-in store: two months of demo service, promotions and texting on.
    s.pos.shared()->settings.promotions =
        app::settingsFromJson(test::readSeed("pos/settings.json").object()).promotions;
    s.pos.shared()->settings.textWebhook = "https://example.invalid/sms";
    s.pos.shared()->sendText = [](const QString &, const QString &) {};
    // Without the screen watching: it would redraw after every one of thousands of changes.
    s.c.setPos(nullptr);
    REQUIRE(app::fillDemoData(s.pos, QDateTime::currentMSecsSinceEpoch()).startsWith(u"Added"_s));
    s.c.setPos(&s.pos);
    // Reports over a range read back what was saved (here: what the sink recorded).
    s.pos.shared()->history = [&s](std::int64_t from, std::int64_t to) {
        std::vector<core::Check> out;
        for (const auto &[id, c] : s.sink.checks)
            if (c.status == core::CheckStatus::Closed && c.closedAt >= from && c.closedAt < to)
                out.push_back(c);
        return out;
    };
    // A clean picture: no passing message on top.
    const auto snap = [&](const char *name) {
        if (QObject *toast = s.window->findChild<QObject *>(u"toast"_s))
            toast->setProperty("opacity", 0);
        QTest::qWait(450);                                      // it fades out
        s.shot(name);
    };
    const auto go = [&](const char *page, const char *name) {
        s.c.jumpTo(QString::fromLatin1(page));
        QTest::qWait(120);
        snap(name);
    };
    const auto chooseRequired = [&] {
        for (const QVariant &g : s.pos.choosingInfo()[u"groups"_s].toList())
            if (!g.toMap()[u"done"_s].toBool())
                s.pos.chooseOption(g.toMap()[u"id"_s].toString(), 0);
        s.pos.finishChoosing();
    };

    // Logging in, the floor.
    s.pos.logout();
    go("login", "m01-login");
    REQUIRE(s.pos.loginWithPin(u"1111"_s));                     // Sam, a server
    s.pos.clockIn();
    for (const char *table : {"T2", "T5"}) {                    // a couple of tables already going
        REQUIRE(s.pos.selectTable(QString::fromLatin1(table)) == app::PosService::TableNeedsGuests);
        s.pos.entryKey(u"4"_s);
        REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
        s.pos.addItem(u"cobb"_s);
        s.pos.addItem(u"classic-burger"_s);
        chooseRequired();                                       // its temperature and side
        s.pos.sendOrder();
        s.pos.releaseCheck();
    }
    go("tables", "m02-tables");
    REQUIRE(s.pos.selectTable(u"T3"_s) == app::PosService::TableNeedsGuests);
    go("guest-count", "m03-guest-count");
    s.pos.entryKey(u"3"_s);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    go("index-lunch", "m04-menu-index");
    s.pos.addItem(u"bacon-burger"_s);
    s.pos.chooseOption(u"temperature"_s, 1);                    // Medium Rare
    s.pos.chooseOption(u"side"_s, 0);                           // Fries
    s.pos.finishChoosing();
    go("items-burgers", "m05-items");
    s.c.activate(u"item-1"_s);                                  // Classic Burger -> its choices
    QTest::qWait(120);
    snap("m06-modifier-page");
    s.c.jumpTo(u"items-salads"_s);
    s.pos.addItem(u"house-salad"_s);
    s.c.jumpTo(u"modifiers"_s);
    QTest::qWait(120);
    snap("m07-choose");
    chooseRequired();
    s.pos.setSeat(2);
    s.pos.setCourse(2);
    s.pos.addItem(u"caesar"_s);
    s.pos.setCourse(1);
    go("items-drinks", "m08-order-seats-courses");
    go("note", "m09-note");
    go("check-options", "m10-check-options");
    go("transfer", "m11-transfer");
    go("move-table", "m12-move-table");
    go("merge", "m13-merge");
    s.pos.sendOrder();
    go("split", "m14-split");

    // A gift card with money on it (the demo ones are spent by now).
    s.pos.releaseCheck();
    REQUIRE(s.pos.sellGiftCard(u"6000 2026 1001"_s, 5000));
    REQUIRE(s.pos.tender(u"cash"_s, 5000));
    REQUIRE(s.pos.closeCheck());

    // Takeout, customers, gift cards.
    s.pos.releaseCheck();
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    go("customer", "m15-takeout-customer");
    s.pos.findCustomers(u"Dana"_s);
    const QVariantList found = s.pos.customerResults();
    if (!found.isEmpty())
        s.pos.selectCustomer(found.first().toMap()[u"id"_s].toString());
    s.pos.useCustomer();
    go("customers", "m16-customers");
    s.pos.lookupGiftCard(u"600020261001"_s);
    go("gift-card", "m17-gift-card");
    s.pos.addItem(u"cobb"_s);
    s.pos.addItem(u"soda"_s);
    chooseRequired();
    go("settle", "m18-settle");
    s.pos.releaseCheck();

    // Drinks for the bar.
    REQUIRE(s.pos.selectTable(u"Bar 1"_s) == app::PosService::TableNeedsGuests);
    s.pos.entryKey(u"2"_s);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    for (const char *drink : {"draft-beer", "house-wine", "soda"}) {
        s.pos.addItem(QString::fromLatin1(drink));
        chooseRequired();
    }
    s.pos.sendOrder();
    s.pos.releaseCheck();

    // The host stand and the kitchen.
    go("host", "m19-host-stand");
    go("kitchen", "m20-kitchen");
    s.tapKey(u"All Day"_s);
    QTest::qWait(60);
    snap("m21-kitchen-all-day");
    go("bar-display", "m22-bar-display");
    go("expo", "m23-expo");
    go("logout", "m24-logout");

    // Manager.
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    s.pos.clockIn();
    go("manager", "m30-manager");
    const auto admin = [&](const char *page, const char *name, bool pickFirst) {
        s.c.jumpTo(QString::fromLatin1(page));
        QTest::qWait(120);
        if (pickFirst) {
            // The first record; the form is zoomed for fingers (ZoneItem.formZoom).
            const double zoom = std::min(46.0 / (25 * std::min(1600.0 / 1920, 900.0 / 1080)), 1888.0 / 680);
            s.tapCanvas(16 + 60 * zoom, 112 + 30 * zoom);
            QTest::qWait(80);
        }
        snap(name);
    };
    admin("admin-menu", "m31-menu-items", true);
    admin("admin-modifier-groups", "m32-modifier-groups", true);
    admin("admin-employees", "m33-employees", true);
    admin("admin-store", "m34-store-settings", false);
    admin("admin-taxes", "m35-taxes", false);
    admin("admin-tenders", "m36-payment-types", true);
    admin("admin-printers", "m37-printers", true);
    admin("admin-terminals", "m38-terminals", false);
    admin("admin-meal-periods", "m39-meal-periods", true);
    admin("admin-inventory", "m40-inventory", true);
    admin("admin-promotions", "m41-promotions", true);
    go("admin-schedule", "m42-schedule");
    go("network", "m42b-network");
    go("sold-out", "m43-sold-out");
    go("drawer", "m44-drawer");
    go("end-of-day", "m45-end-of-day");
    go("reports", "m46-report-sales");
    s.tapKey(u"Items"_s);
    s.tapKey(u"Last Month"_s);                                  // both years have the whole month
    s.tapKey(u"vs Last Year"_s);
    for (int i = 0; i < 200 && s.pos.rangeReport()[u"loading"_s].toBool(); ++i)
        QTest::qWait(20);
    QTest::qWait(80);
    snap("m47-report-month-vs-last-year");
    s.tapKey(u"Day"_s);
    for (const char *r : {"Labor", "Kitchen", "Food Cost", "Tips", "Gift Cards"}) {
        s.tapKey(QString::fromLatin1(r));
        QTest::qWait(80);
        snap(QString(u"m48-report-%1"_s).arg(QString::fromLatin1(r).toLower().replace(u' ', u'-')).toLatin1().constData());
    }
    go("closed-checks", "m49-closed-checks");
    go("factory-reset", "m50-factory-reset");

    // Network, as it looks with a standby server and a few screens.
    s.pos.shared()->network = [] {
        const qint64 since = QDateTime::currentMSecsSinceEpoch() - 3 * 3600 * 1000;
        QVariantList terminals;
        for (const auto &[name, address, user] : {std::tuple{"Bar", "192.168.1.21", "Jo"}, {"Patio Tablet", "192.168.1.34", "Sam"},
                                                 {"Kitchen", "192.168.1.40", ""}})
            terminals << QVariantMap{{u"name"_s, QString::fromLatin1(name)}, {u"address"_s, QString::fromLatin1(address)},
                                     {u"user"_s, QString::fromLatin1(user)}, {u"since"_s, since}};
        return QVariantMap{{u"role"_s, u"main"_s}, {u"machine"_s, u"office-pc"_s}, {u"terminals"_s, terminals},
                           {u"standby"_s, QVariantMap{{u"address"_s, u"192.168.1.11"_s}, {u"since"_s, since}}},
                           {u"printers"_s, QVariantList{
                               QVariantMap{{u"name"_s, u"Kitchen"_s}, {u"where"_s, u"192.168.1.50:9100"_s}, {u"status"_s, u"ok"_s},
                                           {u"at"_s, since + 3 * 3600 * 1000 - 120000}},
                               QVariantMap{{u"name"_s, u"Receipt"_s}, {u"where"_s, u"192.168.1.51:9100"_s}, {u"status"_s, u"failed"_s},
                                           {u"error"_s, u"No answer (is it on?)"_s}, {u"at"_s, since + 3 * 3600 * 1000 - 60000}}}}};
    };
    emit s.pos.shared()->networkChanged();
    go("network", "m52-network-standby");
    s.pos.shared()->network = nullptr;

    // Spanish: Rosa's screens.
    vt::i18n::install();
    vt::ui::followLanguage(&s.engine, &s.c);
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"5555"_s));
    go("tables", "m53-spanish-tables");
    REQUIRE(s.pos.selectTable(u"T6"_s) == app::PosService::TableNeedsGuests);
    s.pos.entryKey(u"2"_s);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    s.pos.addItem(u"bacon-burger"_s);
    go("modifiers", "m54-spanish-choices");
    s.pos.cancelChoosing();
    s.pos.releaseCheck();
    s.pos.logout();
    vt::i18n::setLanguage(u"en"_s);
    s.engine.retranslate();
    REQUIRE(s.pos.loginWithPin(u"1234"_s));

    // The self-order kiosk, as a guest sees it.
    s.pos.logout();
    s.pos.enableSelfOrder();
    QTest::qWait(150);
    snap("m55-kiosk-welcome");
    REQUIRE(s.pos.kioskStart(false));
    REQUIRE(s.pos.kioskAdd(u"cheeseburger"_s));
    s.pos.finishChoosing();
    REQUIRE(s.pos.kioskAdd(u"classic-burger"_s));
    REQUIRE(s.pos.chooseOption(u"temperature"_s, 1));
    QTest::qWait(150);
    snap("m56-kiosk-choices");
    REQUIRE(s.pos.chooseOption(u"side"_s, 2));
    REQUIRE(s.pos.finishChoosing());
    REQUIRE(s.pos.kioskRemove(s.pos.lines().value(0).toMap()[u"id"_s].toLongLong()));   // the first, unfinished
    REQUIRE(s.pos.kioskAdd(u"lemonade"_s));
    REQUIRE(s.pos.chooseOption(u"drink-size"_s, 1));
    REQUIRE(s.pos.finishChoosing());
    QTest::qWait(150);
    snap("m57-kiosk-order");
    QQuickItem *review = Screen::findBy(s.window->contentItem(), "objectName", u"kioskReview"_s);
    REQUIRE(review);
    s.tapItem(review);
    QTest::qWait(120);
    for (const char ch : {'M', 'a', 'r', 'i', 'a'})
        QTest::keyClick(s.window, ch);
    snap("m58-kiosk-name");
    REQUIRE(s.pos.kioskFinish({{u"name"_s, u"Maria"_s}}));
    QTest::qWait(150);
    snap("m59-kiosk-number");
    REQUIRE(s.pos.leaveSelfOrder(u"1234"_s));

    // Editing pages.
    s.c.jumpTo(u"tables"_s);
    REQUIRE(s.c.requestEditMode());
    QTest::qWait(150);
    snap("m51-edit-mode");
}

TEST_CASE("Manual: the phone screens", "[.manual]")
{
    Screen s(false, 540, 1080, u"phone"_s);
    QTest::qWait(300);
    const auto snap = [&](const char *page, const char *name) {
        s.c.jumpTo(QString::fromLatin1(page));
        QTest::qWait(200);
        if (QObject *toast = s.window->findChild<QObject *>(u"toast"_s))
            toast->setProperty("opacity", 0);
        QTest::qWait(450);                                      // it fades out
        s.shot(name);
    };
    s.pos.logout();
    snap("login", "m60-phone-login");
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    for (const char *table : {"T2", "T5"}) {
        REQUIRE(s.pos.selectTable(QString::fromLatin1(table)) == app::PosService::TableNeedsGuests);
        s.pos.entryKey(u"2"_s);
        REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
        s.pos.addItem(u"cobb"_s);
        s.pos.sendOrder();
        s.pos.releaseCheck();
    }
    snap("tables", "m61-phone-tables");
    REQUIRE(s.pos.selectTable(u"T7"_s) == app::PosService::TableNeedsGuests);
    s.pos.entryKey(u"2"_s);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    s.pos.addItem(u"cobb"_s);
    s.pos.addItem(u"caesar"_s);
    snap("index-lunch", "m62-phone-order");
    snap("settle", "m63-phone-settle");
}

TEST_CASE("Memory: a screen through a long service stays flat", "[.memory]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    s.pos.entryKey(u"10000"_s);
    REQUIRE(s.pos.openDrawerSession());
    REQUIRE(s.c.jumpTo(u"tables"_s));
    const auto rss = [] {
        QFile f(u"/proc/self/status"_s);
        f.open(QIODevice::ReadOnly);
        for (const QByteArray &line : f.readAll().split('\n'))
            if (line.startsWith("VmRSS:"))
                return line.mid(6).trimmed().split(' ').first().toLong();
        return 0L;
    };
    const auto serve = [&](int n) {
        for (int i = 0; i < n; ++i) {
            REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
            s.pos.addItem(u"cobb"_s);
            s.pos.addItem(u"water"_s);
            s.pos.sendOrder();
            s.pos.tender(u"cash"_s);
            REQUIRE(s.pos.closeCheck());
            if (i % 10 == 0)
                QTest::qWait(1);   // let the screen redraw, as it would
        }
    };
    serve(100);
    QTest::qWait(200);
    const long after100 = rss();
    serve(300);
    QTest::qWait(200);
    const long after400 = rss();
    WARN("RSS after 100 checks " << after100 << " kB, after 400 " << after400 << " kB, per check "
         << (after400 - after100) / 300.0 << " kB");
}

TEST_CASE("UI: a server's void waits for a manager's PIN on the same screen", "[flow][ui][approval]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.selectTable(u"T4"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    s.pos.addItem(u"cobb"_s);
    REQUIRE(s.pos.sendOrder());
    REQUIRE(s.c.jumpTo(u"items-salads"_s));
    QTest::qWait(50);
    s.c.activate(u"flow-void"_s);                                // Void, as Sam
    QTest::qWait(80);
    CHECK(s.pos.approvalInfo()[u"needed"_s].toBool());
    s.shot("19-approval");
    std::function<QQuickItem *(QQuickItem *, const QString &)> named = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
        for (QQuickItem *i : root->childItems()) {
            if (i->isVisible() && i->objectName() == name)
                return i;
            if (QQuickItem *hit = named(i, name))
                return hit;
        }
        return nullptr;
    };
    for (const char *k : {"1", "2", "3", "4", "OK"})
        s.tapItem(named(s.window->contentItem(), u"approvalKey-"_s + QString::fromLatin1(k)));
    QTest::qWait(50);
    CHECK_FALSE(s.pos.approvalInfo()[u"needed"_s].toBool());
    CHECK(s.pos.lines().first().toMap()[u"voided"_s].toBool());
}

TEST_CASE("UI: idle screens log out; messages show where they're meant to", "[flow][ui][messages]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.c.jumpTo(u"tables"_s));
    // Another screen (the kitchen) sends to the floor; this is a floor screen.
    app::PosService kitchen(s.pos.shared(), u"Kitchen"_s);
    REQUIRE(kitchen.sendMessage(u"floor"_s, u"86 salmon - out until tomorrow"_s));
    QTest::qWait(80);
    QQuickItem *ok = s.window->findChild<QQuickItem *>(u"messageOk"_s);
    REQUIRE(ok);
    CHECK(ok->isVisible());
    s.shot("20-message");
    s.tapItem(ok);
    CHECK_FALSE(ok->isVisible());
    // A message for the kitchen screens doesn't show on the floor.
    REQUIRE(kitchen.sendMessage(u"kitchen"_s, u"Order up"_s));
    QTest::qWait(50);
    CHECK_FALSE(ok->isVisible());

    // Idle: logged out by itself (a short time for the test).
    s.c.setIdleTimeoutForTesting(150);
    QTest::qWait(400);
    CHECK_FALSE(s.pos.loggedIn());
    CHECK(s.c.pageId() == u"login"_s);
}

TEST_CASE("Kiosk on a 1080x1920 portrait screen (Chipsee KIOSK-CM4-215)", "[flow][ui][kiosk][portrait]")
{
    // A 21.5" floor-standing kiosk: tall, guests reaching up to it.
    Screen s(false, 1080, 1920);
    s.pos.enableSelfOrder();
    QTest::qWait(100);
    auto find = [&](const QString &name) {
        QQuickItem *kiosk = Screen::findBy(s.window->contentItem(), "objectName", u"selfOrder"_s);
        REQUIRE(kiosk);
        // The visible one (landscape and portrait share some names).
        std::function<QQuickItem *(QQuickItem *)> look = [&](QQuickItem *item) -> QQuickItem * {
            if (!item->isVisible())
                return nullptr;
            if (item->objectName() == name)
                return item;
            for (QQuickItem *c : item->childItems())
                if (QQuickItem *f = look(c))
                    return f;
            return nullptr;
        };
        return look(kiosk);
    };
    // In reach: what a guest touches sits in the lower two-thirds.
    const auto lowerTwoThirds = [&](QQuickItem *item) {
        REQUIRE(item);
        return item->mapToScene(QPointF(0, 0)).y() >= 1920.0 / 3;
    };
    s.shot("p1-attract");
    s.tapItem(find(u"kioskAttract"_s));
    QTest::qWait(60);
    CHECK(lowerTwoThirds(find(u"kioskForHere"_s)));
    s.shot("p2-where");
    s.tapItem(find(u"kioskForHere"_s));
    REQUIRE(s.pos.selfOrderInfo()[u"ordering"_s].toBool());
    QTest::qWait(60);
    for (const char *name : {"kioskReview", "kioskOrderBar", "kioskStartOver", "kioskEasyReach"})
        CHECK(lowerTwoThirds(find(QString::fromLatin1(name))));

    // A burger: its choices come up from the bottom.
    REQUIRE(s.pos.kioskAdd(u"classic-burger"_s));
    REQUIRE(s.pos.chooseOption(u"temperature"_s, 1));
    QTest::qWait(100);
    CHECK(lowerTwoThirds(find(u"kioskChoicesDone"_s)));
    s.shot("p3-choices");
    REQUIRE(s.pos.chooseOption(u"side"_s, 2));
    s.tapItem(find(u"kioskChoicesDone"_s));
    REQUIRE(s.pos.kioskAdd(u"lemonade"_s));
    REQUIRE(s.pos.chooseOption(u"drink-size"_s, 1));
    REQUIRE(s.pos.finishChoosing());
    QTest::qWait(100);
    s.shot("p4-menu");
    CHECK(find(u"kioskOrderBar"_s)->property("text").toString().startsWith(u"2 items"_s));

    // Easy Reach: everything in the lower part of the screen.
    s.tapItem(find(u"kioskEasyReach"_s));
    QTest::qWait(400);
    QQuickItem *items = find(u"kioskItems"_s);
    REQUIRE(items);
    CHECK(items->mapToScene(QPointF(0, 0)).y() >= 1920 * 0.4 - 1);
    s.shot("p5-easy-reach");
    s.tapItem(find(u"kioskEasyReach"_s));
    QTest::qWait(400);
    CHECK(find(u"kioskItems"_s)->mapToScene(QPointF(0, 0)).y() < 1920 * 0.2);   // full screen again

    // The order sheet, then a name and the number.
    s.tapItem(find(u"kioskOrderBar"_s));
    QTest::qWait(100);
    s.shot("p6-order-sheet");
    s.tapItem(Screen::findBy(s.window->contentItem(), "text", u"Keep Ordering"_s));
    s.tapItem(find(u"kioskReview"_s));
    QTest::qWait(100);
    CHECK(lowerTwoThirds(find(u"kioskName"_s)));
    for (const char ch : {'A', 'n', 'a'})
        QTest::keyClick(s.window, ch);
    s.shot("p7-name");
    s.tapItem(find(u"kioskPlace"_s));
    QTest::qWait(100);
    REQUIRE(find(u"kioskNumber"_s));
    s.shot("p8-number");
}
