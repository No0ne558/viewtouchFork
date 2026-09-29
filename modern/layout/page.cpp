#include "layout/page.hh"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace vt::layout {

namespace {
const QStringList kPageKeys = {
    u"id"_s, u"name"_s, u"kind"_s, u"role"_s, u"templateId"_s, u"mealPeriod"_s, u"permission"_s,
    u"canvas"_s, u"grid"_s, u"background"_s, u"style"_s, u"zones"_s,
    u"schemaVersion"_s,
};
const QStringList kThemeKeys = {u"name"_s, u"background"_s, u"style"_s, u"kinds"_s,
                                u"schemaVersion"_s};
} // namespace

const Zone *Page::zone(const QString &zoneId) const
{
    for (const Zone &z : zones) {
        if (z.id == zoneId)
            return &z;
    }
    return nullptr;
}

Page Page::fromJson(const QJsonObject &o)
{
    Page p;
    p.id = o.value(u"id").toString();
    p.name = o.value(u"name").toString();
    p.kind = o.value(u"kind").toString(p.kind);
    p.role = o.value(u"role").toString();
    p.templateId = o.value(u"templateId").toString();
    p.mealPeriod = o.value(u"mealPeriod").toString();
    p.permission = o.value(u"permission").toString();
    const QJsonObject c = o.value(u"canvas").toObject();
    p.canvas = QSize(c.value(u"w").toInt(p.canvas.width()), c.value(u"h").toInt(p.canvas.height()));
    p.grid = o.value(u"grid").toInt(p.grid);
    p.background = o.value(u"background").toObject();
    p.style = Style::fromJson(o.value(u"style").toObject());
    for (const QJsonValue &z : o.value(u"zones").toArray())
        p.zones.append(Zone::fromJson(z.toObject()));

    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!kPageKeys.contains(it.key()))
            p.extra.insert(it.key(), it.value());
    }
    return p;
}

QJsonObject Page::toJson() const
{
    QJsonObject o = extra;
    o.insert(u"id", id);
    o.insert(u"name", name);
    o.insert(u"kind", kind);
    if (!role.isEmpty()) o.insert(u"role", role);
    if (!templateId.isEmpty()) o.insert(u"templateId", templateId);
    if (!mealPeriod.isEmpty()) o.insert(u"mealPeriod", mealPeriod);
    if (!permission.isEmpty()) o.insert(u"permission", permission);
    o.insert(u"canvas", QJsonObject{{u"w"_s, canvas.width()}, {u"h"_s, canvas.height()}});
    o.insert(u"grid", grid);
    if (!background.isEmpty()) o.insert(u"background", background);
    if (!style.isEmpty()) o.insert(u"style", style.toJson());
    QJsonArray arr;
    for (const Zone &z : zones)
        arr.append(z.toJson());
    o.insert(u"zones", arr);
    return o;
}

Theme Theme::fromJson(const QJsonObject &o)
{
    Theme t;
    t.name = o.value(u"name").toString();
    t.background = o.value(u"background").toObject();
    t.style = Style::fromJson(o.value(u"style").toObject());
    const QJsonObject kinds = o.value(u"kinds").toObject();
    for (auto it = kinds.begin(); it != kinds.end(); ++it)
        t.kinds.insert(it.key(), Style::fromJson(it.value().toObject()));

    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!kThemeKeys.contains(it.key()))
            t.extra.insert(it.key(), it.value());
    }
    return t;
}

QJsonObject Theme::toJson() const
{
    QJsonObject o = extra;
    if (!name.isEmpty()) o.insert(u"name", name);
    if (!background.isEmpty()) o.insert(u"background", background);
    if (!style.isEmpty()) o.insert(u"style", style.toJson());
    if (!kinds.isEmpty()) {
        QJsonObject k;
        for (auto it = kinds.begin(); it != kinds.end(); ++it)
            k.insert(it.key(), it.value().toJson());
        o.insert(u"kinds", k);
    }
    return o;
}

} // namespace vt::layout
