#pragma once

// Shared helpers for POS tests: the shipped seed data and a recording sink.

#include "app/pos_json.hh"
#include "app/pos_service.hh"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonDocument>

#include <map>

namespace vt::test {

inline QJsonDocument readSeed(const char *relative)
{
    QFile f(QStringLiteral(VTM_SEED_DIR "/") + QString::fromLatin1(relative));
    REQUIRE(f.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(f.readAll());
}

inline app::PosData seedPosData()
{
    app::PosData d;
    d.settings = app::settingsFromJson(readSeed("pos/settings.json").object());
    d.menu = app::menuFromJson(readSeed("pos/menu.json").array());
    d.employees = app::employeesFromJson(readSeed("pos/employees.json").array());
    // The drawer tests were written for a cash drawer per terminal; server
    // banks (the starter setting) have tests of their own.
    d.settings.cashMode = core::CashMode::TerminalDrawer;
    // Prices must not depend on the time the tests run: meal-period prices
    // have tests of their own, with a fixed clock.
    for (core::MenuItem &m : d.menu)
        m.periodPrices.clear();
    return d;
}

struct RecordingSink : app::PosSink {
    std::map<std::int64_t, core::Check> checks;
    std::map<std::int64_t, core::TimePunch> punches;
    int checkSaves = 0;

    void saveCheck(const core::Check &c) override
    {
        checks[c.id] = c;
        ++checkSaves;
    }
    void savePunch(const core::TimePunch &p) override { punches[p.id] = p; }
};

} // namespace vt::test
