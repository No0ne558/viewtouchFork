#include "editorcontroller.hh"

#include "layout/schema.hh"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

using namespace Qt::StringLiterals;
using vt::app::LayoutEditor;
using vt::layout::Layout;
using vt::layout::Page;
using vt::layout::Zone;
using vt::layout::ZoneState;

namespace {

QJsonValue toJson(const QVariant &v)
{
    // QML `undefined` arrives as an invalid QVariant: remove the key.
    if (!v.isValid())
        return QJsonValue(QJsonValue::Undefined);
    return QJsonValue::fromVariant(v);
}

std::optional<ZoneState> parseState(QStringView s)
{
    if (s == u"normal") return ZoneState::Normal;
    if (s == u"selected") return ZoneState::Selected;
    if (s == u"disabled") return ZoneState::Disabled;
    return std::nullopt;
}

std::optional<QJsonObject> readJsonFile(const QUrl &url, QString *why)
{
    QFile f(url.toLocalFile());
    if (!f.open(QIODevice::ReadOnly)) {
        *why = u"Cannot open %1: %2"_s.arg(f.fileName(), f.errorString());
        return std::nullopt;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (!doc.isObject()) {
        *why = u"%1 is not a ViewTouch file: %2"_s.arg(QFileInfo(f).fileName(), err.errorString());
        return std::nullopt;
    }
    return doc.object();
}

bool writeJsonFile(const QUrl &url, const QJsonObject &o, QString *why)
{
    QSaveFile f(url.toLocalFile());
    if (!f.open(QIODevice::WriteOnly)) {
        *why = u"Cannot write %1: %2"_s.arg(f.fileName(), f.errorString());
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        *why = u"Cannot write %1: %2"_s.arg(f.fileName(), f.errorString());
        return false;
    }
    return true;
}

} // namespace

EditorController::EditorController(Layout base, QObject *parent)
    : QObject(parent)
    , editor_(std::move(base))
{
    editor_.setChangedCallback([this] { onLayoutChanged(); });
    QUndoStack *stack = editor_.undoStack();
    connect(stack, &QUndoStack::indexChanged, this, &EditorController::stateChanged);
    connect(stack, &QUndoStack::cleanChanged, this, &EditorController::stateChanged);
}

void EditorController::setPageId(const QString &pageId)
{
    if (pageId == pageId_)
        return;
    pageId_ = pageId;
    selection_.clear();
    emit pageChanged();
    emit selectionChanged();
    emit layoutChanged();   // geometry & references are per page
    bump();
}

void EditorController::onLayoutChanged()
{
    // Drop selected ids that an undo or delete removed.
    const Page *page = editor_.layout().page(pageId_);
    QStringList kept;
    for (const QString &id : std::as_const(selection_)) {
        if (page && page->zone(id))
            kept.append(id);
    }
    if (kept != selection_) {
        selection_ = kept;
        emit selectionChanged();
    }
    emit layoutChanged();
    bump();
}

void EditorController::setSelection(QStringList ids)
{
    ids.removeDuplicates();
    if (ids == selection_)
        return;
    selection_ = ids;
    emit selectionChanged();
    bump();
}

void EditorController::bump()
{
    ++revision_;
    emit revisionChanged();
}

bool EditorController::fail(const QString &why)
{
    setNotice(why);
    return false;
}

void EditorController::setNotice(const QString &text)
{
    notice_ = text;
    emit noticeChanged();
}

QString EditorController::selectionKind() const
{
    const Page *page = editor_.layout().page(pageId_);
    QString kind;
    for (const QString &id : selection_) {
        const Zone *z = page ? page->zone(id) : nullptr;
        if (!z)
            continue;
        if (!kind.isEmpty() && kind != z->kind)
            return {};
        kind = z->kind;
    }
    return kind;
}

QVariantList EditorController::geometry() const
{
    QVariantList out;
    for (const Layout::PlacedZone &pz : editor_.layout().effectiveZones(pageId_)) {
        const Zone &z = *pz.zone;
        out.append(QVariantMap{
            {u"id"_s, z.id}, {u"kind"_s, z.kind}, {u"label"_s, z.label},
            {u"x"_s, z.rect.x()}, {u"y"_s, z.rect.y()}, {u"w"_s, z.rect.width()}, {u"h"_s, z.rect.height()},
            {u"inherited"_s, pz.inherited}, {u"owner"_s, pz.owner->id}, {u"ownerName"_s, pz.owner->name},
        });
    }
    return out;
}

QVariantList EditorController::pages() const
{
    QVariantList out;
    for (const Page &p : editor_.layout().pages) {
        out.append(QVariantMap{
            {u"id"_s, p.id}, {u"name"_s, p.name}, {u"kind"_s, p.kind}, {u"role"_s, p.role},
            {u"templateId"_s, p.templateId}, {u"zoneCount"_s, p.zones.size()},
        });
    }
    return out;
}

bool EditorController::canUndo() const { return editor_.undoStack()->canUndo(); }
bool EditorController::canRedo() const { return editor_.undoStack()->canRedo(); }
QString EditorController::undoText() const { return editor_.undoStack()->undoText(); }
QString EditorController::redoText() const { return editor_.undoStack()->redoText(); }

QStringList EditorController::textures() const
{
    QStringList names;
    for (const QString &file : QDir(u":/textures"_s).entryList({u"*.xpm"_s}, QDir::Files, QDir::Name))
        names.append(QFileInfo(file).completeBaseName());
    return names;
}

QStringList EditorController::basicKinds() const { return vt::layout::schema::basicKinds(); }
QStringList EditorController::widgetKinds() const { return vt::layout::schema::widgetKinds(); }
QStringList EditorController::pageKinds() const { return vt::layout::schema::pageKinds(); }

// --- selection -----------------------------------------------------------------

void EditorController::select(const QString &zoneId, bool additive)
{
    QStringList ids = additive ? selection_ : QStringList();
    if (additive && ids.contains(zoneId))
        ids.removeAll(zoneId);
    else
        ids.append(zoneId);
    setSelection(ids);
}

void EditorController::selectOnly(const QStringList &zoneIds)
{
    setSelection(zoneIds);
}

void EditorController::selectInRect(qreal x, qreal y, qreal w, qreal h, bool additive)
{
    const Page *page = editor_.layout().page(pageId_);
    if (!page)
        return;
    const QRect band = QRectF(x, y, w, h).normalized().toAlignedRect();
    QStringList ids = additive ? selection_ : QStringList();
    for (const Zone &z : page->zones) {
        if (band.intersects(z.rect))
            ids.append(z.id);
    }
    setSelection(ids);
}

void EditorController::selectAll()
{
    QStringList ids;
    if (const Page *page = editor_.layout().page(pageId_)) {
        for (const Zone &z : page->zones)
            ids.append(z.id);
    }
    setSelection(ids);
}

void EditorController::clearSelection()
{
    setSelection({});
}

// --- zone editing --------------------------------------------------------------

QString EditorController::addZone(const QString &kind)
{
    const QString id = editor_.addZone(pageId_, kind);
    if (!id.isEmpty())
        setSelection({id});
    return id;
}

void EditorController::deleteSelection()
{
    if (!selection_.isEmpty())
        editor_.deleteZones(pageId_, selection_);
}

void EditorController::duplicateSelection()
{
    const QStringList ids = editor_.duplicateZones(pageId_, selection_);
    if (!ids.isEmpty())
        setSelection(ids);
}

void EditorController::nudge(int dx, int dy)
{
    if (!selection_.isEmpty())
        editor_.nudgeZones(pageId_, selection_, dx, dy);
}

void EditorController::commitRects(const QVariantMap &rects)
{
    QHash<QString, QRect> map;
    for (auto it = rects.begin(); it != rects.end(); ++it) {
        const QVariantMap r = it.value().toMap();
        map.insert(it.key(), QRect(qRound(r.value(u"x"_s).toDouble()), qRound(r.value(u"y"_s).toDouble()),
                                   qRound(r.value(u"w"_s).toDouble()), qRound(r.value(u"h"_s).toDouble())));
    }
    editor_.setZoneRects(pageId_, map);
}

void EditorController::bringToFront() { editor_.bringToFront(pageId_, selection_); }
void EditorController::sendToBack() { editor_.sendToBack(pageId_, selection_); }

void EditorController::align(const QString &how)
{
    static const QHash<QString, LayoutEditor::Align> names = {
        {u"left"_s, LayoutEditor::Align::Left}, {u"hcenter"_s, LayoutEditor::Align::HCenter},
        {u"right"_s, LayoutEditor::Align::Right}, {u"top"_s, LayoutEditor::Align::Top},
        {u"vcenter"_s, LayoutEditor::Align::VCenter}, {u"bottom"_s, LayoutEditor::Align::Bottom},
    };
    if (selection_.size() < 2)
        fail(tr("Select two or more zones to align."));
    else if (names.contains(how))
        editor_.align(pageId_, selection_, names.value(how));
}

void EditorController::distribute(const QString &axis)
{
    if (selection_.size() < 3)
        fail(tr("Select three or more zones to distribute."));
    else
        editor_.distribute(pageId_, selection_, axis == u"vertical" ? Qt::Vertical : Qt::Horizontal);
}

void EditorController::matchSize(const QString &which)
{
    if (selection_.size() < 2)
        fail(tr("Select two or more zones; the first one sets the size."));
    else
        editor_.matchSize(pageId_, selection_, which != u"height", which != u"width");
}

void EditorController::copy()
{
    editor_.copyZones(pageId_, selection_);
    emit stateChanged();
}

void EditorController::cut()
{
    copy();
    deleteSelection();
}

void EditorController::paste()
{
    const QStringList ids = editor_.paste(pageId_);
    if (!ids.isEmpty())
        setSelection(ids);
}

void EditorController::undo() { editor_.undoStack()->undo(); }
void EditorController::redo() { editor_.undoStack()->redo(); }

// --- inspector -------------------------------------------------------------------

QVariantList EditorController::zoneFields(const QString &kind) const
{
    return vt::layout::schema::zoneFields(kind).toVariantList();
}

QVariantList EditorController::pageFields() const
{
    QVariantList fields = vt::layout::schema::pageFields().toVariantList();
    if (mealPeriods_.isEmpty())
        return fields;
    QVariantList choices{QVariantMap{{u"value"_s, QString()}, {u"text"_s, tr("(none)")}}};
    for (const QVariant &v : mealPeriods_) {
        const QVariantMap m = v.toMap();
        choices.append(QVariantMap{{u"value"_s, m.value(u"id"_s)}, {u"text"_s, m.value(u"name"_s)}});
    }
    choices.append(QVariantMap{{u"value"_s, u"all"_s}, {u"text"_s, tr("All day")}});
    for (QVariant &f : fields) {
        QVariantMap m = f.toMap();
        if (m.value(u"path"_s) == u"mealPeriod"_s) {
            m.insert(u"options"_s, choices);
            f = m;
        }
    }
    return fields;
}

QVariantList EditorController::themeFields() const
{
    return vt::layout::schema::themeFields().toVariantList();
}

QVariantList EditorController::actionTypes() const
{
    return vt::layout::schema::actionTypes().toVariantList();
}

QVariantList EditorController::pageOptions() const
{
    QVariantList out{QVariantMap{{u"value"_s, QString()}, {u"text"_s, tr("(none)")}}};
    for (const Page &p : editor_.layout().pages)
        out.append(QVariantMap{{u"value"_s, p.id}, {u"text"_s, u"%1  ·  %2"_s.arg(p.name, p.id)}});
    return out;
}

QVariantMap EditorController::fieldInfo(const QString &target, const QString &path) const
{
    const Layout &l = editor_.layout();
    QJsonValue value(QJsonValue::Undefined);
    QJsonValue resolved(QJsonValue::Undefined);
    bool mixed = false;

    const QStringList parts = path.split(u'.');
    const auto state = parts.size() == 3 && parts[0] == u"style" ? parseState(parts[1]) : std::nullopt;

    if (target == u"zone") {
        const Page *page = l.page(pageId_);
        if (!page || selection_.isEmpty())
            return {};
        bool first = true;
        for (const QString &id : selection_) {
            const QJsonValue v = editor_.zoneField(pageId_, id, path);
            if (first) {
                value = v;
                first = false;
            } else if (v != value) {
                mixed = true;
            }
        }
        if (state) {
            if (const Zone *z = page->zone(selection_.first()))
                resolved = l.resolveStyle(*z, pageId_, *state).value(parts[2]);
        }
    } else if (target == u"page") {
        value = editor_.pageField(pageId_, path);
        if (parts.size() == 2 && parts[0] == u"background") {
            // What the page would show without its own value.
            Layout without = l;
            if (Page *p = without.page(pageId_))
                p->background.remove(parts[1]);
            resolved = without.resolveBackground(pageId_).value(parts[1]);
        } else if (state) {
            Layout without = l;
            if (Page *p = without.page(pageId_))
                p->style.state(*state).remove(parts[2]);
            Zone probe;
            resolved = without.resolveStyle(probe, pageId_, *state).value(parts[2]);
        }
    } else if (target == u"theme") {
        value = editor_.themeField(path);
    }

    return {{u"value"_s, value.toVariant()}, {u"mixed"_s, mixed}, {u"resolved"_s, resolved.toVariant()},
            {u"isSet"_s, !value.isUndefined()}};
}

bool EditorController::setField(const QString &target, const QString &path, const QVariant &value)
{
    const QJsonValue v = toJson(value);
    if (target == u"zone")
        return !selection_.isEmpty() && editor_.setZoneField(pageId_, selection_, path, v);
    if (target == u"theme")
        return editor_.setThemeField(path, v);
    if (target == u"page") {
        QString why;
        const QString before = pageId_;
        if (!editor_.setPageField(pageId_, path, v, &why))
            return why.isEmpty() ? false : fail(why);
        if (path == u"id" && !editor_.layout().page(before))
            emit showPageRequested(v.toString());
        return true;
    }
    return false;
}

bool EditorController::clearField(const QString &target, const QString &path)
{
    return setField(target, path, QVariant());
}

QVariantList EditorController::actions() const
{
    if (selection_.size() != 1)
        return {};
    return editor_.zoneField(pageId_, selection_.first(), u"actions"_s).toArray().toVariantList();
}

void EditorController::setActions(const QVariantList &actions)
{
    if (selection_.size() == 1)
        editor_.setZoneField(pageId_, selection_, u"actions"_s, QJsonArray::fromVariantList(actions));
}

// --- pages ---------------------------------------------------------------------------

QString EditorController::newPage(const QString &name, const QString &kind, const QString &templateId)
{
    const QString id = editor_.addPage(name, kind, templateId);
    if (!id.isEmpty())
        emit showPageRequested(id);
    return id;
}

QString EditorController::duplicatePage()
{
    const QString id = editor_.duplicatePage(pageId_);
    if (!id.isEmpty())
        emit showPageRequested(id);
    return id;
}

bool EditorController::deletePage()
{
    QString why;
    if (!editor_.deletePage(pageId_, &why))
        return fail(why);
    return true;   // LayoutController leaves the vanished page
}

QString EditorController::templateOf(const QString &zoneId) const
{
    for (const Layout::PlacedZone &pz : editor_.layout().effectiveZones(pageId_)) {
        if (pz.zone->id == zoneId && pz.inherited)
            return pz.owner->id;
    }
    return {};
}

// --- files ---------------------------------------------------------------------------

bool EditorController::exportPage(const QUrl &file)
{
    QString why;
    if (!writeJsonFile(file, editor_.exportPage(pageId_), &why))
        return fail(why);
    setNotice(tr("Exported page to %1").arg(file.fileName()));
    return true;
}

bool EditorController::importPage(const QUrl &file)
{
    QString why;
    const auto json = readJsonFile(file, &why);
    if (!json)
        return fail(why);
    const QString id = editor_.importPage(*json, &why);
    if (id.isEmpty())
        return fail(why);
    emit showPageRequested(id);
    return true;
}

bool EditorController::exportLayout(const QUrl &file)
{
    QString why;
    if (!writeJsonFile(file, editor_.layout().toJson(), &why))
        return fail(why);
    setNotice(tr("Exported all pages to %1").arg(file.fileName()));
    return true;
}

bool EditorController::importLayout(const QUrl &file)
{
    QString why;
    const auto json = readJsonFile(file, &why);
    if (!json)
        return fail(why);
    QStringList errors;
    const auto imported = Layout::fromJson(*json, &errors);
    if (!imported || imported->pages.isEmpty())
        return fail(errors.isEmpty() ? tr("The file does not contain a layout.") : errors.join(u'\n'));
    if (!imported->pageByRole(u"login"_s))
        return fail(tr("That layout has no login page."));
    editor_.replaceLayout(*imported, tr("Import layout"));
    return true;
}
