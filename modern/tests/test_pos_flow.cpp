#include <catch2/catch_test_macros.hpp>

#include "layoutcontroller.hh"
#include "print/raster.hh"
#include "editorcontroller.hh"
#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QRegularExpression>
#include <QPainter>
#include <QTemporaryDir>
#include "app/i18n.hh"
#include "language.hh"
#include "fake_stripe.hh"
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

    // The on-screen keyboard (it pops up over the screen).
    QQuickItem *keyboard()
    {
        QObject *popup = window->findChild<QObject *>(u"touchKeys"_s);
        return popup ? qvariant_cast<QQuickItem *>(popup->property("contentItem")) : nullptr;
    }
    bool keyboardShown()
    {
        QObject *popup = window->findChild<QObject *>(u"touchKeys"_s);
        return popup && popup->property("visible").toBool();
    }

    // Tap on-screen keyboard keys (their labels), one per character.
    void typeOnScreen(const QString &labels)
    {
        for (QChar ch : labels)
            tapItem(findBy(keyboard(), "text", QString(ch)));
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
    s.tapCanvas(1468 + 218, 16 + 86 + 38);       // Credit Card, the second payment type (exact balance)
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

    QQuickItem *keyboard = s.keyboard();
    REQUIRE(keyboard);
    CHECK_FALSE(s.keyboardShown());
    s.tapItem(Screen::findBy(s.window->contentItem(), "placeholderText", u"Phone or name…"_s));
    CHECK(s.keyboardShown());                           // a text field is being typed in
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
    REQUIRE(s.keyboardShown());
    CHECK(Screen::findBy(keyboard, "text", u"Done"_s));   // number fields get the number pad
    s.typeOnScreen(u"60012"_s);
    s.tapItem(Screen::findBy(keyboard, "text", u"Done"_s));   // Enter looks the card up
    CHECK(s.pos.giftCardInfo()[u"number"_s] == u"60012"_s);
    CHECK_FALSE(s.keyboardShown());
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

TEST_CASE("UI: the kiosk in the store's own look, asking only what the store wants", "[flow][ui][kiosk][kiosklook]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    QVariantMap store = s.pos.adminRecords(u"store"_s).first().toMap();
    store[u"kioskBackground"_s] = u"blue"_s;
    CHECK_FALSE(s.pos.adminSave(u"store"_s, 0, store));       // colors as #rrggbb
    store[u"kioskBackground"_s] = u"#f4efe6"_s;
    store[u"kioskCard"_s] = u"#ffffff"_s;
    store[u"kioskText"_s] = u"#2b2118"_s;
    store[u"kioskGo"_s] = u"#c0392b"_s;
    store[u"kioskWelcome"_s] = u"Tap to start your order"_s;
    store[u"kioskSizePercent"_s] = 120;
    store[u"kioskAskWhere"_s] = false;
    store[u"kioskAskName"_s] = false;
    store[u"kioskEasyReach"_s] = false;
    REQUIRE(s.pos.adminSave(u"store"_s, 0, store));
    CHECK(app::settingsFromJson(app::toJson(s.pos.shared()->settings)).kioskLook == s.pos.shared()->settings.kioskLook);
    s.pos.logout();

    s.pos.enableSelfOrder();
    QTest::qWait(80);
    auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    QQuickItem *kiosk = find(u"selfOrder"_s);
    REQUIRE(kiosk);
    CHECK(kiosk->property("color").value<QColor>() == QColor(u"#f4efe6"_s));
    CHECK(Screen::findBy(kiosk, "text", u"Tap to start your order"_s));
    s.shot("34-kiosk-look");

    s.tapItem(find(u"kioskAttract"_s));                   // no For Here / To Go: the menu
    QTest::qWait(60);
    REQUIRE(s.pos.selfOrderInfo()[u"ordering"_s].toBool());
    CHECK_FALSE(find(u"kioskEasyReach"_s));
    s.tapItem(Screen::findBy(kiosk, "text", u"Salads"_s));
    QTest::qWait(60);
    s.tapItem(Screen::findBy(kiosk, "text", u"Caesar"_s));  // asks for an optional protein
    QTest::qWait(60);
    s.tapItem(find(u"kioskChoicesDone"_s));
    QTest::qWait(60);
    REQUIRE(s.pos.lines().size() == 1);
    s.shot("35-kiosk-look-menu");
    s.tapItem(find(u"kioskReview"_s));
    QTest::qWait(60);
    CHECK_FALSE(find(u"kioskName"_s));                    // no name asked
    s.tapItem(find(u"kioskPlace"_s));
    QTest::qWait(60);
    QQuickItem *number = find(u"kioskNumber"_s);
    REQUIRE(number);
    CHECK(number->isVisible());
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

TEST_CASE("UI: switch user keeps each person's check for when they're back", "[flow][ui][switch]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    s.pos.addItem(u"water"_s);
    // Mid-order: Check… -> Switch User.
    REQUIRE(s.c.jumpTo(u"check-options"_s));
    REQUIRE(s.pos.lines().size() == 1);
    s.shot("52-switch-user");
    s.c.activate(u"switch"_s);
    QTest::qWait(60);
    CHECK_FALSE(s.pos.loggedIn());
    CHECK(s.c.pageId() == u"login"_s);

    // Someone else in and out: no check of theirs.
    REQUIRE(s.pos.loginWithPin(u"2222"_s));
    QTest::qWait(30);
    CHECK_FALSE(s.pos.hasCheck());
    s.pos.logout();

    // The first one back: their takeout, on its order screen.
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    QTest::qWait(60);
    REQUIRE(s.pos.hasCheck());
    CHECK(s.pos.lines().size() == 1);
    CHECK(s.c.pageId() != u"tables"_s);
    // Once only: logging out from the floor without a check forgets it.
    s.pos.releaseCheck();
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    CHECK_FALSE(s.pos.hasCheck());
}

TEST_CASE("UI: each person's text size, left hand and start screen; a start screen per job", "[flow][ui][prefs]")
{
    Screen s;
    const auto rectOf = [&](const QString &id) {
        ZoneModel *m = s.c.zones();
        for (int r = 0; r < m->rowCount(); ++r)
            if (const QModelIndex i = m->index(r); m->data(i, ZoneModel::ZoneIdRole).toString() == id)
                return QRect(m->data(i, ZoneModel::ZoneXRole).toInt(), m->data(i, ZoneModel::ZoneYRole).toInt(),
                             m->data(i, ZoneModel::ZoneWRole).toInt(), m->data(i, ZoneModel::ZoneHRole).toInt());
        return QRect();
    };
    const auto save = [&](const QString &panel, const QString &name, const QVariantMap &changes) {
        const QVariantList records = s.pos.adminRecords(panel);
        for (int i = 0; i < records.size(); ++i) {
            QVariantMap r = records[i].toMap();
            if (!name.isEmpty() && r[u"name"_s] != name)
                continue;
            r.insert(changes);
            return s.pos.adminSave(panel, i, r);
        }
        return false;
    };
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    // Servers start on the whole menu (Store settings).
    REQUIRE(save(u"store"_s, {}, {{u"startPage.server"_s, u"menu-all"_s}}));
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1111"_s));   // Sam, a server
    QTest::qWait(30);
    CHECK(s.c.pageId() == u"menu-all"_s);
    CHECK_FALSE(s.pos.userPrefs().value(u"leftHanded"_s).toBool());
    const QRect normal = rectOf(u"order-list"_s);
    CHECK(normal.x() < 960);
    s.pos.logout();

    // Sam's own: bigger text, left-handed, starting on Lunch.
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(save(u"employees"_s, u"Sam"_s, {{u"textSize"_s, u"130"_s}, {u"leftHanded"_s, true},
                                           {u"startPage"_s, u"index-lunch"_s}}));
    CHECK(s.c.pageId() != u"index-lunch"_s);   // the manager's own screen didn't change
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    QTest::qWait(60);
    CHECK(s.c.pageId() == u"index-lunch"_s);
    const QRect mirrored = rectOf(u"order-list"_s);
    CHECK(mirrored.x() == 1920 - normal.x() - normal.width());   // the check on the right
    CHECK(rectOf(u"tab-breakfast"_s).x() < rectOf(u"tab-lunch"_s).x());   // rows keep their order
    CHECK(rectOf(u"tab-breakfast"_s).x() == normal.x());
    CHECK(mirrored.width() == normal.width());
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    s.pos.addItem(u"water"_s);
    QTest::qWait(60);
    s.shot("53-left-handed");
    REQUIRE(s.c.jumpTo(u"settle"_s));
    QTest::qWait(40);
    s.shot("54-left-handed-pay");
    CHECK(s.pos.userPrefs().value(u"textSize"_s).toInt() == 130);
    // The floor plan isn't mirrored.
    s.pos.releaseCheck();
    REQUIRE(s.c.jumpTo(u"tables"_s));
    CHECK(rectOf(u"quick"_s).x() > 960);
    s.pos.logout();
    CHECK(s.pos.userPrefs().isEmpty());
}

TEST_CASE("UI: after Send a server can't void or change items without a manager's PIN", "[flow][ui][approval]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));                       // Sam, a server
    REQUIRE(s.pos.selectTable(u"T2"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    s.pos.addItem(u"water"_s);
    s.pos.addItem(u"cobb"_s);
    s.pos.finishChoosing();
    REQUIRE(s.pos.sendOrder());
    QTest::qWait(60);
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    const qint64 cobb = s.pos.lines().value(1).toMap()[u"id"_s].toLongLong();
    s.pos.selectLine(cobb);
    QTest::qWait(40);

    // Sent: no − / + and no choices to change; more is a new line (Again).
    CHECK_FALSE(find(u"lineMore"_s));
    CHECK_FALSE(find(u"lineLess"_s));
    CHECK_FALSE(s.pos.setLineQuantity(cobb, 2));
    CHECK_FALSE(s.pos.lines().value(1).toMap()[u"choices"_s].toBool());

    // Void asks for a manager: a server's own PIN doesn't do it.
    s.c.activate(u"flow-void"_s);
    QTest::qWait(80);
    REQUIRE(find(u"approvalKey-OK"_s));
    s.shot("64-manager-approval");
    const auto type = [&](const QString &pin) {
        for (const QChar ch : pin) {
            QQuickItem *k = find(u"approvalKey-"_s + ch);
            s.tapItem(k);
            QTest::qWait(30);
        }
        s.tapItem(find(u"approvalKey-OK"_s));
        QTest::qWait(60);
    };
    QTest::mouseClick(s.window, Qt::LeftButton, {}, QPoint(1450, 240));   // Mushroom Swiss, behind the dimmed pad
    QTest::qWait(60);
    type(u"1111"_s);
    CHECK_FALSE(s.pos.lines().value(1).toMap()[u"voided"_s].toBool());
    CHECK(s.pos.lines().size() == 2);                          // the pad's keys don't reach the menu behind it
    REQUIRE(find(u"approvalKey-OK"_s));                         // still waiting
    type(u"1234"_s);                                            // Morgan, a manager
    CHECK(s.pos.lines().value(1).toMap()[u"voided"_s].toBool());
    CHECK_FALSE(find(u"approvalKey-OK"_s));
    bool noted = false;
    for (const QVariant &e : s.pos.checkHistory())
        noted = noted || e.toMap().value(u"what"_s).toString().contains(u"approved by Morgan"_s);
    CHECK(noted);                                               // who approved it is on the check's history
}

TEST_CASE("UI: discounts are a manager's: $ Off and % Off of the amount typed", "[flow][ui][approval][discounts]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));                       // Sam, a server
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    s.pos.addItem(u"cobb"_s);                                   // $12.50
    s.pos.finishChoosing();
    REQUIRE(s.c.jumpTo(u"settle"_s));
    QTest::qWait(60);
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    const auto type = [&](const QString &pin) {
        for (const QChar ch : pin) {
            s.tapItem(find(u"approvalKey-"_s + ch));
            QTest::qWait(30);
        }
        s.tapItem(find(u"approvalKey-OK"_s));
        QTest::qWait(60);
    };
    const auto discounts = [&] { return s.pos.totals()[u"discounts"_s].toString(); };

    // A comp from the server: a manager's PIN first.
    s.c.activate(u"tender-comp"_s);
    QTest::qWait(60);
    REQUIRE(find(u"approvalKey-OK"_s));
    s.tapItem(find(u"approvalKey-Cancel"_s));
    QTest::qWait(40);
    CHECK(s.pos.payments().isEmpty());

    // $5.00 off, typed: approved by Morgan.
    s.pos.entryKey(u"500"_s);
    s.c.activate(u"off-amount"_s);
    QTest::qWait(60);
    REQUIRE(find(u"approvalKey-OK"_s));
    type(u"1234"_s);
    CHECK(discounts() == u"-$5.00"_s);
    CHECK(s.pos.payments().value(0).toMap()[u"name"_s].toString() == u"$5.00 off"_s);
    s.shot("65-custom-discount");

    // A manager's own % Off: no PIN asked. 20% of what's left of the cobb.
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.pos.openCheck(s.pos.openChecks().value(0).toMap()[u"id"_s].toLongLong()));
    REQUIRE(s.c.jumpTo(u"settle"_s));
    s.pos.entryKey(u"20"_s);
    s.c.activate(u"off-percent"_s);
    QTest::qWait(60);
    CHECK_FALSE(find(u"approvalKey-OK"_s));
    CHECK(discounts() == u"-$7.50"_s);                          // 5.00 + 20% of 12.50
    CHECK_FALSE(s.pos.customDiscount(true));                    // nothing typed
    s.pos.entryKey(u"150"_s);
    CHECK_FALSE(s.pos.customDiscount(true));                    // over 100%
}

TEST_CASE("UI: a manager fixes time punches, with a reason that goes on the Labor report", "[flow][ui][punches]")
{
    Screen s;
    const auto stamp = [](std::int64_t ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString(u"yyyy-MM-dd HH:mm"_s); };
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    const std::int64_t hour = 3'600'000;
    // Sam forgot to clock out: still on the clock since 5 hours ago.
    s.pos.shared()->punches.push_back({901, "sam", now - 5 * hour, 0, {}, "server", vt::Money::fromCents(1200)});
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    const auto records = [&] { return s.pos.adminRecords(u"punches"_s); };
    const auto indexOf = [&](qint64 id) {
        const QVariantList r = records();
        for (int i = 0; i < r.size(); ++i)
            if (r[i].toMap()[u"id"_s].toLongLong() == id)
                return i;
        return -1;
    };
    int i = indexOf(901);
    REQUIRE(i >= 0);
    QVariantMap sam = records()[i].toMap();
    CHECK(sam[u"clockOut"_s].toString().isEmpty());
    CHECK(sam[u"_detail"_s].toString().contains(u"still clocked in"_s));

    // Clocked out 1 hour ago, with a 15-minute break: a reason first.
    sam[u"clockOut"_s] = stamp(now - hour);
    sam[u"breaks"_s] = QDateTime::fromMSecsSinceEpoch(now - 3 * hour).toString(u"HH:mm"_s) + u"-"_s
                       + QDateTime::fromMSecsSinceEpoch(now - 3 * hour + 15 * 60'000).toString(u"HH:mm"_s);
    CHECK_FALSE(s.pos.adminSave(u"punches"_s, i, sam));
    sam[u"reason"_s] = u"forgot to clock out"_s;
    REQUIRE(s.pos.adminSave(u"punches"_s, i, sam));
    const core::TimePunch *fixed = nullptr;
    for (const core::TimePunch &p : s.pos.shared()->punches)
        if (p.id == 901)
            fixed = &p;
    REQUIRE(fixed);
    CHECK_FALSE(fixed->open());
    CHECK(fixed->breaks.size() == 1);
    REQUIRE(s.pos.shared()->settings.punchChanges.size() == 1);
    CHECK(s.pos.shared()->settings.punchChanges[0].why == "forgot to clock out");
    CHECK(s.pos.shared()->settings.punchChanges[0].by == "Morgan (Manager)");

    // A missed punch for Casey yesterday; one overlapping it is refused.
    QVariantMap casey = s.pos.adminNewRecord(u"punches"_s);
    casey[u"employeeId"_s] = u"casey"_s;
    casey[u"clockIn"_s] = stamp(now - 30 * hour);
    casey[u"clockOut"_s] = stamp(now - 24 * hour);
    casey[u"reason"_s] = u"tablet was down"_s;
    REQUIRE(s.pos.adminSave(u"punches"_s, -1, casey));
    casey[u"clockIn"_s] = stamp(now - 26 * hour);
    casey[u"clockOut"_s] = stamp(now - 25 * hour);
    CHECK_FALSE(s.pos.adminSave(u"punches"_s, -1, casey));
    casey[u"clockIn"_s] = stamp(now + hour);                     // the future
    CHECK_FALSE(s.pos.adminSave(u"punches"_s, -1, casey));

    // Removing needs a reason too (the Remove button just says how).
    i = indexOf(901);
    CHECK_FALSE(s.pos.adminDelete(u"punches"_s, i));

    // The Labor report marks it and lists the change and why.
    const core::Report labor = s.pos.buildReport(u"labor"_s);
    bool marked = false, listed = false;
    for (const core::ReportRow &row : labor.rows) {
        marked = marked || (!row.cells.empty() && row.cells[0] == "Sam *");
        listed = listed || (row.cells.size() == 5 && row.cells[4] == "forgot to clock out");
    }
    CHECK(marked);
    CHECK(listed);

    // On screen: Schedule -> Time Punches.
    REQUIRE(s.c.jumpTo(u"admin-schedule"_s));
    s.c.activate(u"punches"_s);
    QTest::qWait(80);
    REQUIRE(s.c.pageId() == u"admin-punches"_s);
    s.shot("67-time-punches");

    QVariantMap gone = records()[indexOf(901)].toMap();
    gone[u"remove"_s] = true;
    gone[u"reason"_s] = u"duplicate"_s;
    REQUIRE(s.pos.adminSave(u"punches"_s, indexOf(901), gone));
    CHECK(indexOf(901) < 0);
    CHECK(s.pos.shared()->settings.punchChanges.size() == 3);
}

TEST_CASE("Layout files carry their pictures and fonts to another store", "[flow][ui][bundle]")
{
    QTemporaryDir dir;
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    QFile font(u"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf"_s);
    const bool haveFont = font.open(QIODevice::ReadOnly);
    const QByteArray fontBytes = haveFont ? font.readAll() : QByteArray();
    const QUrl withPicture = QUrl::fromLocalFile(dir.filePath(u"with-picture.vtlayout.json"_s));
    const QUrl exported = QUrl::fromLocalFile(dir.filePath(u"bundle.vtlayout.json"_s));
    {
        // Store A: a picture on the login page, and a font.
        Screen a;
        REQUIRE(a.pos.loginWithPin(u"1234"_s));
        REQUIRE(a.pos.addStoreImage(u"menu-board.png"_s, QString::fromLatin1(png.toBase64())));
        if (haveFont)
            REQUIRE(a.pos.addStoreImage(u"House Font.ttf"_s, QString::fromLatin1(fontBytes.toBase64())));
        QJsonObject layout = a.c.layout().toJson();
        QJsonArray pages = layout.value(u"pages"_s).toArray();
        for (int i = 0; i < pages.size(); ++i) {
            QJsonObject page = pages[i].toObject();
            if (page.value(u"role"_s).toString() != u"login")
                continue;
            QJsonArray zones = page.value(u"zones"_s).toArray();
            for (int z = 0; z < zones.size(); ++z)
                if (zones[z].toObject().value(u"id"_s).toString() == u"logo") {
                    QJsonObject zone = zones[z].toObject();
                    zone.insert(u"imagePath"_s, u"store:menu-board.png"_s);
                    zones[z] = zone;
                }
            page.insert(u"zones"_s, zones);
            pages[i] = page;
        }
        layout.insert(u"pages"_s, pages);
        QFile f(withPicture.toLocalFile());
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(layout).toJson());
        f.close();
        a.c.enterEditMode();
        REQUIRE(a.c.editor()->importLayout(withPicture));
        REQUIRE(a.c.editor()->exportLayout(exported));
        QFile out(exported.toLocalFile());
        REQUIRE(out.open(QIODevice::ReadOnly));
        const QJsonObject images = QJsonDocument::fromJson(out.readAll()).object().value(u"images"_s).toObject();
        CHECK(images.contains(u"menu-board.png"_s));
        CHECK(images.size() == (haveFont ? 2 : 1));              // only what's used, and the fonts
        a.c.leaveEditMode(false);
    }
    // Store B: importing brings them; the saved pages don't carry the pictures.
    Screen b;
    REQUIRE(b.pos.loginWithPin(u"1234"_s));
    const auto names = [&] {
        QStringList out;
        for (const QVariant &v : b.pos.storeImages())
            out << v.toMap()[u"name"_s].toString();
        return out;
    };
    CHECK_FALSE(names().contains(u"menu-board.png"_s));
    b.c.enterEditMode();
    REQUIRE(b.c.editor()->importLayout(exported));
    CHECK(names().contains(u"menu-board.png"_s));
    if (haveFont)
        CHECK(names().join(u","_s).contains(u".ttf"_s, Qt::CaseInsensitive));
    CHECK_FALSE(b.c.editor()->editor().layout().toJson().contains(u"images"_s));
}

TEST_CASE("UI: a manager arranges the self-filling menu by touch", "[flow][ui][arrange]")
{
    Screen s;
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    const auto tap = [&](const QString &name) {
        QQuickItem *it = find(name);
        REQUIRE(it);
        s.tapItem(it);
        QTest::qWait(60);
    };
    const auto indexOf = [&](const QString &id) {
        const QVariantList items = s.pos.menuItems();
        for (int i = 0; i < items.size(); ++i)
            if (items[i].toMap()[u"id"_s] == id)
                return i;
        return -1;
    };
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    REQUIRE(s.c.jumpTo(u"menu-all"_s));
    QTest::qWait(60);
    CHECK_FALSE(find(u"menuArrange"_s));                        // servers don't
    s.pos.releaseCheck();
    s.pos.logout();

    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    REQUIRE(s.c.jumpTo(u"menu-all"_s));
    QTest::qWait(60);
    REQUIRE(indexOf(u"classic-burger"_s) < indexOf(u"cheeseburger"_s));
    tap(u"menuArrange"_s);
    tap(u"menuItem-cheeseburger"_s);
    CHECK(s.pos.lines().isEmpty());                            // picked, not ordered
    tap(u"arrangeEarlier"_s);
    CHECK(indexOf(u"cheeseburger"_s) < indexOf(u"classic-burger"_s));
    tap(u"swatch-1"_s);                                        // green; still arranging after the change
    CHECK(s.pos.menuItems()[indexOf(u"cheeseburger"_s)].toMap()[u"buttonColor"_s] == u"#1f8a4c"_s);
    REQUIRE(find(u"arrangeBar"_s));
    s.shot("78-arrange-menu");
    tap(u"arrangeDone"_s);
    CHECK_FALSE(find(u"arrangeBar"_s));
    tap(u"menuItem-cheeseburger"_s);                           // orders again
    CHECK(s.pos.lines().size() == 1);
}

TEST_CASE("UI: a terminal's own look", "[flow][ui][terminallook]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.c.jumpTo(u"tables"_s));
    const auto fillOf = [&](const QString &id) {
        ZoneModel *m = s.c.zones();
        for (int r = 0; r < m->rowCount(); ++r)
            if (m->data(m->index(r), ZoneModel::ZoneIdRole).toString() == id)
                return m->data(m->index(r), ZoneModel::StyleNormalRole).toMap().value(u"fill"_s).toString();
        return QString();
    };
    const QString before = fillOf(u"checks"_s);
    const QVariantList looks = s.c.looks();
    REQUIRE(looks.size() >= 2);
    QString lookId;
    for (const QVariant &v : looks)
        if (v.toMap()[u"colors"_s].toStringList().value(1) != before)
            lookId = v.toMap()[u"id"_s].toString();
    REQUIRE_FALSE(lookId.isEmpty());

    QVariantMap t = s.pos.adminNewRecord(u"terminals"_s);
    t[u"name"_s] = s.pos.terminalName();
    t[u"look"_s] = lookId;
    REQUIRE(s.pos.adminSave(u"terminals"_s, -1, t));
    QTest::qWait(60);
    CHECK(s.pos.terminalLook() == lookId);
    CHECK(fillOf(u"checks"_s) != before);                      // this screen, recolored
    s.shot("77-terminal-look");

    // The store's look is untouched; clearing goes back to it.
    t = s.pos.adminRecords(u"terminals"_s).last().toMap();
    t[u"look"_s] = QString();
    REQUIRE(s.pos.adminSave(u"terminals"_s, s.pos.adminRecords(u"terminals"_s).size() - 1, t));
    QTest::qWait(60);
    CHECK(fillOf(u"checks"_s) == before);
}

TEST_CASE("UI: course pacing: fire the next course in 10 minutes", "[flow][ui][pacing]")
{
    Screen s;
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.selectTable(u"T4"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    s.pos.addItem(u"house-salad"_s);                            // course 1
    REQUIRE(s.pos.chooseOption(u"dressing"_s, 0));
    REQUIRE(s.pos.chooseOption(u"salad-protein"_s, 0));
    s.pos.finishChoosing();
    s.pos.selectLine(0);                                         // course 2 for what's ordered next
    s.pos.setCourse(2);
    s.pos.addItem(u"cobb"_s);                                   // course 2: held
    REQUIRE(s.pos.chooseOption(u"salad-protein"_s, 0));
    s.pos.finishChoosing();
    REQUIRE(s.pos.sendOrder());
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    QTest::qWait(60);

    s.tapItem(find(u"fireLater"_s));
    QTest::qWait(40);
    REQUIRE(find(u"fireIn-10"_s));
    s.shot("76-fire-in");
    s.tapItem(find(u"fireIn-10"_s));
    QTest::qWait(40);
    QVariantMap check = s.pos.checkInfo();
    CHECK_FALSE(check[u"firesAt"_s].toString().isEmpty());
    CHECK(check[u"heldCount"_s].toInt() == 1);
    CHECK(s.pos.fireDueOrders() == 0);                          // not yet

    // Ten minutes later (the store's 30-second check): it fires by itself.
    for (auto &[id, c] : s.pos.shared()->open)
        if (c.fireAt > 0)
            c.fireAt = QDateTime::currentMSecsSinceEpoch() - 1000;
    CHECK(s.pos.fireDueOrders() == 1);
    check = s.pos.checkInfo();
    CHECK(check[u"heldCount"_s].toInt() == 0);
    CHECK(check[u"firesAt"_s].toString().isEmpty());

    // Changed one's mind: back to firing by hand.
    s.pos.selectLine(0);
    s.pos.setCourse(3);
    s.pos.addItem(u"water"_s);
    CHECK_FALSE(s.pos.sendOrder());                              // held: "fire it when it's time"
    REQUIRE(s.pos.fireCourseIn(15));
    REQUIRE(s.pos.fireCourseIn(-1));
    CHECK(s.pos.checkInfo()[u"firesAt"_s].toString().isEmpty());
}

TEST_CASE("UI: kitchen tickets are late past what their items usually take", "[flow][ui][preptimes]")
{
    Screen s;
    s.pos.shared()->settings.prepSeconds["cobb"] = 600;          // usually 10 minutes
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    s.pos.addItem(u"cobb"_s);
    s.pos.finishChoosing();
    REQUIRE(s.pos.sendOrder());
    QVariantMap ticket = s.pos.kitchenTickets().value(0).toMap();
    CHECK(ticket[u"targetMinutes"_s].toInt() == 10);
    CHECK(ticket[u"lateMinutes"_s].toInt() == 12);
    CHECK(ticket[u"warnMinutes"_s].toInt() == 7);

    // Twenty minutes ago: LATE on the kitchen screen.
    for (auto &[id, c] : s.pos.shared()->open)
        for (core::OrderLine &l : c.lines)
            l.sentAt -= 20 * 60'000;
    REQUIRE(s.c.jumpTo(u"kitchen"_s));
    QTest::qWait(1100);
    bool late = false;
    std::function<void(QQuickItem *)> walk = [&](QQuickItem *it) {
        if (it->isVisible() && it->property("text").toString().startsWith(u"LATE"_s))
            late = true;
        for (QQuickItem *c : it->childItems())
            walk(c);
    };
    walk(s.window->contentItem());
    CHECK(late);
    s.shot("75-kitchen-late");

    // Bumped: what it took (20 minutes) moves the usual time up.
    ticket = s.pos.kitchenTickets().value(0).toMap();
    REQUIRE(s.pos.bumpTicket(ticket[u"checkId"_s].toLongLong(), ticket[u"sentAt"_s].toLongLong(), {}));
    CHECK(s.pos.shared()->settings.prepSeconds["cobb"] == 720);   // 0.8 x 600 + 0.2 x 1200

    // A manager's own target wins.
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    const QVariantList menu = s.pos.adminRecords(u"menu"_s);
    for (int i = 0; i < menu.size(); ++i)
        if (menu[i].toMap()[u"id"_s] == u"cobb"_s) {
            QVariantMap r = menu[i].toMap();
            r[u"prepMinutes"_s] = 5;
            REQUIRE(s.pos.adminSave(u"menu"_s, i, r));
        }
    CHECK(s.pos.prepMinutesFor("cobb") == 5);
}

TEST_CASE("UI: opening and closing checklists, ticked by whoever does them", "[flow][ui][checklists]")
{
    Screen s;
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.c.jumpTo(u"checklists"_s));
    QTest::qWait(60);
    s.tapItem(find(u"task-opening-0"_s));
    QTest::qWait(40);
    QVariantList opening = s.pos.checklists()[u"opening"_s].toList();
    REQUIRE(opening.size() == 5);
    CHECK(opening[0].toMap()[u"done"_s].toBool());
    CHECK(opening[0].toMap()[u"by"_s] == u"Sam"_s);
    s.tapItem(find(u"task-opening-0"_s));                      // touched again: undone
    QTest::qWait(40);
    CHECK_FALSE(s.pos.checklists()[u"opening"_s].toList()[0].toMap()[u"done"_s].toBool());
    s.tapItem(find(u"task-opening-0"_s));
    s.tapItem(find(u"task-closing-0"_s));
    s.tapItem(find(u"task-closing-1"_s));
    QTest::qWait(40);
    CHECK(s.pos.checklists()[u"closingDone"_s].toInt() == 2);
    s.shot("74-checklists");

    // End of Day shows what's left; the report lists who did what.
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    CHECK(s.pos.dayInfo()[u"closingLeft"_s].toInt() == 3);
    REQUIRE(s.c.jumpTo(u"end-of-day"_s));
    QTest::qWait(60);
    CHECK(find(u"eodChecklist"_s));
    const core::Report r = s.pos.buildReport(u"checklists"_s);
    int notDone = 0, bySam = 0;
    for (const core::ReportRow &row : r.rows)
        if (row.cells.size() == 3) {
            notDone += row.cells[1] == "NOT DONE";
            bySam += row.cells[1] == "Sam";
        }
    CHECK(notDone == 7);
    CHECK(bySam == 3);
}

TEST_CASE("UI: time off and shift swaps from the Time Clock, decided by a manager", "[flow][ui][requests]")
{
    Screen s;
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    // Sam works tomorrow 4-10 PM.
    const QDateTime tomorrow(QDate::currentDate().addDays(1), QTime(16, 0));
    core::Shift shift;
    shift.id = 501;
    shift.employeeId = "sam";
    shift.start = tomorrow.toMSecsSinceEpoch();
    shift.end = tomorrow.addSecs(6 * 3600).toMSecsSinceEpoch();
    s.pos.shared()->shifts.push_back(shift);
    REQUIRE(s.c.jumpTo(u"time-clock"_s));

    // Sam gives it away, and asks for a day off three days out.
    REQUIRE(s.pos.timeClockStart(u"1111"_s));
    QTest::qWait(60);
    s.tapItem(find(u"giveAway-501"_s));
    QTest::qWait(40);
    CHECK_FALSE(find(u"giveAway-501"_s));                       // now "up for grabs"
    s.tapItem(find(u"timeOff"_s));
    QTest::qWait(40);
    REQUIRE(find(u"offPicker"_s));
    s.tapItem(find(u"offDay-3"_s));
    s.shot("72-time-off");
    s.tapItem(find(u"askOff"_s));
    QTest::qWait(40);
    QVariantList mine = s.pos.timeClock()[u"requests"_s].toMap()[u"mine"_s].toList();
    REQUIRE(mine.size() == 2);
    CHECK_FALSE(s.pos.timeClockRequestOff(QDate::currentDate().addDays(3).toString(u"yyyy-MM-dd"_s), {}));   // again
    s.pos.timeClockDone();

    // Casey sees it up for grabs and takes it.
    REQUIRE(s.pos.timeClockStart(u"2222"_s));
    QTest::qWait(60);
    const QVariantList grabs = s.pos.timeClock()[u"requests"_s].toMap()[u"upForGrabs"_s].toList();
    REQUIRE(grabs.size() == 1);
    const qint64 swapId = grabs[0].toMap()[u"id"_s].toLongLong();
    s.shot("73-up-for-grabs");
    s.tapItem(find(u"take-"_s + QString::number(swapId)));
    QTest::qWait(40);
    CHECK(s.pos.timeClock()[u"requests"_s].toMap()[u"upForGrabs"_s].toList().isEmpty());
    s.pos.timeClockDone();

    // The manager: two waiting (on the dashboard too); the swap approved, the day off not.
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    CHECK(s.pos.dashboard()[u"requestsWaiting"_s].toInt() == 2);
    QVariantList records = s.pos.adminRecords(u"requests"_s);
    REQUIRE(records.size() == 2);
    for (int i = 0; i < records.size(); ++i) {
        QVariantMap r = records[i].toMap();
        r[u"status"_s] = r[u"_title"_s].toString().contains(u"Give away"_s) ? u"approved"_s : u"denied"_s;
        REQUIRE(s.pos.adminSave(u"requests"_s, i, r));
        records = s.pos.adminRecords(u"requests"_s);   // re-sorted: waiting first
        i = -1;
        if (std::ranges::none_of(records, [](const QVariant &v) { return v.toMap()[u"status"_s] == u"pending"_s; }))
            break;
    }
    CHECK(s.pos.shared()->shifts.back().employeeId == "casey");
    CHECK(s.pos.dashboard()[u"requestsWaiting"_s].toInt() == 0);
    REQUIRE(s.c.jumpTo(u"admin-schedule"_s));
    s.c.activate(u"requests"_s);
    QTest::qWait(60);
    CHECK(s.c.pageId() == u"admin-requests"_s);
    s.pos.logout();

    // Sam sees how both went.
    REQUIRE(s.pos.timeClockStart(u"1111"_s));
    mine = s.pos.timeClock()[u"requests"_s].toMap()[u"mine"_s].toList();
    QStringList statuses;
    for (const QVariant &v : mine)
        statuses << v.toMap()[u"status"_s].toString();
    statuses.sort();
    CHECK(statuses == QStringList{u"approved"_s, u"denied"_s});
}

TEST_CASE("UI: overtime warnings on the Time Clock, at clock-in and on the dashboard", "[flow][ui][overtime]")
{
    Screen s;
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    const std::int64_t hour = 3'600'000;
    // An 8-hour pay week that started yesterday; Sam already worked 6.5 h of it.
    core::PosSettings &st = s.pos.shared()->settings;
    st.overtimeWeeklyHours = 8;
    st.overtimeDailyHours = 0;
    st.weekStartsOn = QDate::currentDate().addDays(-1).dayOfWeek() % 7;
    s.pos.shared()->punches.push_back({931, "sam", now - 8 * hour, now - 3 * hour / 2, {}, "server", vt::Money::fromCents(1200)});
    QStringList notices;
    QObject::connect(&s.pos, &app::PosSession::notice, [&](const QString &t) { notices << t; });

    REQUIRE(s.pos.timeClockStart(u"1111"_s));
    QVariantMap ot = s.pos.timeClock()[u"overtime"_s].toMap();
    CHECK(ot[u"state"_s] == u"soon"_s);
    CHECK(ot[u"left"_s] == u"1.5"_s);
    REQUIRE(s.pos.timeClockAct(u"in"_s));
    CHECK(notices.join(u"|"_s).contains(u"reaches overtime in 1.5 h"_s));
    REQUIRE(s.c.jumpTo(u"time-clock"_s));
    QTest::qWait(60);
    REQUIRE(Screen::findBy(s.window->contentItem(), "objectName", u"clockOvertime"_s));
    s.shot("71-overtime-soon");
    s.pos.timeClockDone();

    // The dashboard flags it too.
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    bool flagged = false;
    for (const QVariant &v : s.pos.dashboard()[u"labor"_s].toMap()[u"onClock"_s].toList())
        flagged = flagged || (v.toMap()[u"name"_s] == u"Sam"_s && v.toMap()[u"overtime"_s] == u"soon"_s);
    CHECK(flagged);

    // Past it: "over".
    st.overtimeWeeklyHours = 6;
    s.pos.logout();
    REQUIRE(s.pos.timeClockStart(u"1111"_s));
    CHECK(s.pos.timeClock()[u"overtime"_s].toMap()[u"state"_s] == u"over"_s);
}

TEST_CASE("UI: ring items in by number (keyboard, or Find)", "[flow][ui][plu]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    REQUIRE(s.c.jumpTo(u"index-lunch"_s));
    QTest::qWait(40);
    // 203 Enter: Cobb (salads start at 201).
    for (const char *k : {"2", "0", "3"})
        QTest::keyClick(s.window, *k);
    CHECK(s.c.statusText().contains(u"#203"_s));
    QTest::keyClick(s.window, Qt::Key_Return);
    QTest::qWait(60);
    REQUIRE(s.pos.lines().size() == 1);
    CHECK(s.pos.lines()[0].toMap()[u"name"_s] == u"Cobb"_s);
    s.c.finishChoosing();
    REQUIRE(s.c.jumpTo(u"index-lunch"_s));
    for (const char *k : {"9", "9", "9"})
        QTest::keyClick(s.window, *k);
    QTest::keyClick(s.window, Qt::Key_Return);
    QTest::qWait(40);
    CHECK(s.c.statusText().contains(u"No item has number 999"_s));
    CHECK(s.pos.lines().size() == 1);

    // Find: "30" lists the drinks by number.
    s.c.activate(u"tab-find"_s);
    QTest::qWait(60);
    s.pos.textKey(u"3"_s);
    s.pos.textKey(u"0"_s);
    s.pos.textKey(u"5"_s);
    QTest::qWait(60);
    CHECK(Screen::findBy(s.window->contentItem(), "objectName", u"menuItem-water"_s));   // 305
    CHECK_FALSE(Screen::findBy(s.window->contentItem(), "objectName", u"menuItem-coffee"_s));

    // Numbers are each item's own.
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    const QVariantList menu = s.pos.adminRecords(u"menu"_s);
    for (int i = 0; i < menu.size(); ++i)
        if (menu[i].toMap()[u"id"_s] == u"caesar"_s) {
            QVariantMap r = menu[i].toMap();
            r[u"number"_s] = u"203"_s;
            CHECK_FALSE(s.pos.adminSave(u"menu"_s, i, r));
            r[u"number"_s] = u"12a"_s;
            CHECK_FALSE(s.pos.adminSave(u"menu"_s, i, r));
            r[u"number"_s] = u"250"_s;
            CHECK(s.pos.adminSave(u"menu"_s, i, r));
        }
}

TEST_CASE("UI: the manager's dashboard: today so far", "[flow][ui][dashboard]")
{
    Screen s;
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    const std::int64_t hour = 3'600'000;
    // Sam on the clock 4 hours at $12; buns running low.
    s.pos.shared()->punches.push_back({921, "sam", now - 4 * hour, 0, {}, "server", vt::Money::fromCents(1200)});
    for (core::Ingredient &g : s.pos.shared()->ingredients)
        if (g.id == "bun")
            g.onHand = 4;
    // A cobb sold ($12.50), and a check still open.
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    CHECK_FALSE(s.pos.dashboard().isEmpty());
    REQUIRE(s.pos.startCheck(core::CheckType::Quick));
    s.pos.addItem(u"cobb"_s);
    s.pos.finishChoosing();
    REQUIRE(s.pos.sendOrder());
    REQUIRE(s.pos.tender(u"credit"_s));
    REQUIRE(s.pos.closeCheck());
    // Last week by this time: half as much.
    core::Check lastWeek = s.pos.shared()->closedToday.back();
    lastWeek.payments.clear();
    for (core::OrderLine &l : lastWeek.lines)
        l.unitPrice = vt::Money::fromCents(625);
    s.pos.shared()->history = [lastWeek](std::int64_t, std::int64_t) { return std::vector<core::Check>{lastWeek}; };
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    s.pos.addItem(u"water"_s);
    s.pos.releaseCheck();

    const QVariantMap d = s.pos.dashboard();
    const QVariantMap sales = d[u"sales"_s].toMap();
    CHECK(sales[u"net"_s] == u"$12.50"_s);
    CHECK(sales[u"checks"_s].toInt() == 1);
    CHECK(sales[u"change"_s].toInt() == 100);                    // twice last week's
    const QVariantMap labor = d[u"labor"_s].toMap();
    CHECK(labor[u"cost"_s] == u"$48.00"_s);                      // 4 h x $12
    CHECK(labor[u"percent"_s].toInt() == 384);
    CHECK(labor[u"onClock"_s].toList().size() == 1);
    CHECK(d[u"open"_s].toMap()[u"count"_s].toInt() == 1);
    CHECK(d[u"top"_s].toList().value(0).toMap()[u"name"_s] == u"Cobb"_s);
    bool bun = false;
    for (const QVariant &v : d[u"low"_s].toList())
        bun = bun || v.toMap()[u"name"_s].toString().contains(u"un"_s);
    CHECK(bun);

    // Manager -> Dashboard; not for servers.
    REQUIRE(s.c.jumpTo(u"manager"_s));
    s.c.activate(u"dashboard"_s);
    QTest::qWait(80);
    REQUIRE(s.c.pageId() == u"dashboard"_s);
    REQUIRE(Screen::findBy(s.window->contentItem(), "objectName", u"dashSales"_s));
    s.shot("70-dashboard");
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    CHECK(s.pos.dashboard().isEmpty());
}

TEST_CASE("UI: dishes running low show how many are left", "[flow][ui][stock]")
{
    Screen s;
    for (core::Ingredient &g : s.pos.shared()->ingredients)
        if (g.id == "bun")
            g.onHand = 4;                                       // low at 12
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    QVariantMap left = s.pos.stockLeft();
    CHECK(left.value(u"classic-burger"_s).toInt() == 4);
    CHECK(left.value(u"cheeseburger"_s).toInt() == 4);
    CHECK_FALSE(left.contains(u"cobb"_s));                      // no buns in it
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    QTest::qWait(60);
    REQUIRE(Screen::findBy(s.window->contentItem(), "text", u"4 left"_s));

    // One sells: 3 left.
    s.pos.addItem(u"classic-burger"_s);
    REQUIRE(s.pos.chooseOption(u"temperature"_s, 1));
    REQUIRE(s.pos.chooseOption(u"side"_s, 2));
    REQUIRE(s.pos.finishChoosing());
    REQUIRE(s.pos.sendOrder());
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    QTest::qWait(60);
    CHECK(s.pos.stockLeft().value(u"classic-burger"_s).toInt() == 3);
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"3 left"_s));
    s.shot("69-stock-left");
}

TEST_CASE("Time Punches reaches a month back; the dashboard refreshes", "[flow][punches][older]")
{
    Screen s;
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    const std::int64_t day = 24 * 3'600'000;
    // In the store (not loaded at start): Sam, 20 days ago, and 40 days ago.
    std::vector<core::TimePunch> stored{
        {951, "sam", now - 20 * day, now - 20 * day + 8 * 3'600'000, {}, "server", vt::Money::fromCents(1200)},
        {952, "sam", now - 40 * day, now - 40 * day + 8 * 3'600'000, {}, "server", vt::Money::fromCents(1200)}};
    s.pos.shared()->punchHistory = [stored](std::int64_t from, std::int64_t to) {
        std::vector<core::TimePunch> out;
        for (const core::TimePunch &p : stored)
            if (p.clockIn >= from && p.clockIn < to)
                out.push_back(p);
        return out;
    };
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    QVariantList records = s.pos.adminRecords(u"punches"_s);
    int i = -1;
    for (int r = 0; r < records.size(); ++r) {
        CHECK(records[r].toMap()[u"id"_s].toLongLong() != 952);   // over a month: not listed
        if (records[r].toMap()[u"id"_s].toLongLong() == 951)
            i = r;
    }
    REQUIRE(i >= 0);
    QVariantMap r = records[i].toMap();
    r[u"clockOut"_s] = QDateTime::fromMSecsSinceEpoch(now - 20 * day + 9 * 3'600'000).toString(u"yyyy-MM-dd HH:mm"_s);
    r[u"reason"_s] = u"stayed late"_s;
    REQUIRE(s.pos.adminSave(u"punches"_s, i, r));
    CHECK(s.pos.shared()->settings.punchChanges.back().why == "stayed late");

    QSignalSpy dayChanged(&s.pos, &app::PosSession::dayChanged);
    s.pos.refreshDay();
    CHECK(dayChanged.count() >= 1);
}

TEST_CASE("UI: End of Day lists who's still clocked in, and clocks them out", "[flow][ui][punches][endofday]")
{
    Screen s;
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    const std::int64_t hour = 3'600'000;
    s.pos.shared()->punches.push_back({911, "sam", now - 14 * hour, 0, {}, "server", vt::Money::fromCents(1200)});   // forgot
    s.pos.shared()->punches.push_back({912, "casey", now - 2 * hour, 0, {}, "cashier", vt::Money::fromCents(1100)});
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.c.jumpTo(u"end-of-day"_s));
    QTest::qWait(80);
    const QVariantList in = s.pos.dayInfo()[u"clockedIn"_s].toList();
    REQUIRE(in.size() == 2);
    bool samLong = false, caseyLong = true;
    for (const QVariant &v : in) {
        if (v.toMap()[u"name"_s] == u"Sam"_s) samLong = v.toMap()[u"overTwelve"_s].toBool();
        if (v.toMap()[u"name"_s] == u"Casey"_s) caseyLong = v.toMap()[u"overTwelve"_s].toBool();
    }
    CHECK(samLong);
    CHECK_FALSE(caseyLong);
    QQuickItem *out = Screen::findBy(s.window->contentItem(), "objectName", u"eodClockOut-911"_s);
    REQUIRE(out);
    s.shot("68-end-of-day-clocked-in");
    s.tapItem(out);
    QTest::qWait(60);
    CHECK(s.pos.dayInfo()[u"clockedIn"_s].toList().size() == 1);
    REQUIRE(s.pos.shared()->settings.punchChanges.size() == 1);
    CHECK(s.pos.shared()->settings.punchChanges[0].employee == "Sam");
}

TEST_CASE("UI: the Time Clock: clock in and out, breaks and the schedule, by PIN alone", "[flow][ui][timeclock]")
{
    Screen s;
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    const auto pin = [&](const QString &digits) {
        for (const QChar ch : digits) {
            s.tapItem(find(u"clockKey-"_s + ch));
            QTest::qWait(20);
        }
        s.tapItem(find(u"clockKey-OK"_s));
        QTest::qWait(60);
    };
    // Sam works today and the day after tomorrow.
    const QDateTime today(QDate::currentDate(), QTime(0, 0));
    for (const int day : {0, 2}) {
        core::Shift shift;
        shift.id = 100 + day;
        shift.employeeId = "sam";
        shift.start = today.addDays(day).addSecs(6 * 3600).toMSecsSinceEpoch();
        shift.end = today.addDays(day).addSecs(23 * 3600 + 59 * 60).toMSecsSinceEpoch();
        shift.note = day == 2 ? "patio" : "";
        s.pos.shared()->shifts.push_back(shift);
    }

    // From the login page.
    s.c.activate(u"time-clock"_s);
    QTest::qWait(60);
    REQUIRE(s.c.pageId() == u"time-clock"_s);
    pin(u"9999"_s);
    CHECK(s.pos.timeClock().isEmpty());                         // not a PIN
    pin(u"1111"_s);
    QVariantMap info = s.pos.timeClock();
    REQUIRE(info[u"name"_s] == u"Sam"_s);
    CHECK_FALSE(s.pos.loggedIn());                              // never logged in to the register
    CHECK(info[u"status"_s] == u"out"_s);
    const QVariantList shifts = info[u"shifts"_s].toList();
    REQUIRE(shifts.size() == 2);
    CHECK(shifts[0].toMap()[u"day"_s] == u"Today"_s);
    CHECK(shifts[0].toMap()[u"now"_s].toBool());
    CHECK(shifts[1].toMap()[u"note"_s] == u"patio"_s);
    REQUIRE(find(u"clockIn"_s));

    s.tapItem(find(u"clockIn"_s));
    QTest::qWait(60);
    CHECK(s.pos.timeClock()[u"status"_s] == u"in"_s);
    s.shot("66-time-clock");
    s.tapItem(find(u"clockBreak"_s));
    QTest::qWait(40);
    CHECK(s.pos.timeClock()[u"status"_s] == u"break"_s);
    s.tapItem(find(u"clockBreak"_s));
    QTest::qWait(40);
    CHECK(s.pos.timeClock()[u"status"_s] == u"in"_s);
    s.tapItem(find(u"clockOut"_s));
    QTest::qWait(40);
    CHECK(s.pos.timeClock()[u"status"_s] == u"out"_s);
    CHECK(s.pos.timeClock()[u"punches"_s].toList().size() == 1);
    s.tapItem(find(u"clockDone"_s));
    QTest::qWait(40);
    CHECK(s.pos.timeClock().isEmpty());
    REQUIRE(find(u"clockKey-OK"_s));
    CHECK_FALSE(s.pos.loggedIn());

    // A terminal set to Time Clock rests there: after a logout too.
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    QVariantMap t = s.pos.adminNewRecord(u"terminals"_s);
    t[u"name"_s] = s.pos.terminalName();
    t[u"screen"_s] = u"timeClock"_s;
    REQUIRE(s.pos.adminSave(u"terminals"_s, -1, t));
    s.pos.logout();
    QTest::qWait(60);
    CHECK(s.c.pageId() == u"time-clock"_s);
    // "Log In to the Register…" is still there for a manager.
    s.c.activate(u"register"_s);
    QTest::qWait(60);
    CHECK(s.c.pageId() == u"login"_s);
}

TEST_CASE("UI: holding a button explains it instead of pressing it", "[flow][ui][explain]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    QTest::qWait(60);
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    QQuickItem *send = Screen::findBy(s.window->contentItem(), "text", u"Send"_s);
    REQUIRE(send);
    // Held: nothing sent, the card says what it does.
    const QPoint at = send->mapToScene(QPointF(send->width() / 2, send->height() / 2)).toPoint();
    QTest::mousePress(s.window, Qt::LeftButton, {}, at);
    QTest::qWait(1000);
    QTest::mouseRelease(s.window, Qt::LeftButton, {}, at);
    QTest::qWait(60);
    REQUIRE(find(u"explainCard"_s));
    CHECK(s.c.explanation().value(u"text"_s).toString().contains(u"kitchen"_s));
    s.shot("63-explain");
    CHECK(s.pos.lines().isEmpty());

    // An item button names the item and its price; a page button the page.
    s.c.explain(u"item-1"_s);
    CHECK(s.c.explanation().value(u"text"_s).toString().startsWith(u"Adds Classic Burger ($"_s));
    s.c.explain(u"tab-find"_s);
    CHECK(s.c.explanation().value(u"text"_s).toString() != QString());

    // A touch closes it.
    s.tapItem(find(u"explainCard"_s));
    QTest::qWait(40);
    CHECK_FALSE(find(u"explainCard"_s));
}

TEST_CASE("UI: the Popular page fills itself with today's best sellers", "[flow][ui][popular]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    CHECK(s.pos.popularItems().isEmpty());
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    s.pos.addItem(u"water"_s);
    s.pos.addItem(u"cobb"_s);
    s.pos.finishChoosing();
    REQUIRE(s.pos.setLineQuantity(0, 3));      // 3 Cobbs: first
    s.pos.addItem(u"water"_s);                // 2 waters
    const QStringList top = s.pos.popularItems();
    REQUIRE(top.size() == 2);
    CHECK(top[0] == u"cobb"_s);
    CHECK(top[1] == u"water"_s);

    REQUIRE(s.c.jumpTo(u"index-lunch"_s));
    s.c.activate(u"cat-menu-popular"_s);
    QTest::qWait(80);
    REQUIRE(s.c.pageId() == u"menu-popular"_s);
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    QQuickItem *cobb = find(u"menuItem-cobb"_s);
    QQuickItem *water = find(u"menuItem-water"_s);
    REQUIRE(cobb);
    REQUIRE(water);
    CHECK(cobb->mapToScene({0, 0}).x() < water->mapToScene({0, 0}).x());   // most sold first
    CHECK_FALSE(find(u"menuItem-caesar"_s));
    s.shot("62-popular");
}

TEST_CASE("UI: Another Round orders the drinks sent last again", "[flow][ui][round]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.selectTable(u"T3"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    REQUIRE(s.c.jumpTo(u"items-drinks"_s));
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    const auto names = [&] {
        QStringList out;
        for (const QVariant &v : s.pos.lines())
            if (!v.toMap()[u"sent"_s].toBool())
                out << v.toMap()[u"name"_s].toString();
        return out;
    };
    CHECK_FALSE(s.pos.anotherRound());                 // nothing sent yet
    s.pos.addItem(u"soda"_s);
    REQUIRE(s.pos.chooseOption(u"drink-size"_s, 1));
    REQUIRE(s.pos.finishChoosing());
    s.pos.addItem(u"draft-beer"_s);
    REQUIRE(s.pos.chooseOption(u"draft"_s, 0));
    REQUIRE(s.pos.finishChoosing());
    REQUIRE(s.pos.setLineQuantity(0, 2));              // two beers
    s.pos.addItem(u"bacon-burger"_s);                  // not a drink
    REQUIRE(s.pos.chooseOption(u"temperature"_s, 1));
    REQUIRE(s.pos.chooseOption(u"side"_s, 2));
    REQUIRE(s.pos.finishChoosing());
    REQUIRE(s.pos.sendOrder());
    REQUIRE(s.c.jumpTo(u"items-drinks"_s));
    QTest::qWait(60);

    QQuickItem *round = find(u"anotherRound"_s);
    REQUIRE(round);
    s.shot("61-another-round");
    s.tapItem(round);
    QTest::qWait(60);
    CHECK(names().size() == 2);                        // the soda and the beers, not the burger
    const QVariantList lines = s.pos.lines();
    CHECK(lines.last().toMap()[u"quantity"_s].toInt() == 2);
    CHECK_FALSE(lines.last().toMap()[u"modifiers"_s].toList().isEmpty());   // same pour

    // The next round is the drinks of the latest Send only.
    REQUIRE(s.pos.sendOrder());
    s.pos.addItem(u"water"_s);
    REQUIRE(s.pos.sendOrder());
    REQUIRE(s.pos.anotherRound());
    CHECK(names() == QStringList{u"Water"_s});
}

TEST_CASE("UI: Undo puts back the item just removed", "[flow][ui][undo]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    s.pos.addItem(u"water"_s);
    s.pos.addItem(u"bacon-burger"_s);
    REQUIRE(s.pos.chooseOption(u"temperature"_s, 1));
    REQUIRE(s.pos.chooseOption(u"side"_s, 2));
    REQUIRE(s.pos.finishChoosing());
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    s.pos.addItem(u"water"_s);
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    CHECK_FALSE(find(u"undoBar"_s));

    // Take the burger off: the bar offers it back, in its place, choices and all.
    const QVariantMap burger = s.pos.lines().value(1).toMap();
    s.pos.selectLine(burger[u"id"_s].toLongLong());
    REQUIRE(s.pos.voidItem());
    QTest::qWait(60);
    REQUIRE(s.pos.lines().size() == 2);
    REQUIRE(find(u"undoBar"_s));
    CHECK(s.pos.undoText() == u"Removed Bacon Burger"_s);
    s.shot("60-undo");
    s.tapItem(find(u"undoLast"_s));
    QTest::qWait(60);
    REQUIRE(s.pos.lines().size() == 3);
    CHECK(s.pos.lines().value(1).toMap()[u"name"_s] == burger[u"name"_s]);
    CHECK(s.pos.lines().value(1).toMap()[u"modifiers"_s].toList().size() == burger[u"modifiers"_s].toList().size());
    CHECK_FALSE(find(u"undoBar"_s));
    CHECK_FALSE(s.pos.undoLast());                 // once

    // Fewer: Undo puts the number back.
    s.pos.selectLine(burger[u"id"_s].toLongLong());
    REQUIRE(s.pos.setLineQuantity(0, 3));
    REQUIRE(s.pos.changeLineQuantity(0, -1));
    CHECK(s.pos.undoText().contains(u"2 instead of 3"_s));
    REQUIRE(s.pos.undoLast());
    CHECK(s.pos.lines().value(1).toMap()[u"quantity"_s].toInt() == 3);

    // Only on the check it happened on.
    REQUIRE(s.pos.voidItem());
    s.pos.releaseCheck();
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    CHECK(s.pos.undoText().isEmpty());
}

TEST_CASE("UI: − 2 + and Again on a touched line", "[flow][ui][quantity]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.selectTable(u"T4"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    s.pos.addItem(u"bacon-burger"_s);
    REQUIRE(s.pos.chooseOption(u"temperature"_s, 1));   // Medium Rare
    REQUIRE(s.pos.chooseOption(u"side"_s, 2));          // Onion Rings (+1.00)
    REQUIRE(s.pos.finishChoosing());
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    QTest::qWait(60);
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    const auto line = [&](int i) { return s.pos.lines().value(i).toMap(); };
    const qint64 one = std::llround(line(0)[u"price"_s].toString().remove(u'$').toDouble() * 100);

    // The new line is the touched one: + + makes it 3.
    QQuickItem *more = find(u"lineMore"_s);
    REQUIRE(more);
    s.tapItem(more);
    QTest::qWait(40);
    s.tapItem(find(u"lineMore"_s));
    QTest::qWait(40);
    REQUIRE(s.pos.lines().size() == 1);
    CHECK(line(0)[u"quantity"_s].toInt() == 3);
    CHECK(std::llround(line(0)[u"price"_s].toString().remove(u'$').toDouble() * 100) == one * 3);
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"3 × Bacon Burger"_s));
    s.shot("57-quantity");
    s.tapItem(find(u"lineLess"_s));
    QTest::qWait(40);
    CHECK(line(0)[u"quantity"_s].toInt() == 2);

    // Again: one more the same way, choices and all, as its own line.
    s.tapItem(find(u"lineAgain"_s));
    QTest::qWait(40);
    REQUIRE(s.pos.lines().size() == 2);
    CHECK(line(1)[u"quantity"_s].toInt() == 1);
    CHECK(line(1)[u"modifiers"_s].toList().size() == line(0)[u"modifiers"_s].toList().size());
    CHECK(line(1)[u"selected"_s].toBool());

    // − on the last one takes it off the check.
    s.tapItem(find(u"lineLess"_s));
    QTest::qWait(40);
    CHECK(s.pos.lines().size() == 1);

    // Sent: no − / +, only Again (a new line, sent with the next Send).
    REQUIRE(s.pos.sendOrder());
    s.pos.selectLine(line(0)[u"id"_s].toLongLong());
    QTest::qWait(40);
    CHECK_FALSE(find(u"lineMore"_s));
    CHECK_FALSE(s.pos.setLineQuantity(line(0)[u"id"_s].toLongLong(), 5));
    s.tapItem(find(u"lineAgain"_s));
    QTest::qWait(40);
    REQUIRE(s.pos.lines().size() == 2);
    CHECK_FALSE(line(1)[u"sent"_s].toBool());
    CHECK(line(0)[u"quantity"_s].toInt() == 2);
}

TEST_CASE("UI: a table split by seat, a line moved, and back together", "[flow][ui][tablechecks]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.selectTable(u"T6"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    for (int seat = 1; seat <= 3; ++seat) {
        s.pos.setSeat(seat);
        s.pos.addItem(u"water"_s);
    }
    s.pos.addItem(u"water"_s);   // seat 3 too
    QTest::qWait(60);
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    const auto tapName = [&](const QString &name) {
        QQuickItem *it = find(name);
        REQUIRE(it);
        s.tapItem(it);
        QTest::qWait(50);
    };

    tapName(u"tableCheck-more"_s);
    s.shot("58-table-tools");
    tapName(u"splitBySeat"_s);
    REQUIRE(s.pos.tableChecks().size() == 3);
    CHECK(s.pos.lines().size() == 1);          // seat 1 stayed here
    CHECK_FALSE(find(u"orderSheet"_s));        // closed

    // Seat 1's rings go to check 3 instead (touch, Move…, Check 3).
    s.pos.selectLine(s.pos.lines().value(0).toMap().value(u"id"_s).toLongLong());
    QTest::qWait(40);
    tapName(u"lineMove"_s);
    s.shot("59-move-line");
    tapName(u"moveTo-3"_s);
    CHECK(s.pos.lines().isEmpty());
    tapName(u"tableCheck-3"_s);
    CHECK(s.pos.lines().size() == 3);          // seat 3's two and the moved one

    // Back to one check.
    tapName(u"tableCheck-more"_s);
    tapName(u"combineTableChecks"_s);
    CHECK(s.pos.tableChecks().size() == 1);
    CHECK(s.pos.lines().size() == 4);

    // Nothing has a seat: split by seat says so.
    REQUIRE(s.pos.selectTable(u"T7"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    s.pos.addItem(u"water"_s);
    CHECK_FALSE(s.pos.splitBySeat());
}

TEST_CASE("UI: separate checks at one table, switched on the order screen", "[flow][ui][tablechecks]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.selectTable(u"T5"_s) == app::PosSession::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    s.pos.addItem(u"water"_s);
    QTest::qWait(60);
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    REQUIRE(s.pos.tableChecks().size() == 1);
    QQuickItem *plus = find(u"tableCheck-new"_s);
    REQUIRE(plus);
    const qint64 first = s.pos.checkInfo().value(u"id"_s).toLongLong();

    // Guest 2 on their own check, without leaving the page.
    s.tapItem(plus);
    QTest::qWait(60);
    CHECK(s.c.pageId() == u"items-burgers"_s);
    REQUIRE(s.pos.tableChecks().size() == 2);
    CHECK(s.pos.checkInfo().value(u"label"_s).toString() == u"T5"_s);
    CHECK(s.pos.checkInfo().value(u"id"_s).toLongLong() != first);
    CHECK(s.pos.lines().isEmpty());
    s.pos.addItem(u"water"_s);
    s.pos.addItem(u"water"_s);
    s.tapItem(find(u"tableCheck-new"_s));
    QTest::qWait(60);
    REQUIRE(s.pos.tableChecks().size() == 3);
    for (int i = 0; i < 3; ++i) {   // a party of six
        s.tapItem(find(u"tableCheck-new"_s));
        QTest::qWait(40);
    }
    REQUIRE(s.pos.tableChecks().size() == 6);
    s.shot("55-table-checks");

    // Twenty: still one line; the open one shown, the others a page away.
    for (int i = 0; i < 14; ++i) {
        s.tapItem(find(u"tableCheck-new"_s));
        QTest::qWait(30);
    }
    REQUIRE(s.pos.tableChecks().size() == 20);
    QTest::qWait(60);
    CHECK(find(u"tableCheck-20"_s));            // the new one, open
    CHECK_FALSE(find(u"tableCheck-1"_s));       // a page back
    QQuickItem *prev = find(u"tableCheck-prev"_s);
    REQUIRE(prev);
    const QQuickItem *plus20 = find(u"tableCheck-new"_s);
    CHECK(plus20->height() == find(u"tableCheck-20"_s)->height());
    s.shot("56-twenty-checks");
    for (int i = 0; i < 5 && !find(u"tableCheck-1"_s); ++i) {
        s.tapItem(prev);
        QTest::qWait(40);
    }
    REQUIRE(find(u"tableCheck-1"_s));

    // Back to check 1: its own line.
    QQuickItem *one = find(u"tableCheck-1"_s);
    REQUIRE(one);
    s.tapItem(one);
    QTest::qWait(60);
    CHECK(s.pos.checkInfo().value(u"id"_s).toLongLong() == first);
    CHECK(s.pos.lines().size() == 1);
    CHECK(s.c.pageId() == u"items-burgers"_s);

    // The table now asks which check.
    s.pos.releaseCheck();
    CHECK(s.pos.selectTable(u"T5"_s) == app::PosSession::TableChooseCheck);
}

TEST_CASE("UI: find an item by typing part of its name", "[flow][ui][find]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    REQUIRE(s.c.jumpTo(u"index-lunch"_s));
    s.pos.textKey(u"x"_s);                                   // left over from somewhere
    s.c.activate(u"tab-find"_s);
    QTest::qWait(80);
    REQUIRE(s.c.pageId() == u"find-item"_s);
    CHECK(s.pos.textEntry().isEmpty());                       // Find starts clean
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    for (const QChar ch : u"cob"_s)
        s.pos.textKey(QString(ch));
    QTest::qWait(60);
    QQuickItem *cobb = find(u"menuItem-cobb"_s);
    REQUIRE(cobb);
    CHECK_FALSE(find(u"menuItem-caesar"_s));                  // only what matches
    s.shot("51-find-item");
    s.tapItem(cobb);
    QTest::qWait(60);
    CHECK(s.pos.lines().size() == 1);
    CHECK(s.pos.textEntry().isEmpty());                       // ready for the next one
    s.c.finishChoosing();                                     // Cobb's protein (optional)
    QTest::qWait(60);
    CHECK(s.c.pageId() == u"find-item"_s);

    // A word inside the name counts; nothing found says so.
    for (const QChar ch : u"bur"_s)
        s.pos.textKey(QString(ch));
    QTest::qWait(60);
    CHECK(find(u"menuItem-classic-burger"_s));                // "Classic Burger": a word starts with it
    s.pos.textKey(u"clear"_s);
    for (const QChar ch : u"zzz"_s)
        s.pos.textKey(QString(ch));
    QTest::qWait(60);
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"Nothing on the menu has \"zzz\"."_s));
}

TEST_CASE("UI: Preview shows a page on a phone, a tablet, a terminal and a kiosk", "[flow][ui][preview]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.enterEditMode();
    EditorController *e = s.c.editor();
    QVariantMap info = e->previewInfo();
    CHECK(info[u"canvasW"_s].toInt() == 1920);
    CHECK(info[u"smallest"_s].toInt() > 0);
    CHECK(info[u"phone"_s].toString().contains(u"phone"_s, Qt::CaseInsensitive));   // Tables has a phone version
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(50);
    QObject *preview = s.window->findChild<QObject *>(u"pagePreview"_s);
    REQUIRE(preview);
    QMetaObject::invokeMethod(preview, "open");
    QTest::qWait(300);
    auto *content = preview->property("contentItem").value<QQuickItem *>();
    REQUIRE(content);
    for (const char *id : {"phone", "tablet", "terminal", "kiosk"})
        CHECK(Screen::findBy(content, "objectName", u"preview-"_s + QLatin1StringView(id)));
    QQuickItem *phone = Screen::findBy(content, "objectName", u"previewSize-phone"_s);
    QQuickItem *terminal = Screen::findBy(content, "objectName", u"previewSize-terminal"_s);
    REQUIRE(phone);
    REQUIRE(terminal);
    CHECK(phone->property("text").toString().contains(u"mm"_s));
    // The same buttons are smaller on a phone than on a terminal.
    const auto mm = [](QQuickItem *label) {
        static const QRegularExpression n(u"([0-9]+\\.[0-9])"_s);
        return n.match(label->property("text").toString()).captured(1).toDouble();
    };
    CHECK(mm(phone) < mm(terminal));
    s.shot("79-preview");
    QMetaObject::invokeMethod(preview, "close");
    s.c.leaveEditMode(false);
}

TEST_CASE("UI: ready-made layouts for each screen, and page files", "[flow][ui][layouts]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    const auto rectOf = [&](const QString &page, const QString &zone) {
        const vt::layout::Page *p = s.c.editor() ? s.c.editor()->editor().layout().page(page)
                                                 : s.c.activeLayout().page(page);
        return p && p->zone(zone) ? p->zone(zone)->rect : QRect();
    };
    QTemporaryDir dir;

    // Every main screen has five.
    for (const QString &page : {u"login"_s, u"tables"_s, u"settle"_s, u"kitchen"_s, u"index-lunch"_s}) {
        REQUIRE(s.c.jumpTo(page));
        s.c.enterEditMode();
        CHECK(s.c.editor()->arrangements().size() == 5);
        REQUIRE(s.c.leaveEditMode(false));
    }

    // The gallery on the Tables page: Buttons left.
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.enterEditMode();
    EditorController *e = s.c.editor();
    const QRect quick = rectOf(u"tables"_s, u"quick"_s);
    CHECK(Screen::findBy(s.window->contentItem(), "objectName", u"layoutsButton"_s));   // Layouts… in the toolbar
    // The earlier edit sessions' toolbars are gone first (deleted later).
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(50);
    QObject *gallery = s.window->findChild<QObject *>(u"layoutGallery"_s);
    REQUIRE(gallery);
    QMetaObject::invokeMethod(gallery, "open");                                       // what it does
    QTest::qWait(250);
    CHECK(gallery->property("visible").toBool());
    auto *galleryItem = gallery->property("contentItem").value<QQuickItem *>();
    REQUIRE(galleryItem);
    QQuickItem *left = Screen::findBy(galleryItem, "objectName", u"layout-buttons-left"_s);
    REQUIRE(left);
    s.shot("47-layout-gallery");
    s.tapItem(left);
    QTest::qWait(80);
    CHECK(rectOf(u"tables"_s, u"quick"_s).x() == 16);
    e->undo();
    CHECK(rectOf(u"tables"_s, u"quick"_s) == quick);
    REQUIRE(e->useArrangement(u"buttons-left"_s));

    // A menu page: the order screen around it (its template).
    REQUIRE(e->exportPage(QUrl::fromLocalFile(dir.filePath(u"tables.vtpage.json"_s))));
    REQUIRE(s.c.leaveEditMode(true));
    QTest::qWait(80);
    s.shot("48-tables-buttons-left");
    REQUIRE(s.c.jumpTo(u"index-lunch"_s));
    s.c.enterEditMode();
    e = s.c.editor();
    REQUIRE(e->useArrangement(u"big-send-pay"_s));
    CHECK(rectOf(u"order-template"_s, u"flow-pay"_s).width() == 334);
    REQUIRE(s.c.leaveEditMode(true));
    QTest::qWait(80);
    s.shot("49-order-big-send-pay");

    // Another store's page used for this one, then the whole restaurant from a file.
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.enterEditMode();
    e = s.c.editor();
    REQUIRE(e->useArrangement(u"classic"_s));
    CHECK(rectOf(u"tables"_s, u"quick"_s) == quick);
    REQUIRE(e->importPageHere(QUrl::fromLocalFile(dir.filePath(u"tables.vtpage.json"_s))));
    CHECK(rectOf(u"tables"_s, u"quick"_s).x() == 16);              // the file's arrangement
    CHECK(e->editor().layout().page(u"tables"_s)->role == u"tables"_s);   // still the Tables page
    REQUIRE(e->exportLayout(QUrl::fromLocalFile(dir.filePath(u"store.vtlayout.json"_s))));
    REQUIRE(e->useArrangement(u"counter"_s));
    REQUIRE(e->importLayout(QUrl::fromLocalFile(dir.filePath(u"store.vtlayout.json"_s))));
    CHECK(rectOf(u"tables"_s, u"quick"_s).x() == 16);
    REQUIRE(s.c.leaveEditMode(false));

    // Every layout of every screen, for a look (only when taking screenshots).
    if (qEnvironmentVariableIsEmpty("VTM_SHOTS"))
        return;
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    s.pos.addItem(u"caesar"_s);
    s.pos.finishChoosing();
    REQUIRE(s.pos.sendOrder());
    for (const QString &page : {u"login"_s, u"tables"_s, u"index-lunch"_s, u"settle"_s, u"kitchen"_s}) {
        REQUIRE(s.c.jumpTo(page));
        s.c.enterEditMode();
        QStringList ids;
        for (const QVariant &v : s.c.editor()->arrangements())
            ids << v.toMap()[u"id"_s].toString();
        REQUIRE(s.c.leaveEditMode(false));
        for (const QString &id : ids) {
            REQUIRE(s.c.jumpTo(page));
            s.c.enterEditMode();
            REQUIRE(s.c.editor()->useArrangement(id));
            REQUIRE(s.c.leaveEditMode(true));
            REQUIRE(s.c.jumpTo(page));
            QTest::qWait(120);
            s.shot(qPrintable(u"50-%1-%2"_s.arg(page, id)));
        }
    }
}

TEST_CASE("UI: the setup guide opens for the first manager and walks through the store", "[flow][ui][setup]")
{
    Screen s;
    s.pos.shared()->settings.setupDone = false;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));                      // a server: no guide
    QTest::qWait(60);
    CHECK_FALSE(s.c.setupOpen());
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    QTest::qWait(120);
    REQUIRE(s.c.setupOpen());
    const auto find = [&](const char *name) {
        return Screen::findBy(s.window->contentItem(), "objectName", QString::fromLatin1(name));
    };
    QQuickItem *guide = find("setupGuide");
    REQUIRE(guide);
    s.shot("46-setup-welcome");
    const auto next = [&] {
        s.tapItem(find("setupNext"));
        QTest::qWait(80);
    };
    next();                                                      // your store
    guide->setProperty("storeName", u"Taco Loco"_s);
    guide->setProperty("receiptLines", u"123 Main St · 555-0100"_s);
    s.shot("46-setup-store");
    next();                                                      // logo
    CHECK(s.pos.shared()->settings.storeName == "Taco Loco");
    QTemporaryDir dir;
    QImage logo(240, 240, QImage::Format_ARGB32);
    logo.fill(Qt::transparent);
    {
        QPainter p(&logo);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(u"#e4572e"_s));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(10, 10, 220, 220));
        p.setPen(Qt::white);
        p.setFont(QFont(u"DejaVu Sans"_s, 40, QFont::Bold));
        p.drawText(QRectF(10, 10, 220, 220), Qt::AlignCenter, u"TL"_s);
    }
    REQUIRE(logo.save(dir.filePath(u"taco.png"_s)));
    guide->setProperty("logoRef", s.pos.addImageFile(dir.filePath(u"taco.png"_s)));
    guide->setProperty("receiptLogo", true);
    QTest::qWait(80);
    s.shot("46-setup-logo");
    next();                                                      // look
    CHECK(s.pos.shared()->settings.displayLogo == "store:taco.png");
    QTest::qWait(80);
    QQuickItem *fromLogo = find("setupLook-logo-dark");
    REQUIRE(fromLogo);
    s.tapItem(fromLogo);
    QTest::qWait(80);
    CHECK(s.c.activeLayout().theme.name == u"From the logo (dark)"_s);
    s.shot("46-setup-look");
    next();                                                      // taxes
    guide->setProperty("foodTax", u"8.25"_s);
    next();                                                      // menu
    CHECK(s.pos.shared()->settings.tax.foodPpm == 82500);
    s.pos.setupAddItem(u"Fish Tacos"_s, 12.5, u"tacos"_s);
    QTest::qWait(60);
    s.shot("46-setup-menu");
    next();                                                      // staff
    s.pos.setupAddEmployee(u"Ana Ruiz"_s, u"manager"_s, u"8642"_s);
    QTest::qWait(80);
    REQUIRE(find("setupRetire"));
    s.shot("46-setup-staff");
    s.tapItem(find("setupRetire"));
    QTest::qWait(80);
    CHECK(s.pos.setupInfo()[u"samples"_s].toInt() == 0);
    next();                                                      // done
    s.shot("46-setup-done");
    next();                                                      // Finish
    CHECK_FALSE(s.c.setupOpen());
    CHECK(s.pos.shared()->settings.setupDone);
    CHECK_FALSE(s.pos.loggedIn());                               // Morgan (a sample) is off now
    CHECK(s.pos.loginWithPin(u"8642"_s));                        // Ana, the owner
}

TEST_CASE("UI: ready-made looks recolor every screen; one comes from the logo", "[flow][ui][looks]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));                 // a server can't
    CHECK_FALSE(s.c.applyLook(u"light"_s));
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    QStringList ids;
    for (const QVariant &v : s.c.looks())
        ids << v.toMap()[u"id"_s].toString();
    CHECK(ids.contains(u"light"_s));
    CHECK_FALSE(ids.contains(u"logo-dark"_s));             // no logo yet

    const auto tour = [&](const QString &look) {
        REQUIRE(s.c.applyLook(look));
        REQUIRE(s.c.jumpTo(u"tables"_s));
        QTest::qWait(80);
        s.shot(qPrintable(u"44-look-%1-tables"_s.arg(look)));
        if (!s.pos.hasCheck()) {
            REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
            s.pos.addItem(u"caesar"_s);
            s.pos.finishChoosing();
            s.pos.addItem(u"soda"_s);
            s.pos.chooseOption(u"drink-size"_s, 1);
            s.pos.finishChoosing();
        }
        REQUIRE(s.c.jumpTo(u"index-lunch"_s));
        QTest::qWait(80);
        s.shot(qPrintable(u"44-look-%1-order"_s.arg(look)));
        REQUIRE(s.c.jumpTo(u"settle"_s));
        QTest::qWait(80);
        s.shot(qPrintable(u"44-look-%1-settle"_s.arg(look)));
    };
    tour(u"light"_s);
    CHECK(s.c.activeLayout().theme.background.value(u"fill"_s).toString() == u"#e9edf2"_s);
    tour(u"contrast"_s);
    tour(u"cafe"_s);

    // With a logo: looks in its colors.
    QImage logo(200, 200, QImage::Format_ARGB32);
    logo.fill(Qt::transparent);
    for (int y = 40; y < 160; ++y)
        for (int x = 40; x < 160; ++x)
            logo.setPixel(x, y, x < 100 ? qRgb(0x0b, 0x6e, 0x4f) : qRgb(0xf2, 0xa5, 0x41));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    logo.save(&buffer, "PNG");
    REQUIRE(s.pos.addStoreImage(u"logo.png"_s, QString::fromLatin1(png.toBase64())));
    s.pos.shared()->settings.displayLogo = "store:logo.png";
    ids.clear();
    for (const QVariant &v : s.c.looks())
        ids << v.toMap()[u"id"_s].toString();
    REQUIRE(ids.contains(u"logo-dark"_s));
    tour(u"logo-dark"_s);
    tour(u"logo-light"_s);

    // In the editor: Theme tab -> Looks; one undo step.
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.enterEditMode();
    EditorController *e = s.c.editor();
    s.tapItem(Screen::findBy(s.window->contentItem(), "text", u"Theme"_s));
    QTest::qWait(120);
    QQuickItem *ocean = Screen::findBy(s.window->contentItem(), "objectName", u"look-ocean"_s);
    REQUIRE(ocean);
    s.shot("45-looks-gallery");
    s.tapItem(ocean);
    QTest::qWait(60);
    CHECK(e->editor().layout().theme.name == u"Ocean"_s);
    e->undo();
    CHECK(e->editor().layout().theme.name == u"From the logo (light)"_s);
    REQUIRE(s.c.leaveEditMode(false));
}

TEST_CASE("UI: the store's own font: added once, in every font list, on buttons", "[flow][ui][fonts]")
{
    Screen s;
    const QString garamond = QStringLiteral(VTM_SEED_DIR) + u"/../../fonts/ebgaramond/EBGaramond-Regular.ttf"_s;
    REQUIRE(QFile::exists(garamond));
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    CHECK_FALSE(s.c.fontFamilies().contains(u"EB Garamond"_s));   // not on this computer
    CHECK(s.pos.addImageFile(garamond) == u"store:ebgaramond-regular.ttf"_s);
    QTest::qWait(50);
    CHECK(s.c.fontFamilies().contains(u"EB Garamond"_s));          // installed for this screen
    bool listed = false;
    for (const QVariant &v : s.pos.storeImages())
        listed = listed || v.toMap()[u"kind"_s] == u"font"_s;
    CHECK(listed);

    // On a button, from the editor's font list.
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.enterEditMode();
    EditorController *e = s.c.editor();
    e->selectOnly({u"quick"_s});
    REQUIRE(e->setField(u"zone"_s, u"style.normal.font"_s, u"EB Garamond"_s));
    QTest::qWait(150);
    QQuickItem *choice = Screen::findBy(s.window->contentItem(), "objectName", u"fontChoice"_s);
    REQUIRE(choice);
    CHECK(choice->property("currentText").toString() == u"EB Garamond"_s);
    CHECK(Screen::findBy(s.window->contentItem(), "objectName", u"fontAdd"_s));
    REQUIRE(s.c.leaveEditMode(true));
    QTest::qWait(80);
    QQuickItem *quick = Screen::findBy(s.window->contentItem(), "zoneId", u"quick"_s);
    REQUIRE(quick);
    CHECK(quick->property("st").toMap()[u"font"_s] == u"EB Garamond"_s);
    s.shot("43-store-font");
}

TEST_CASE("UI: the menu laid out by itself: families, items, new items with no editing", "[flow][ui][menugrid]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    REQUIRE(s.c.jumpTo(u"index-lunch"_s));
    s.c.activate(u"cat-menu-all"_s);
    QTest::qWait(80);
    CHECK(s.c.pageId() == u"menu-all"_s);
    const auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    REQUIRE(find(u"menuFamily-salads"_s));
    s.tapItem(find(u"menuFamily-salads"_s));
    QTest::qWait(60);
    REQUIRE(find(u"menuItem-cobb"_s));
    s.tapItem(find(u"menuItem-cobb"_s));
    QTest::qWait(60);
    CHECK(s.pos.lines().size() == 1);
    CHECK(s.c.pageId() == u"modifiers"_s);             // its choices, as its own button would
    s.c.finishChoosing();                               // Done: back to the menu
    QTest::qWait(60);
    CHECK(s.c.pageId() == u"menu-all"_s);

    // A new salad from Manager -> Menu: its button is there, no page editing.
    QVariantMap item = s.pos.adminNewRecord(u"menu"_s);
    item[u"name"_s] = u"Soup of the Day"_s;
    item[u"price"_s] = 6.5;
    item[u"family"_s] = u"salads"_s;
    REQUIRE(s.pos.adminSave(u"menu"_s, -1, item));
    QTest::qWait(80);
    QQuickItem *soup = find(u"menuItem-soup-of-the-day"_s);
    REQUIRE(soup);
    s.shot("42-menu-grid");
    s.tapItem(soup);
    QTest::qWait(60);
    CHECK(s.pos.lines().size() == 2);

    // A burger asks how it's cooked, as its own button would.
    s.tapItem(find(u"menuFamily-burgers"_s));
    QTest::qWait(60);
    s.tapItem(find(u"menuItem-classic-burger"_s));
    QTest::qWait(80);
    CHECK(s.c.pageId() == u"modifiers"_s);
}

TEST_CASE("UI: the store's pictures: a logo on the login page and screen saver, on buttons and backgrounds",
          "[flow][ui][pictures]")
{
    Screen s;
    QTemporaryDir dir;
    s.pos.shared()->imageCacheDir = dir.filePath(u"cache"_s);
    const auto picture = [&](const QString &name, QColor color, int w, int h) {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(color);
        const QString path = dir.filePath(name);
        REQUIRE(img.save(path));
        return path;
    };
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    // Add Picture… on this computer: the store keeps it, under a plain name.
    // A logo: a red badge with the café's name, on a transparent background.
    const QString logoFile = dir.filePath(u"Cafe Logo.png"_s);
    {
        QImage img(360, 360, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(u"#c0392b"_s));
        p.setPen(QPen(Qt::white, 10));
        p.drawEllipse(QRectF(10, 10, 340, 340));
        p.setPen(Qt::white);
        QFont f(u"DejaVu Sans"_s, 34, QFont::Bold);
        p.setFont(f);
        p.drawText(QRectF(10, 90, 340, 90), Qt::AlignCenter, u"ViewTouch"_s);
        f.setPointSize(56);
        p.setFont(f);
        p.drawText(QRectF(10, 180, 340, 110), Qt::AlignCenter, u"Café"_s);
        p.end();
        REQUIRE(img.save(logoFile));
    }
    // What the receipt printer makes of it (Print the logo on receipts).
    if (const QByteArray dir = qgetenv("VTM_SHOTS"); !dir.isEmpty()) {
        QFile lf(logoFile);
        REQUIRE(lf.open(QIODevice::ReadOnly));
        const auto dots = print::rasterize(lf.readAll(), 576 * 3 / 4, 200);
        REQUIRE(dots);
        QImage paper(dots->width + 80, dots->height + 40, QImage::Format_RGB32);
        paper.fill(Qt::white);
        for (int y = 0; y < dots->height; ++y)
            for (int x = 0; x < dots->width; ++x)
                if (dots->dot(x, y))
                    paper.setPixel(x + 40, y + 20, qRgb(0, 0, 0));
        paper.save(QString::fromLocal8Bit(dir) + u"/41-receipt-logo-dots.png"_s);
    }
    const QString ref = s.pos.addImageFile(QUrl::fromLocalFile(logoFile).toString());
    CHECK(ref == u"store:cafe-logo.png"_s);
    QTest::qWait(30);
    REQUIRE(s.pos.storeImages().size() == 1);
    QVariantMap store = s.pos.adminRecords(u"store"_s).first().toMap();
    store[u"displayLogo"_s] = ref;
    REQUIRE(s.pos.adminSave(u"store"_s, 0, store));

    // On the login page.
    s.pos.logout();
    REQUIRE(s.c.jumpTo(u"login"_s));
    QTest::qWait(150);
    QQuickItem *logoZone = Screen::findBy(s.window->contentItem(), "zoneId", u"logo"_s);
    REQUIRE(logoZone);
    CHECK(logoZone->property("pictureUrl").toString().startsWith(u"file:"_s));
    s.shot("37-login-logo");

    // On the screen saver.
    s.c.setScreenSaverForTesting(100);
    QTest::qWait(400);
    QQuickItem *saverLogo = s.window->findChild<QQuickItem *>(u"screenSaverLogo"_s);
    REQUIRE(saverLogo);
    CHECK(saverLogo->isVisible());
    s.shot("38-screen-saver-logo");
    s.c.setScreenSaverForTesting(3'600'000);
    s.c.wake();

    // A picture on a button, and behind the Tables page.
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    s.pos.addImageFile(picture(u"patio.png"_s, QColor(u"#2e7d32"_s), 320, 180));
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.enterEditMode();
    EditorController *e = s.c.editor();
    e->selectOnly({u"host"_s});
    REQUIRE(e->setField(u"zone"_s, u"imagePath"_s, u"store:patio.png"_s));
    QTest::qWait(150);
    QQuickItem *choice = Screen::findBy(s.window->contentItem(), "objectName", u"imageChoice"_s);   // the Picture field
    REQUIRE(choice);
    CHECK(choice->property("currentText").toString() == u"patio.png"_s);
    CHECK(Screen::findBy(s.window->contentItem(), "objectName", u"imageAdd"_s));
    s.shot("40-picture-field");
    e->clearSelection();
    REQUIRE(e->setField(u"page"_s, u"background.image"_s, u"store:patio.png"_s));
    REQUIRE(e->setField(u"page"_s, u"background.imageFit"_s, u"cover"_s));
    REQUIRE(s.c.leaveEditMode(true));
    QTest::qWait(150);
    QQuickItem *host = Screen::findBy(s.window->contentItem(), "zoneId", u"host"_s);
    REQUIRE(host);
    CHECK(host->property("pictureUrl").toString().startsWith(u"file:"_s));
    QQuickItem *bg = s.window->findChild<QQuickItem *>(u"pagePicture"_s);
    REQUIRE(bg);
    CHECK(bg->isVisible());
    s.shot("39-pictures");
}

TEST_CASE("UI: brisket by the pound: the Weigh page asks how much", "[flow][ui][weight]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    s.c.activate(u"brisket"_s);
    QTest::qWait(60);
    CHECK(s.c.pageId() == u"weigh"_s);
    for (const QString &k : {u"4"_s, u"5"_s, u"8"_s})   // keys only the keypad has (Course has 1 2 3)
        s.tapKey(k);
    QTest::qWait(40);
    CHECK(Screen::findBy(s.window->contentItem(), "text", u"4.58 lb  ·  $100.76"_s));
    s.shot("36-weigh");
    s.c.activate(u"add"_s);
    QTest::qWait(60);
    CHECK(s.c.pageId() == u"items-burgers"_s);
    REQUIRE(s.pos.lines().size() == 1);
    CHECK(s.pos.lines()[0].toMap()[u"name"_s].toString().contains(u"4.58 lb"_s));
}

TEST_CASE("UI: the theme's status colors: a table with my check", "[flow][ui][statuscolors]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.enterEditMode();
    REQUIRE(s.c.editor()->setField(u"theme"_s, u"status.tableMine"_s, u"#7a1fa2"_s));
    REQUIRE(s.c.leaveEditMode(true));
    CHECK(s.c.statusColors().value(u"tableMine"_s) == u"#7a1fa2"_s);

    REQUIRE(s.pos.selectTable(u"T4"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    s.pos.addItem(u"cobb"_s);
    s.pos.releaseCheck();
    QTest::qWait(80);
    QQuickItem *t4 = Screen::findBy(s.window->contentItem(), "name", u"T4"_s);
    REQUIRE(t4);
    CHECK(t4->property("tint").value<QColor>() == QColor(u"#7a1fa2"_s));
}

TEST_CASE("UI: zones shown only when their rules hold; live text in labels", "[flow][ui][rules]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));               // a manager arranges the page
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.enterEditMode();
    EditorController *e = s.c.editor();
    const auto add = [&](const QString &label, const QString &rule, const QString &value) {
        const QString id = e->addZone(u"button"_s);
        e->selectOnly({id});
        REQUIRE(e->setField(u"zone"_s, u"label"_s, label));
        if (!rule.isEmpty())
            REQUIRE(e->setField(u"zone"_s, u"showWhen."_s + rule, value));
        return id;
    };
    const QString due = add(u"Due {check.balance} · {user.name}"_s, u"check"_s, u"open"_s);
    const QString boss = add(u"Boss Button"_s, u"login"_s, u"manager"_s);
    REQUIRE(e->setField(u"zone"_s, u"hotkey"_s, u"m"_s));
    const QString lunch = add(u"Lunch Special"_s, u"mealPeriod"_s, u"lunch"_s);
    const QString takeout = add(u"Takeout {check.label}"_s, u"checkType"_s, u"takeout"_s);
    REQUIRE(s.c.leaveEditMode(true));
    s.c.setMealPeriod(u"lunch"_s);
    QTest::qWait(60);

    const auto shown = [&](const QString &text) {
        return Screen::findBy(s.window->contentItem(), "text", text) != nullptr;
    };
    CHECK(shown(u"Boss Button"_s));
    CHECK(shown(u"Lunch Special"_s));
    CHECK_FALSE(shown(u"Due  · Morgan (Manager)"_s));   // no check open

    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"1111"_s));                // Sam, a server
    REQUIRE(s.c.jumpTo(u"tables"_s));
    QTest::qWait(60);
    CHECK_FALSE(shown(u"Boss Button"_s));
    CHECK_FALSE(s.c.triggerHotkey(u"m"_s));                 // hidden: its key does nothing either

    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    s.pos.addItem(u"cobb"_s);
    QTest::qWait(60);
    const QString balance = s.pos.totals()[u"balance"_s].toString();
    CHECK(shown(u"Due %1 · Sam"_s.arg(balance)));
    CHECK(shown(u"Takeout %1"_s.arg(s.pos.checkInfo()[u"label"_s].toString())));
    s.shot("33-rules");

    s.c.setMealPeriod(u"dinner"_s);
    QTest::qWait(30);
    CHECK_FALSE(shown(u"Lunch Special"_s));
    Q_UNUSED(due); Q_UNUSED(boss); Q_UNUSED(lunch); Q_UNUSED(takeout);
}

TEST_CASE("UI: a widget's own buttons hidden, renamed, restyled; their commands on buttons of your own",
          "[flow][ui][builtins]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.c.jumpTo(u"kitchen"_s));
    s.c.enterEditMode();
    EditorController *e = s.c.editor();
    REQUIRE(e);
    e->selectOnly({u"tickets"_s});
    REQUIRE(e->setField(u"zone"_s, u"props.buttons.recall.hide"_s, true));
    REQUIRE(e->setField(u"zone"_s, u"props.buttons.allDay.label"_s, u"Everything"_s));
    REQUIRE(e->setField(u"zone"_s, u"props.buttons.allDay.order"_s, 1));   // first in the row
    REQUIRE(e->setField(u"zone"_s, u"style.normal.keyFill"_s, u"#aa2200"_s));
    QTest::qWait(150);
    s.shot("32-inspector");
    // A button of our own, anywhere: it does what All Day does.
    const QString mine = e->addZone(u"button"_s);
    REQUIRE_FALSE(mine.isEmpty());
    e->selectOnly({mine});
    e->setActions({QVariantMap{{u"type"_s, u"command"_s}, {u"name"_s, u"kitchenAllDay"_s}}});
    REQUIRE(s.c.leaveEditMode(true));
    QTest::qWait(100);

    const auto find = [&](const char *name) {
        return Screen::findBy(s.window->contentItem(), "objectName", QString::fromLatin1(name));
    };
    CHECK_FALSE(find("kdsRecall"));                     // hidden (only shown items are found)
    QQuickItem *allDay = find("kdsAllDay");
    REQUIRE(allDay);
    CHECK(allDay->property("text").toString() == u"Everything"_s);
    QQuickItem *station = find("kdsStation");
    REQUIRE(station);
    CHECK(allDay->mapToScene({0, 0}).x() < station->mapToScene({0, 0}).x());
    CHECK(allDay->property("color").value<QColor>() == QColor(u"#aa2200"_s));
    s.shot("31-builtins");

    s.c.activate(mine);
    QTest::qWait(50);
    CHECK(find("kdsAllDay")->property("text").toString() == u"Hide All Day"_s);

    // Seats, courses and guests from buttons too.
    REQUIRE(s.pos.selectTable(u"T3"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    REQUIRE(s.c.jumpTo(u"index-lunch"_s));
    s.c.enterEditMode();
    e = s.c.editor();
    const auto button = [&](const char *command) {
        const QString id = e->addZone(u"button"_s);
        e->selectOnly({id});
        e->setActions({QVariantMap{{u"type"_s, u"command"_s}, {u"name"_s, QString::fromLatin1(command)}}});
        return id;
    };
    const QString seat = button("seatNext"), course = button("courseNext"), guests = button("guestsMore");
    REQUIRE(s.c.leaveEditMode(true));
    s.c.activate(seat);
    s.c.activate(seat);
    QTest::qWait(30);
    CHECK(s.pos.checkInfo()[u"seat"_s] == 2);
    s.c.activate(course);
    QTest::qWait(30);
    CHECK(s.pos.checkInfo()[u"course"_s] == 2);
    const int entered = s.pos.property("entryGuests").toInt();   // the guest count page's number
    s.c.activate(guests);
    QTest::qWait(30);
    CHECK(s.pos.property("entryGuests").toInt() == entered + 1);
}

TEST_CASE("UI: a kitchen screen picks its station; the fries show at the fryer", "[flow][ui][stations]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.pos.selectTable(u"T2"_s) == app::PosService::TableNeedsGuests);
    REQUIRE(s.pos.startCheck(core::CheckType::DineIn));
    s.pos.addItem(u"burger-combo"_s);
    s.pos.chooseOption(u"temperature"_s, 2);
    s.pos.chooseOption(u"side"_s, 0);
    s.pos.chooseOption(u"combo-drink"_s, 0);
    s.pos.finishChoosing();
    s.pos.addItem(u"caesar"_s);
    REQUIRE(s.pos.sendOrder());
    s.pos.releaseCheck();
    REQUIRE(s.c.jumpTo(u"kitchen"_s));
    QTest::qWait(150);
    const auto shows = [&](const QString &text) {
        return Screen::findBy(s.window->contentItem(), "text", text) != nullptr;
    };
    // Found again each time: a settings change rebuilds the page.
    const auto station = [&] {
        QQuickItem *k = Screen::findBy(s.window->contentItem(), "objectName", u"kdsStation"_s);
        REQUIRE(k);
        return k;
    };
    s.tapItem(station());                                   // Grill
    QTest::qWait(150);
    CHECK(s.pos.kitchenStation() == u"grill"_s);
    s.shot("29-station-grill");
    s.tapItem(station());                                   // Fryer
    QTest::qWait(150);
    CHECK(s.pos.kitchenStation() == u"fryer"_s);
    CHECK(shows(u"1  Fries"_s));
    CHECK_FALSE(shows(u"1  COMBO BGR"_s));
    s.shot("30-station-fryer");
    s.tapItem(station());                                   // Cold Line
    QTest::qWait(150);
    s.tapItem(station());                                   // back to the page's own: the kitchen
    QTest::qWait(150);
    CHECK(s.pos.kitchenStation().isEmpty());
    CHECK(shows(u"1  COMBO BGR"_s));
}

TEST_CASE("UI: a takeout ready later, picked by day, hour and minutes", "[flow][ui][later]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.activate(u"takeout"_s);
    CHECK(s.c.pageId() == u"index-lunch"_s);                  // straight to the menu
    QTest::qWait(50);
    const auto key = [&](const QString &name) {
        QQuickItem *k = Screen::findBy(s.window->contentItem(), "objectName", name);
        REQUIRE(k);
        return k;
    };
    s.tapItem(key(u"who-later"_s));                          // Ready Later… on the check
    CHECK(s.c.pageId() == u"order-later"_s);
    QTest::qWait(50);
    s.tapItem(key(u"laterDay-1"_s));      // tomorrow
    s.tapItem(key(u"laterHour-18"_s));    // 6 PM
    s.tapItem(key(u"laterMinute-30"_s));
    const QDateTime due = QDateTime::fromMSecsSinceEpoch(s.pos.checkInfo()[u"dueAt"_s].toLongLong());
    CHECK(due == QDateTime(QDate::currentDate().addDays(1), QTime(18, 30)));
    QTest::qWait(50);
    CHECK(key(u"laterDue"_s)->property("text").toString().contains(u"tomorrow"_s));
    s.shot("28-order-later");
    s.tapItem(key(u"laterAsap"_s));
    CHECK(s.pos.checkInfo()[u"dueAt"_s].toLongLong() == 0);
}

TEST_CASE("UI: opening a bar tab and finding it on the tabs screen", "[flow][ui][tabs]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"4444"_s));
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.activate(u"tabs"_s);
    CHECK(s.c.pageId() == u"tabs"_s);
    s.c.activate(u"new"_s);
    CHECK(s.c.pageId() == u"new-tab"_s);
    for (const char ch : {'M', 'i', 'k', 'e'})
        s.pos.textKey(QString(QChar(ch)));
    s.c.activate(u"open"_s);                          // Open Tab, then the menu
    CHECK(s.c.pageId() == u"index-lunch"_s);
    CHECK(s.pos.checkInfo()[u"label"_s] == u"Mike"_s);
    s.c.activate(u"flow-tables"_s);
    REQUIRE(s.pos.openTab(u"Ana"_s));
    s.pos.releaseCheck();
    REQUIRE(s.c.jumpTo(u"tabs"_s));
    QTest::qWait(60);
    s.shot("27-bar-tabs");
    QQuickItem *ana = Screen::findBy(s.window->contentItem(), "text", u"Ana"_s);
    REQUIRE(ana);
    s.tapItem(ana);
    CHECK(s.pos.checkInfo()[u"label"_s] == u"Ana"_s);
}

TEST_CASE("UI: the screen dims when untouched; the first touch only wakes it", "[flow][ui][saver]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.setScreenSaverForTesting(200);
    QTest::qWait(400);
    REQUIRE(s.c.asleep());
    QQuickItem *saver = Screen::findBy(s.window->contentItem(), "objectName", u"screenSaver"_s);
    REQUIRE(saver);
    CHECK(saver->isVisible());
    s.shot("26-screen-saver");

    // A touch on Quick Order while dim: awake, and no order started.
    s.c.setScreenSaverForTesting(60'000);
    s.tapItem(Screen::findBy(s.window->contentItem(), "text", u"Quick Order"_s));
    CHECK_FALSE(s.c.asleep());
    CHECK_FALSE(saver->isVisible());
    CHECK_FALSE(s.pos.hasCheck());
    CHECK(s.c.pageId() == u"tables"_s);
    s.tapItem(Screen::findBy(s.window->contentItem(), "text", u"Quick Order"_s));   // now it works
    CHECK(s.pos.hasCheck());

    // Kitchen screens stay on.
    REQUIRE(s.c.jumpTo(u"kitchen"_s));
    s.c.setScreenSaverForTesting(150);
    QTest::qWait(400);
    CHECK_FALSE(s.c.asleep());
}

TEST_CASE("UI: receiving a delivery by touch", "[flow][ui][vendors]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.c.jumpTo(u"receive-delivery"_s));
    QTest::qWait(80);
    auto find = [&](const QString &name) { return Screen::findBy(s.window->contentItem(), "objectName", name); };
    s.tapItem(find(u"vendor-city-bakery"_s));
    QTest::qWait(60);
    CHECK_FALSE(find(u"qty-lettuce"_s));                 // only City Bakery's items
    s.tapItem(find(u"qty-bun"_s));
    for (const char ch : {'2', '4'})
        QTest::keyClick(s.window, ch);
    s.tapItem(find(u"cost-bun"_s));
    for (const char ch : {'0', '.', '4', '2'})
        QTest::keyClick(s.window, ch);
    QTest::qWait(60);
    s.shot("25-receive-delivery");
    const double before = s.pos.shared()->ingredient("bun")->onHand;
    s.tapItem(find(u"receiveDelivery"_s));
    QTest::qWait(80);
    CHECK(s.pos.shared()->ingredient("bun")->onHand == before + 24);
    CHECK(s.pos.shared()->ingredient("bun")->cost.cents() == 42);
    CHECK(find(u"qty-bun"_s)->property("text").toString().isEmpty());   // ready for the next one
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

    // Jo bartends, and serves some nights: clocking in asks which.
    s.pos.logout();
    const auto pin = [&](const char *digits) {
        for (const char *d = digits; *d; ++d)
            s.pos.pinKey(QString(QChar(*d)));
    };
    pin("4444");
    s.pos.clockOut();
    pin("4444");
    REQUIRE(s.pos.clockIn());
    go("login", "m78-which-job");
    REQUIRE(s.pos.clockInAs(u"bartender"_s));
    REQUIRE(s.pos.loginWithPin(u"1111"_s));

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
    // A delivery received and a pay out for ice, for the Purchases and Expenses reports.
    REQUIRE(s.pos.receiveDelivery({{u"vendor"_s, u"valley-foods"_s}, {u"invoice"_s, u"VF-20931"_s},
                                   {u"lines"_s, QVariantList{
                                        QVariantMap{{u"ingredient"_s, u"patty"_s}, {u"qty"_s, 40}, {u"cost"_s, 1.65}},
                                        QVariantMap{{u"ingredient"_s, u"cheese"_s}, {u"qty"_s, 80}},
                                        QVariantMap{{u"ingredient"_s, u"eggs"_s}, {u"qty"_s, 90}}}}}));
    if (s.pos.drawerInfo()[u"open"_s].toBool()) {
        s.pos.entryKey(u"1800"_s);
        s.pos.setExpenseCategory(u"Ice"_s);
        s.pos.payout(core::CashMovement::Kind::Payout);
    }
    go("receive-delivery", "m76-receive-delivery");
    REQUIRE(s.pos.searchChecks(u"cobb"_s));
    for (int i = 0; i < 200 && s.pos.checkSearch()[u"loading"_s].toBool(); ++i)
        QTest::qWait(20);
    if (const QVariantList found = s.pos.checkSearch()[u"results"_s].toList(); !found.isEmpty())
        s.pos.selectFoundCheck(found.first().toMap()[u"id"_s].toLongLong());
    go("find-check", "m77-find-check");

    // A combo for tomorrow evening, two bar tabs, and the screen dimmed.
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    s.pos.addItem(u"burger-combo"_s);
    s.pos.chooseOption(u"temperature"_s, 2);
    s.pos.chooseOption(u"side"_s, 2);
    s.pos.chooseOption(u"combo-drink"_s, 2);
    go("modifiers", "m79-combo");
    s.pos.finishChoosing();
    REQUIRE(s.pos.setDueAt(QDateTime(QDate::currentDate().addDays(1), QTime(18, 30)).toMSecsSinceEpoch()));
    go("order-later", "m80-order-later");
    s.pos.sendOrder();
    s.pos.releaseCheck();
    for (const QString &who : {u"Mike"_s, u"Ana, red jacket"_s, u"Table of 4 by the TV"_s}) {
        REQUIRE(s.pos.openTab(who));
        s.pos.addItem(u"draft-beer"_s);
        chooseRequired();
        s.pos.sendOrder();
        s.pos.releaseCheck();
    }
    go("tabs", "m81-bar-tabs");
    go("tables", "m81-tables");
    REQUIRE(s.pos.setKitchenStation(u"fryer"_s));
    go("kitchen", "m83-station-fryer");
    REQUIRE(s.pos.setKitchenStation(QString()));
    {   // The kitchen screen's own buttons: red, Recall hidden, All Day renamed.
        s.c.enterEditMode();
        EditorController *e = s.c.editor();
        e->selectOnly({u"tickets"_s});
        e->setField(u"zone"_s, u"props.buttons.recall.hide"_s, true);
        e->setField(u"zone"_s, u"props.buttons.allDay.label"_s, u"Everything"_s);
        e->setField(u"zone"_s, u"style.normal.keyFill"_s, u"#aa2200"_s);
        QTest::qWait(150);
        snap("m84-builtins");
        REQUIRE(s.c.leaveEditMode(false));
    }
    s.c.setScreenSaverForTesting(100);
    QTest::qWait(300);
    snap("m82-screen-saver");
    s.c.setScreenSaverForTesting(3'600'000);
    s.c.wake();
    go("reports", "m46-report-sales");
    s.tapKey(u"Items"_s);
    s.tapKey(u"Last Month"_s);                                  // both years have the whole month
    s.tapKey(u"vs Last Year"_s);
    for (int i = 0; i < 200 && s.pos.rangeReport()[u"loading"_s].toBool(); ++i)
        QTest::qWait(20);
    QTest::qWait(80);
    snap("m47-report-month-vs-last-year");
    s.tapKey(u"Day"_s);
    for (const char *r : {"Labor", "Kitchen", "Food Cost", "Tips", "Gift Cards", "Expenses", "Purchases", "Exceptions", "Deposit", "Customers", "Royalty", "Accounting"}) {
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

    // Posted until tonight: it comes back for the next person who logs in.
    REQUIRE(kitchen.sendMessage(u"all"_s, u"Wine dinner Friday: sell tickets"_s,
                                QDateTime(QDate::currentDate(), QTime(23, 59)).toMSecsSinceEpoch()));
    QTest::qWait(50);
    CHECK(ok->isVisible());
    s.tapItem(ok);
    CHECK_FALSE(ok->isVisible());
    s.pos.logout();
    REQUIRE(s.pos.loginWithPin(u"4444"_s));
    REQUIRE(s.c.jumpTo(u"tables"_s));
    QTest::qWait(50);
    CHECK(ok->isVisible());
    s.tapItem(ok);

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

// Every page on a phone held upright, as screenshots (VTM_SHOTS): run by hand
// with [phoneaudit] to look them over.
TEST_CASE("Phone audit: every page in portrait", "[.][phoneaudit]")
{
    Screen s(false, 412, 870, u"phone"_s);
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Takeout));
    s.pos.addItem(u"cobb"_s);
    s.c.finishChoosing();
    s.pos.addItem(u"water"_s);
    QStringList ids;
    for (const layout::Page &p : s.c.layout().pages)
        if (p.formFactor != u"phone" && p.kind != u"template" && p.kind != u"library")
            ids << p.id;
    ids.sort();
    for (const QString &id : ids) {
        if (!s.c.jumpTo(id))
            continue;
        QTest::qWait(60);
        s.shot((u"phone-"_s + id).toUtf8().constData());
    }
    QTest::qWait(30);
}

TEST_CASE("Host stand screen: pick a party, push tables together, seat; bussing", "[ui][host]")
{
    for (const bool phone : {false, true}) {
        Screen s(false, phone ? 412 : 1600, phone ? 870 : 900, phone ? u"phone"_s : QString());
        REQUIRE(s.pos.loginWithPin(u"1234"_s));
        const qint64 lee = s.pos.addReservation({{u"name"_s, u"Lee"_s}, {u"size"_s, 8},
                                                 {u"at"_s, QDateTime::currentMSecsSinceEpoch() + 3'600'000}});
        REQUIRE(lee > 0);
        REQUIRE(s.pos.addToWaitlist({{u"name"_s, u"Kim"_s}, {u"size"_s, 2}}) > 0);
        REQUIRE(s.pos.setTableState(u"T3"_s, u"dirty"_s));
        REQUIRE(s.c.jumpTo(u"seating"_s));
        QTest::qWait(60);
        QQuickItem *root = s.window->contentItem();
        const auto by = [&](const QString &name) { return Screen::findBy(root, "objectName", name); };

        // Hold T5 + T6 for Lee.
        s.tapItem(by(u"hostBooked"_s));
        s.tapItem(by(u"hostParty-Lee"_s));
        s.tapItem(by(u"host-T5"_s));
        s.tapItem(by(u"host-T6"_s));
        s.shot(phone ? "host-phone-picked" : "host-picked");
        CHECK(by(u"hostHint"_s)->property("text").toString().startsWith(u"T5 + T6 for 8 guests"_s));
        s.tapItem(by(u"hostHold"_s));
        CHECK(s.pos.floor().value(u"T6"_s).toMap().value(u"state"_s) == u"reserved"_s);

        // Seat Kim at T1, and a walk-in of 3 at T2.
        s.tapItem(by(u"hostWaiting"_s));
        s.tapItem(by(u"hostParty-Kim"_s));
        s.tapItem(by(u"host-T1"_s));
        s.tapItem(by(u"hostSeat"_s));
        CHECK(s.pos.floor().value(u"T1"_s).toMap().value(u"state"_s) == u"seated"_s);
        s.tapItem(by(u"hostWalkIn"_s));
        s.tapItem(by(u"host-T2"_s));
        s.tapItem(by(u"hostSeat"_s));
        CHECK(s.pos.floor().value(u"T2"_s).toMap().value(u"guests"_s) == 2);

        // T3 bussed.
        s.tapItem(by(u"host-T3"_s));
        s.tapItem(by(u"hostClean"_s));
        CHECK_FALSE(s.pos.floor().contains(u"T3"_s));
        s.tapItem(by(u"host-T4"_s));
        s.tapItem(by(u"hostDirty"_s));
        CHECK(s.pos.floor().value(u"T4"_s).toMap().value(u"state"_s) == u"dirty"_s);
        QTest::qWait(30);
        s.shot(phone ? "host-phone" : "host");
        CHECK_FALSE(s.pos.hasCheck());   // the host doesn't keep the tables' checks
    }
}

TEST_CASE("Phone orders: name, phone and address right on the check", "[flow][ui][who]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    REQUIRE(s.pos.saveCustomer({{u"name"_s, u"Dana Ruiz"_s}, {u"phone"_s, u"555-0142"_s},
                                {u"address"_s, u"12 Oak St"_s}}));
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.activate(u"delivery"_s);
    CHECK(s.c.pageId() == u"index-lunch"_s);
    QTest::qWait(50);
    const auto key = [&](const QString &name) {
        QQuickItem *k = Screen::findBy(s.window->contentItem(), "objectName", name);
        REQUIRE(k);
        return k;
    };
    CHECK(key(u"who-name"_s)->property("text") == u"+ Name"_s);
    CHECK(key(u"who-address"_s)->property("text") == u"+ Address"_s);
    s.shot("who-1-empty");

    // Type a name.
    s.tapItem(key(u"who-name"_s));
    for (char ch : std::string("Sam"))
        QTest::keyClick(s.window, ch);
    s.shot("who-2-typing");
    s.tapItem(key(u"whoSave"_s));
    CHECK(s.pos.checkInfo()[u"customer"_s].toMap()[u"name"_s] == u"Sam"_s);
    CHECK(key(u"who-name"_s)->property("text") == u"Sam"_s);

    // A phone number that matches a regular: touch them, and everything fills in.
    s.tapItem(key(u"who-phone"_s));
    for (char ch : std::string("0142"))
        QTest::keyClick(s.window, ch);
    QTest::qWait(30);
    QQuickItem *regular = nullptr;
    for (const QVariant &v : s.pos.customerResults())
        if (v.toMap()[u"name"_s] == u"Dana Ruiz"_s)
            regular = key(u"regular-"_s + v.toMap()[u"id"_s].toString());
    s.shot("who-3-regular");
    s.tapItem(regular);
    const QVariantMap who = s.pos.checkInfo()[u"customer"_s].toMap();
    CHECK(who[u"name"_s] == u"Dana Ruiz"_s);
    CHECK(who[u"address"_s] == u"12 Oak St"_s);
    CHECK(key(u"who-address"_s)->property("text") == u"12 Oak St"_s);

    // Change just the address: Enter saves.
    s.tapItem(key(u"who-address"_s));
    QTest::keyClick(s.window, Qt::Key_A, Qt::ControlModifier);
    for (char ch : std::string("9 Elm Ave"))
        QTest::keyClick(s.window, ch);
    QTest::keyClick(s.window, Qt::Key_Return);
    CHECK(s.pos.checkInfo()[u"customer"_s].toMap()[u"address"_s] == u"9 Elm Ave"_s);
    CHECK(s.pos.checkInfo()[u"customer"_s].toMap()[u"phone"_s] == u"555-0142"_s);
    QTest::qWait(30);
    s.shot("who-4-done");
}

TEST_CASE("On-screen keyboard: on by default, over the screen, lifts a covered field, off by setting", "[ui][keyboard]")
{
    Session s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    QQmlApplicationEngine engine;
    // No --touch-keyboard: the terminal's setting decides (on when not set).
    engine.setInitialProperties({{u"controller"_s, QVariant::fromValue(&s.c)}, {u"width"_s, 1600}, {u"height"_s, 900}});
    engine.loadFromModule("ViewTouch", "Main");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    REQUIRE(window);
    REQUIRE(QTest::qWaitForWindowExposed(window));
    CHECK(window->property("touchKeyboard").toBool());
    QQuickItem *root = window->contentItem();
    const auto by = [&](QQuickItem *in, const QString &name) { return Screen::findBy(in, "objectName", name); };
    const auto tap = [&](QQuickItem *item) {
        REQUIRE(item);
        QTest::mouseClick(window, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        QTest::qWait(30);
    };
    // A key of the keyboard: its label's parent.
    const auto key = [&](QQuickItem *keyboard, const QString &label) {
        QQuickItem *text = Screen::findBy(keyboard, "text", label);
        REQUIRE(text);
        return text->parentItem();
    };

    // A takeout's name, typed on the screen's keyboard: over the page, which keeps its size.
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.activate(u"takeout"_s);
    QTest::qWait(50);
    QQuickItem *page = window->findChild<QQuickItem *>(u"pageSurface"_s);
    REQUIRE(page);
    const QSizeF pageSize(page->width() * page->scale(), page->height() * page->scale());
    tap(by(root, u"who-name"_s));
    QObject *popup = window->findChild<QObject *>(u"touchKeys"_s);
    REQUIRE(popup);
    CHECK(popup->property("visible").toBool());
    auto *docked = qvariant_cast<QQuickItem *>(popup->property("contentItem"));
    REQUIRE(docked);
    QTest::qWait(200);
    CHECK(QSizeF(page->width() * page->scale(), page->height() * page->scale()) == pageSize);
    CHECK(window->property("lift").toReal() == 0);                // the field is above the keyboard
    for (const char *k : {"S", "a", "m"})                         // a capital to start
        tap(key(docked, QString::fromLatin1(k)));
    CHECK(by(root, u"whoField"_s)->property("text") == u"Sam"_s);
    tap(key(docked, u"⏎"_s));                                    // Enter saves it
    CHECK(s.pos.checkInfo()[u"customer"_s].toMap()[u"name"_s] == u"Sam"_s);
    const auto shot = [&](const char *name) {
        if (const QByteArray dir = qgetenv("VTM_SHOTS"); !dir.isEmpty())
            window->grabWindow().save(QString::fromLocal8Bit(dir) + u'/' + QString::fromLatin1(name) + u".png"_s);
    };

    // A phone number: the number pad.
    tap(by(root, u"who-phone"_s));
    QTest::qWait(30);
    CHECK(Screen::findBy(docked, "text", u"q"_s) == nullptr);
    for (const char *k : {"5", "5", "5"})
        tap(key(docked, QString::fromLatin1(k)));
    CHECK(by(root, u"whoField"_s)->property("text") == u"555"_s);
    shot("kb-1-numbers");
    tap(by(root, u"whoSave"_s));

    // A field low on the screen: the page slides up to keep it in view, and back after.
    REQUIRE(s.c.jumpTo(u"customer"_s));
    QTest::qWait(80);
    QQuickItem *lowest = nullptr;
    std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        if (!item->isVisible())
            return;
        if ((item->inherits("QQuickTextField") || item->inherits("QQuickTextArea"))
            && (!lowest || item->mapToScene(QPointF(0, 0)).y() > lowest->mapToScene(QPointF(0, 0)).y()))
            lowest = item;
        for (QQuickItem *child : item->childItems())
            walk(child);
    };
    walk(page);
    REQUIRE(lowest);
    tap(lowest);
    QTest::qWait(300);
    CHECK(window->property("lift").toReal() > 0);
    const qreal keysTop = window->height() - docked->height();
    CHECK(lowest->mapToScene(QPointF(0, lowest->height())).y() <= keysTop);
    shot("kb-0-lifted");
    tap(key(docked, u"⌨▾"_s));                                   // put away: the page comes back
    QTest::qWait(300);
    CHECK(window->property("lift").toReal() == 0);
    s.pos.releaseCheck();

    // The setup guide covers the page: the keyboard floats over it.
    s.c.openSetup();
    QTest::qWait(80);
    tap(by(root, u"setupNext"_s));                                // past the welcome
    QTest::qWait(80);
    QQuickItem *storeName = by(root, u"setupStoreName"_s);
    REQUIRE(storeName);
    tap(storeName);
    CHECK(popup->property("visible").toBool());
    auto *keys = qvariant_cast<QQuickItem *>(popup->property("contentItem"));
    REQUIRE(keys);
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    for (const char *k : {"b", "o", "b"})
        tap(key(keys, QString::fromLatin1(k)));
    CHECK(storeName->property("text").toString().endsWith(u"bob"_s));
    shot("kb-2-setup");

    // Off for this terminal (it has a keyboard): none shows.
    REQUIRE(s.pos.adminSave(u"terminals"_s, -1, {{u"name"_s, s.pos.terminalName()}, {u"keyboard"_s, u"off"_s}}));
    QTest::qWait(30);
    CHECK_FALSE(window->property("touchKeyboard").toBool());
    CHECK_FALSE(popup->property("visible").toBool());
}

TEST_CASE("Order screen: swipe a line, same as last time, deliveries board", "[ui][swipe][phoneorders]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    s.pos.entryKey(u"10000"_s);
    REQUIRE(s.pos.openDrawerSession());
    QQuickItem *root = s.window->contentItem();
    const auto by = [&](const QString &name) { return Screen::findBy(root, "objectName", name); };
    // Drag across a line by dx (a swipe).
    const auto swipe = [&](const QString &name, qreal dx) {
        QQuickItem *text = Screen::findBy(root, "text", name);
        REQUIRE(text);
        const QPoint from = text->mapToScene(QPointF(text->width() / 2, text->height() / 2)).toPoint();
        QTest::mousePress(s.window, Qt::LeftButton, {}, from);
        for (int i = 1; i <= 10; ++i) {
            QTest::mouseMove(s.window, from + QPoint(int(dx * i / 10), 0));
            QTest::qWait(10);
        }
        if (dx < 0)
            s.shot("phone-0-swipe");
        QTest::mouseRelease(s.window, Qt::LeftButton, {}, from + QPoint(int(dx), 0));
        QTest::qWait(50);
    };

    // A regular's takeout.
    REQUIRE(s.pos.saveCustomer({{u"name"_s, u"Dana Ruiz"_s}, {u"phone"_s, u"555-0142"_s}}));
    QString dana;
    for (const core::CustomerRecord &c : s.pos.shared()->customers)
        dana = QString::fromStdString(c.id);
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.activate(u"takeout"_s);
    REQUIRE(s.pos.useCustomer(dana));
    s.pos.addItem(u"coffee"_s);
    s.pos.addItem(u"water"_s);
    QTest::qWait(50);
    CHECK(by(u"readyQuote"_s)->property("text").toString().startsWith(u"Ready in about"_s));

    // Swipe right: one more. Swipe left: off.
    swipe(u"Coffee"_s, 300);
    CHECK(s.pos.lines()[0].toMap()[u"quantity"_s] == 2);
    swipe(u"Water"_s, -160);
    CHECK(s.pos.lines().size() == 1);
    swipe(u"2 × Coffee"_s, 40);                                  // a short drag does nothing
    CHECK(s.pos.lines()[0].toMap()[u"quantity"_s] == 2);
    REQUIRE(s.pos.sendOrder());
    REQUIRE(s.pos.tender(u"cash"_s));
    REQUIRE(s.pos.closeCheck());

    // Next time: Same as Last Time.
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.activate(u"takeout"_s);
    REQUIRE(s.pos.useCustomer(dana));
    QTest::qWait(50);
    QQuickItem *same = by(u"sameAsLastTime"_s);
    REQUIRE(same);
    CHECK(same->property("text") == u"Same as Last Time: 2 × Coffee"_s);
    s.shot("phone-1-same");
    s.tapItem(same);
    CHECK(s.pos.lines().size() == 1);
    CHECK_FALSE(by(u"sameAsLastTime"_s));                       // gone once there's an order
    s.pos.releaseCheck();

    // Deliveries: one sent, one not.
    QVariantMap e = s.pos.adminNewRecord(u"employees"_s);
    e[u"name"_s] = u"Lou"_s;
    e[u"role"_s] = u"driver"_s;
    e[u"pin"_s] = u"7777"_s;
    REQUIRE(s.pos.adminSave(u"employees"_s, -1, e));
    for (const auto &[name, address] : {std::pair{u"Ana"_s, u"12 Oak St"_s}, std::pair{u"Bo"_s, u"3 Pine Ave, apt 2"_s}}) {
        REQUIRE(s.pos.startCheck(core::CheckType::Delivery));
        s.pos.addItem(u"coffee"_s);
        REQUIRE(s.pos.setCustomer({{u"name"_s, name}, {u"address"_s, address}}));
        if (name == u"Ana"_s)
            REQUIRE(s.pos.sendOrder());
        s.pos.releaseCheck();
    }
    REQUIRE(s.c.jumpTo(u"deliveries"_s));
    QTest::qWait(80);
    const auto row = [&](const QString &name) {
        for (const QVariant &v : s.pos.deliveries())
            if (v.toMap()[u"name"_s] == name)
                return v.toMap();
        return QVariantMap();
    };
    const qint64 ana = row(u"Ana"_s)[u"id"_s].toLongLong();
    s.tapItem(by(u"delivery-"_s + QString::number(ana)));
    s.tapItem(by(u"driver-Lou"_s));
    s.shot("phone-2-deliveries");
    s.tapItem(by(u"deliverySendOut"_s));
    CHECK(row(u"Ana"_s)[u"state"_s] == u"out"_s);
    CHECK(row(u"Bo"_s)[u"state"_s] == u"new"_s);
    s.tapItem(by(u"delivery-"_s + QString::number(ana)));
    s.tapItem(by(u"deliveryBack"_s));
    CHECK(row(u"Ana"_s)[u"state"_s] == u"back"_s);
    QTest::qWait(50);
    s.shot("phone-3-back");
    s.tapItem(by(u"delivery-"_s + QString::number(ana)));
    s.tapItem(by(u"deliveryOpen"_s));
    QTest::qWait(50);
    CHECK(s.pos.checkInfo()[u"id"_s].toLongLong() == ana);
}

TEST_CASE("Qualifiers: No opens the item's choices; hold a choice for Lite / Extra / Side", "[ui][qualifiers]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1111"_s));
    REQUIRE(s.pos.startCheck(core::CheckType::Quick));
    REQUIRE(s.c.jumpTo(u"items-burgers"_s));
    s.c.orderItem(u"classic-burger"_s);
    QTest::qWait(80);
    REQUIRE(s.pos.chooseOption(u"temperature"_s, 2));
    REQUIRE(s.pos.chooseOption(u"side"_s, 0));
    s.c.finishChoosing();
    QTest::qWait(80);
    const QString menuPage = s.c.pageId();
    CHECK(menuPage == u"items-burgers"_s);
    QQuickItem *root = s.window->contentItem();
    const auto by = [&](const QString &name) { return Screen::findBy(root, "objectName", name); };

    // No: the burger's choices open, waiting for what to leave off.
    s.c.activate(u"flow-no"_s);
    QTest::qWait(120);
    CHECK(s.pos.pendingQualifier() == u"no"_s);
    CHECK(s.c.pageId() != menuPage);
    QQuickItem *onion = by(u"option-Onion"_s);
    REQUIRE(onion);
    s.tapItem(onion);
    QTest::qWait(50);
    CHECK(by(u"option-Onion"_s)->property("text").toString().startsWith(u"No Onion"_s));

    // Held: how to have it.
    QQuickItem *bacon = by(u"option-Tomato"_s);   // in view, beside the onion
    REQUIRE(bacon);
    const QPoint at = bacon->mapToScene(QPointF(bacon->width() / 2, bacon->height() / 2)).toPoint();
    QTest::mousePress(s.window, Qt::LeftButton, {}, at);
    QTest::qWait(700);
    QTest::mouseRelease(s.window, Qt::LeftButton, {}, at);
    QTest::qWait(50);
    REQUIRE(by(u"qualifierSheet"_s));
    s.shot("qual-1-hold");
    s.tapItem(by(u"how-extra"_s));
    QTest::qWait(50);
    CHECK_FALSE(by(u"qualifierSheet"_s));
    CHECK(by(u"option-Tomato"_s)->property("text").toString().startsWith(u"Extra Tomato"_s));
    s.shot("qual-2-chosen");
    QStringList mods;
    for (const QVariant &m : s.pos.lines().last().toMap()[u"modifiers"_s].toList())
        mods << m.toMap()[u"name"_s].toString();
    CHECK(mods.contains(u"No Onion"_s));
    CHECK(mods.contains(u"Extra Tomato"_s));
}

TEST_CASE("Phone audit: typing on a phone", "[.][phonekeys]")
{
    Screen s(true, 412, 870, u"phone"_s);
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    s.shot("pk-0-login");
    REQUIRE(s.c.jumpTo(u"tables"_s));
    s.c.activate(u"takeout"_s);
    QTest::qWait(80);
    QQuickItem *root = s.window->contentItem();
    s.tapItem(Screen::findBy(root, "objectName", u"who-name"_s));
    QTest::qWait(300);
    s.shot("pk-1-name");
    s.tapItem(Screen::findBy(root, "objectName", u"whoSave"_s));
    s.tapItem(Screen::findBy(root, "objectName", u"who-phone"_s));
    QTest::qWait(300);
    s.shot("pk-2-phone");
    s.tapItem(Screen::findBy(root, "objectName", u"whoSave"_s));
    s.pos.releaseCheck();
    REQUIRE(s.c.jumpTo(u"customer"_s));
    QTest::qWait(80);
    QQuickItem *lowest = nullptr;
    std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        if (!item->isVisible())
            return;
        if ((item->inherits("QQuickTextField") || item->inherits("QQuickTextArea"))
            && (!lowest || item->mapToScene(QPointF(0, 0)).y() > lowest->mapToScene(QPointF(0, 0)).y()))
            lowest = item;
        for (QQuickItem *child : item->childItems())
            walk(child);
    };
    walk(root);
    if (lowest) {
        s.tapItem(lowest);
        QTest::qWait(300);
        s.shot("pk-3-lifted");
    }
}

TEST_CASE("Card reader (simulated): Pay with a card, declined, canceled", "[ui][cards]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    s.pos.entryKey(u"10000"_s);
    REQUIRE(s.pos.openDrawerSession());
    // This screen has a (simulated) card reader.
    REQUIRE(s.pos.adminSave(u"terminals"_s, -1, {{u"name"_s, s.pos.terminalName()}, {u"cardReader"_s, u"simulated"_s}}));
    CardReader *reader = s.c.cardReader();
    CHECK(reader->kind() == u"simulated"_s);
    reader->setSimulatedDelay(300);
    QQuickItem *root = s.window->contentItem();
    const auto by = [&](const QString &name) { return Screen::findBy(root, "objectName", name); };

    REQUIRE(s.pos.startCheck(core::CheckType::Quick));
    REQUIRE(s.pos.addItem(u"coffee"_s));                       // $2.98
    REQUIRE(s.c.jumpTo(u"settle"_s));
    QTest::qWait(50);

    // Declined: cents ending in 05.
    for (const char *k : {"1", "0", "5"})
        s.pos.entryKey(QString::fromLatin1(k));
    s.c.activate(u"tender-credit"_s);
    QTest::qWait(60);
    REQUIRE(by(u"cardWait"_s));
    s.shot("card-1-waiting");
    QTest::qWait(500);
    CHECK_FALSE(by(u"cardWait"_s));
    CHECK(s.pos.payments().isEmpty());
    CHECK(s.c.statusText().contains(u"declined"_s));

    // Canceled.
    s.c.activate(u"tender-credit"_s);
    QTest::qWait(60);
    s.tapItem(by(u"cardCancel"_s));
    QTest::qWait(50);
    CHECK_FALSE(by(u"cardWait"_s));
    CHECK(s.pos.payments().isEmpty());

    // Approved: the whole balance, on the check with the card.
    s.pos.entryKey(u"clear"_s);                                // the $1.05 stays typed after a decline
    s.c.activate(u"tender-credit"_s);
    QTest::qWait(500);
    REQUIRE(s.pos.payments().size() == 1);
    CHECK(s.pos.payments()[0].toMap()[u"name"_s].toString().contains(u"Visa •••• 4242"_s));
    CHECK(s.pos.totals()[u"balanceCents"_s].toLongLong() == 0);
    s.shot("card-2-approved");

    // No reader: Credit Card is typed in as before.
    REQUIRE(s.pos.adminSave(u"terminals"_s, 0, {{u"name"_s, s.pos.terminalName()}, {u"cardReader"_s, u""_s}}));
    CHECK(reader->kind().isEmpty());
}

TEST_CASE("Countertop reader: Pay, the reader waits, a test card is tapped", "[ui][cards][counter]")
{
    Screen s;
    REQUIRE(s.pos.loginWithPin(u"1234"_s));
    s.pos.entryKey(u"10000"_s);
    REQUIRE(s.pos.openDrawerSession());
    test::FakeStripe stripe;
    stripe.install(s.pos);
    s.pos.shared()->settings.stripeSecretKey = "sk_test_x";
    s.pos.shared()->settings.stripeReaders.push_back({"tmr_1", "Front counter", "simulated_wisepos_e"});
    REQUIRE(s.pos.adminSave(u"terminals"_s, -1, {{u"name"_s, s.pos.terminalName()}, {u"cardReader"_s, u"counter:tmr_1"_s}}));
    CHECK(s.c.cardReader()->kind() == u"counter"_s);
    QQuickItem *root = s.window->contentItem();
    const auto by = [&](const QString &name) { return Screen::findBy(root, "objectName", name); };

    REQUIRE(s.pos.startCheck(core::CheckType::Quick));
    REQUIRE(s.pos.addItem(u"coffee"_s));
    REQUIRE(s.c.jumpTo(u"settle"_s));
    QTest::qWait(50);
    s.c.activate(u"tender-credit"_s);
    QTest::qWait(80);
    REQUIRE(by(u"cardWait"_s));
    REQUIRE(by(u"testCardApprove"_s));                      // Stripe test mode
    s.shot("counter-1-waiting");
    s.tapItem(by(u"testCardApprove"_s));
    test::waitFor([&] { return !s.pos.payments().isEmpty(); });
    QTest::qWait(60);
    CHECK_FALSE(by(u"cardWait"_s));
    REQUIRE(s.pos.payments().size() == 1);
    CHECK(s.pos.payments()[0].toMap()[u"name"_s].toString().contains(u"Mastercard •••• 4444"_s));
    s.shot("counter-2-paid");
}
