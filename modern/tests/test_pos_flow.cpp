#include <catch2/catch_test_macros.hpp>

#include "layoutcontroller.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
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
