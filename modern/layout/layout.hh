#pragma once

#include "layout/page.hh"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace vt::layout {

// The complete customizable UI: a theme plus every page.
//
// On disk a layout is either a directory (theme.json + pages/<id>.json, used
// for the shipped seed) or a single .vtlayout.json document.
class Layout {
public:
    static constexpr int SchemaVersion = 1;
    static constexpr int MaxTemplateDepth = 8;

    Theme theme;
    QList<Page> pages;

    const Page *page(const QString &id) const;
    Page *page(const QString &id);
    const Page *pageByRole(const QString &role) const;
    const Page *firstPageOfKind(const QString &kind) const;

    // Page id named by an action's "page" or "role" key; empty if unresolved.
    QString resolveTarget(const QJsonObject &params) const;

    // [page, its template, that template's template, ...]. Stops at a missing
    // template, a cycle, or MaxTemplateDepth.
    QList<const Page *> templateChain(const QString &pageId) const;

    struct PlacedZone {
        const Zone *zone = nullptr;
        const Page *owner = nullptr;
        bool inherited = false;
    };
    // Zones visible on a page, bottom to top: deepest template first, the
    // page's own zones last. Within a page, ordered by z then file order.
    QList<PlacedZone> effectiveZones(const QString &pageId) const;

    // Fully resolved style for a zone in a state, as seen on `viewingPageId`:
    // zone -> page chain -> theme kind -> theme -> (non-normal) resolved normal.
    QJsonObject resolveStyle(const Zone &zone, const QString &viewingPageId, ZoneState state) const;
    QJsonObject resolveBackground(const QString &pageId) const;

    // Human-readable problems: dangling references, duplicate ids, missing
    // required roles, zones off the canvas. Empty means the layout is sound.
    QStringList validate() const;

    // The version of `pageId` made for `formFactor` screens ("phone"), if any.
    const Page *variantFor(const QString &pageId, const QString &formFactor) const;

    // Every table name in the layout (its table zones).
    QStringList tableLabels() const;
    // A table name not used yet, following `like`: "T7" -> "T8" (the next
    // free number after the highest with that prefix), "Patio" -> "Patio 2".
    QString nextTableLabel(const QString &like = QStringLiteral("T1")) const;

    // False (with an error) when `o` carries a schemaVersion newer than ours.
    static bool checkSchema(const QJsonObject &o, const QString &what, QStringList *errors = nullptr);

    static std::optional<Layout> fromJson(const QJsonObject &o, QStringList *errors = nullptr);
    QJsonObject toJson() const;

    static std::optional<Layout> loadDirectory(const QString &dir, QStringList *errors = nullptr);
    bool saveDirectory(const QString &dir, QStringList *errors = nullptr) const;

    bool operator==(const Layout &) const = default;
};

} // namespace vt::layout
