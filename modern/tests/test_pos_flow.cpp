#include <catch2/catch_test_macros.hpp>

#include "layoutcontroller.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QDate>
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
    s.c.activate(u"item-1"_s);                       // Classic Burger
    CHECK(s.c.pageId() == u"mod-temperature"_s);
    s.c.activate(u"opt-2"_s);                        // Medium Rare
    CHECK(s.c.pageId() == u"mod-side"_s);
    s.c.activate(u"opt-3"_s);                        // Onion Rings (+1.00)
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

    explicit Screen(bool touchKeyboard = false)
    {
        engine.setInitialProperties({{u"controller"_s, QVariant::fromValue(&c)},
                                     {u"width"_s, 1600}, {u"height"_s, 900}, {u"touchKeyboard"_s, touchKeyboard}});
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
    s.pos.addItem(u"medium-rare"_s);
    s.pos.addItem(u"onion-rings"_s);
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
    order(u"T4"_s, {"bacon-burger", "medium-rare", "fries", "cheeseburger"});
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
    order(u"T1"_s, {"bacon-burger", "medium-rare", "fries", "draft-beer"});
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
