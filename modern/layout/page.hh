#pragma once

#include "layout/style.hh"
#include "layout/zone.hh"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSize>
#include <QString>

namespace vt::layout {

// A screen. Pages are authored on a logical canvas that is scaled uniformly
// to the physical display, so one page serves every resolution.
struct Page {
    QString id;
    QString name;
    // login|tables|guestCount|index|items|modifier|settle|logout|manager|
    // kitchen|template|library|custom
    QString kind = QStringLiteral("custom");
    // Well-known entry point the app navigates to by name (replaces the
    // legacy negative page IDs such as -1 login, -20 settle).
    QString role;
    // Zones of the template page (and its templates) are drawn behind this
    // page's own zones, like the legacy parent pages -94..-99.
    QString templateId;
    QString mealPeriod;
    QSize canvas{1920, 1080};
    int grid = 8;
    QJsonObject background;   // { fill, texture }; missing keys inherit
    Style style;              // page-level zone defaults
    QList<Zone> zones;        // bottom to top
    QJsonObject extra;

    const Zone *zone(const QString &zoneId) const;

    static Page fromJson(const QJsonObject &o);
    QJsonObject toJson() const;

    bool operator==(const Page &) const = default;
};

struct Theme {
    QString name;
    QJsonObject background;
    Style style;                  // defaults for every zone
    QMap<QString, Style> kinds;   // per-kind overrides, e.g. "label"
    QJsonObject extra;

    static Theme fromJson(const QJsonObject &o);
    QJsonObject toJson() const;

    bool operator==(const Theme &) const = default;
};

} // namespace vt::layout
