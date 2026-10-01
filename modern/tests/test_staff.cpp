#include <catch2/catch_test_macros.hpp>

#include "app/pos_json.hh"
#include "pos_fixture.hh"
#include "qt_catch.hh"

#include <QDateTime>

using namespace Qt::StringLiterals;
using namespace vt;
using vt::app::PosService;

// Staff: permissions per person, breaks, overtime.

namespace {

int indexOf(PosService &pos, const QString &name)
{
    const QVariantList staff = pos.adminRecords(u"employees"_s);
    for (int i = 0; i < staff.size(); ++i) {
        if (staff[i].toMap()[u"name"_s].toString().startsWith(name))
            return i;
    }
    return -1;
}

} // namespace

TEST_CASE("Permissions per person: yes or no over the role", "[staff][permissions]")
{
    PosService pos(test::seedPosData(), nullptr);
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const int sam = indexOf(pos, u"Sam"_s);
    REQUIRE(sam >= 0);
    QVariantMap r = pos.adminRecords(u"employees"_s)[sam].toMap();
    CHECK(r[u"perm:order.void"_s].toString().isEmpty());       // as the role
    r[u"perm:order.void"_s] = u"allow"_s;                       // Sam may void sent items
    r[u"perm:check.discount"_s] = u"deny"_s;                    // but not give discounts
    REQUIRE(pos.adminSave(u"employees"_s, sam, r));
    const core::Employee &e = pos.shared()->employees[sam];
    CHECK(e.can(core::perm::Void));
    CHECK_FALSE(e.can(core::perm::Discount));
    CHECK(e.can(core::perm::Order));                           // the rest as a server
    CHECK(pos.adminRecords(u"employees"_s)[sam].toMap()[u"perm:order.void"_s] == u"allow"_s);
    pos.logout();

    REQUIRE(pos.loginWithPin(u"1111"_s));
    CHECK(pos.permissions().contains(u"order.void"_s));
    REQUIRE(pos.selectTable(u"T1"_s) == PosService::TableNeedsGuests);
    REQUIRE(pos.startCheck(core::CheckType::DineIn));
    pos.addItem(u"coffee"_s);
    REQUIRE(pos.sendOrder());
    CHECK(pos.voidItem());                                     // allowed for Sam
    pos.addItem(u"tea"_s);
    CHECK_FALSE(pos.tender(u"discount"_s));                    // denied for Sam
    CHECK(pos.tender(u"cash"_s, 100));                         // payments still fine
    pos.logout();

    // A manager can't take their own manager rights away.
    REQUIRE(pos.loginWithPin(u"1234"_s));
    const int morgan = indexOf(pos, u"Morgan"_s);
    QVariantMap me = pos.adminRecords(u"employees"_s)[morgan].toMap();
    me[u"perm:manager"_s] = u"deny"_s;
    CHECK_FALSE(pos.adminSave(u"employees"_s, morgan, me));

    // Saved with the employee.
    CHECK(app::employeeFromJson(app::toJson(e)) == e);
}
