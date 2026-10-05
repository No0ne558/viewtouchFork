#include <catch2/catch_test_macros.hpp>

#include "app/i18n.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QCoreApplication>
#include <QSignalSpy>

using namespace Qt::StringLiterals;
using namespace vt;

// Languages: each person's screens in their language; guests get the store's.

namespace {

// Back to English after each test: the screen's language is app-wide.
struct Spanish {
    Spanish() { i18n::install(); }
    ~Spanish()
    {
        i18n::setLanguage(u"en"_s);
        i18n::setGuestLanguage(u"en"_s);
    }
};

QString tr(const char *text, int n = -1)
{
    return QCoreApplication::translate("Test", text, nullptr, n);
}

} // namespace

TEST_CASE("Languages: phrases, plurals, and English when there is none", "[i18n]")
{
    Spanish es;
    CHECK(tr("Send") == u"Send"_s);   // English until asked
    CHECK(tr("%n guest(s)", 1) == u"1 guest"_s);   // English plurals
    CHECK(tr("%n guest(s)", 3) == u"3 guests"_s);
    REQUIRE(i18n::setLanguage(u"es"_s));
    CHECK_FALSE(i18n::setLanguage(u"es"_s));   // no change
    CHECK(tr("Send") == u"Enviar"_s);
    CHECK(tr("Welcome, %1").arg(u"Rosa"_s) == u"Bienvenido, Rosa"_s);
    CHECK(tr("It took over from another main server %n time(s).", 1) == u"Tomó el control de otro servidor principal 1 vez."_s);
    CHECK(tr("It took over from another main server %n time(s).", 3) == u"Tomó el control de otro servidor principal 3 veces."_s);
    CHECK(tr("Not a phrase anyone wrote") == u"Not a phrase anyone wrote"_s);

    // Guests' screens (the customer display): the store's language.
    i18n::setGuestLanguage(u"en"_s);
    CHECK(QCoreApplication::translate("CustomerDisplay", "Thank you!") == u"Thank you!"_s);
    i18n::setGuestLanguage(u"es"_s);
    CHECK(QCoreApplication::translate("CustomerDisplay", "Thank you!") == u"¡Gracias!"_s);

    // A while in another language (a terminal's operation on a server).
    {
        const i18n::Scope english(u"en"_s);
        CHECK(tr("Send") == u"Send"_s);
    }
    CHECK(tr("Send") == u"Enviar"_s);
}

TEST_CASE("Languages: each person's own, else the store's", "[i18n]")
{
    Spanish es;
    app::PosService pos(test::seedPosData(), nullptr);
    CHECK(pos.language() == u"en"_s);   // the store's, nobody logged in
    QSignalSpy notices(&pos, &app::PosSession::notice);
    pos.invoke(u"loginWithPin"_s, {u"5555"_s});   // Rosa
    CHECK(pos.language() == u"es"_s);
    REQUIRE_FALSE(notices.isEmpty());
    CHECK(notices.last()[0].toString() == u"Bienvenido, Rosa"_s);   // already in hers
    pos.invoke(u"logout"_s);
    CHECK(pos.language() == u"en"_s);

    // The store in Spanish: everyone without their own.
    QVariantMap store = pos.adminRecords(u"store"_s).value(0).toMap();
    pos.invoke(u"loginWithPin"_s, {u"1234"_s});
    store[u"language"_s] = u"es"_s;
    QVariant saved;
    pos.invoke(u"adminSave"_s, {u"store"_s, 0, store}, [&](const QVariant &r) { saved = r; });
    CHECK(pos.shared()->settings.language == "es");
    CHECK(pos.language() == u"es"_s);
    store[u"language"_s] = u"fr"_s;   // not one we have
    pos.invoke(u"adminSave"_s, {u"store"_s, 0, store});
    CHECK(pos.shared()->settings.language == "es");
}

TEST_CASE("Languages: two terminals on one server, each in its own", "[i18n]")
{
    Spanish es;
    app::PosShared shared(test::seedPosData(), nullptr);
    app::PosService bar(&shared, u"Bar"_s);
    app::PosService patio(&shared, u"Patio"_s);
    QSignalSpy barNotices(&bar, &app::PosSession::notice);
    QSignalSpy patioNotices(&patio, &app::PosSession::notice);
    bar.invoke(u"loginWithPin"_s, {u"5555"_s});     // Rosa: Spanish
    patio.invoke(u"loginWithPin"_s, {u"1111"_s});   // Sam: the store's (English)
    CHECK(barNotices.last()[0].toString() == u"Bienvenido, Rosa"_s);
    CHECK(patioNotices.last()[0].toString() == u"Welcome, Sam"_s);
    // What the server sends each terminal is in its language too.
    CHECK(bar.snapshot()[u"language"_s] == u"es"_s);
    CHECK(patio.snapshot()[u"language"_s] == u"en"_s);
    const QVariantList barFields = bar.snapshot()[u"messages"_s].toList();
    Q_UNUSED(barFields)
    bar.invoke(u"sendMessage"_s, {u"everyone"_s, QString()});
    CHECK(barNotices.last()[0].toString() == u"Escriba el mensaje."_s);
    patio.invoke(u"sendMessage"_s, {u"everyone"_s, QString()});
    CHECK(patioNotices.last()[0].toString() == u"Type the message."_s);
}
