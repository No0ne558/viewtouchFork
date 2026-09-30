#pragma once

#include "core/check.hh"
#include "core/employee.hh"
#include "core/day.hh"
#include "core/menu.hh"
#include "core/report.hh"
#include "core/settings.hh"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

// JSON (de)serialization for the POS domain. The core stays free of Qt; this
// is the boundary where it meets storage and seed files.
//
// Money in stored checks is integer cents. Human-edited files (menu,
// settings) use decimal amounts ("price": 12.50, "food": 8.25 percent) and
// are converted with exact rounding.
namespace vt::app {

inline constexpr int PosSchemaVersion = 1;

QJsonObject toJson(const core::Check &check);
std::optional<core::Check> checkFromJson(const QJsonObject &o);

QJsonObject toJson(const core::MenuItem &item);
core::MenuItem menuItemFromJson(const QJsonObject &o);
std::vector<core::MenuItem> menuFromJson(const QJsonArray &a);

// Seed files may carry a plain "pin"; it is hashed here and never kept.
QJsonObject toJson(const core::Employee &e);
core::Employee employeeFromJson(const QJsonObject &o);
std::vector<core::Employee> employeesFromJson(const QJsonArray &a);

QJsonObject toJson(const core::TimePunch &p);
core::TimePunch punchFromJson(const QJsonObject &o);

QJsonObject toJson(const core::Report &r);
core::Report reportFromJson(const QJsonObject &o);

QJsonObject toJson(const core::DrawerSession &d);
core::DrawerSession drawerFromJson(const QJsonObject &o);

QJsonObject toJson(const core::PrinterConfig &p);
core::PrinterConfig printerFromJson(const QJsonObject &o);

QJsonObject toJson(const core::PosSettings &s);
core::PosSettings settingsFromJson(const QJsonObject &o);

// "HH:MM" <-> minutes after midnight; clockMinutes gives -1 for bad text.
QString clockText(int minutes);
int clockMinutes(const QString &text);

// Salted SHA-256 of a PIN, hex encoded.
std::string hashPin(const QString &pin, const std::string &salt);
std::string newSalt();

// "12.50" -> 1250 cents; percent 8.25 -> 82500 ppm. Exact for inputs with
// at most 2 (money) / 4 (percent) decimals.
std::int64_t centsFromDecimal(double value);
std::int64_t ppmFromPercent(double percent);
double decimalFromCents(std::int64_t cents);
double percentFromPpm(std::int64_t ppm);

QString qs(const std::string &s);
std::string ss(const QString &s);

} // namespace vt::app
