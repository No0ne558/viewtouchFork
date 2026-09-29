#include "layoutcontroller.hh"

#include "storage/layout_store.hh"

#include <QJsonArray>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;
using vt::app::Navigator;
using vt::layout::Action;
using vt::layout::Layout;
using vt::layout::ZoneState;

Q_LOGGING_CATEGORY(lcLayout, "vt.layout")

namespace {

// Page id of the first push/replace jump, used to light index tabs.
QString primaryJumpTarget(const Layout &layout, const vt::layout::Zone &zone)
{
    for (const Action &a : zone.actions) {
        if (a.type() != u"jump")
            continue;
        const QString mode = a.data.value(u"mode").toString(u"push"_s);
        if (mode == u"push" || mode == u"replace")
            return layout.resolveTarget(a.data);
    }
    return {};
}

QString homePageOf(const Layout &layout)
{
    if (const vt::layout::Page *p = layout.pageByRole(u"login"_s))
        return p->id;
    return layout.pages.isEmpty() ? QString() : layout.pages.first().id;
}

} // namespace

LayoutController::LayoutController(Layout layout, QObject *parent)
    : QObject(parent)
    , layout_(std::move(layout))
    , nav_(layout_)
{
    nav_.setMealPeriod(mealPeriodAt(QTime::currentTime()));
    nav_.reset(homePageOf(layout_));
    refresh();
}

LayoutController::~LayoutController()
{
    delete editor_.data();
}

QString LayoutController::mealPeriodAt(QTime time)
{
    if (time < QTime(11, 0))
        return u"breakfast"_s;
    if (time < QTime(16, 0))
        return u"lunch"_s;
    return u"dinner"_s;
}

const Layout &LayoutController::activeLayout() const
{
    return editor_ ? editor_->layout() : layout_;
}

QString LayoutController::pageName() const
{
    const auto *p = currentPage();
    return p ? p->name : QString();
}

QString LayoutController::pageKind() const
{
    const auto *p = currentPage();
    return p ? p->kind : QString();
}

QSize LayoutController::canvasSize() const
{
    const auto *p = currentPage();
    return p ? p->canvas : QSize(1920, 1080);
}

int LayoutController::pageGrid() const
{
    const auto *p = currentPage();
    return p ? std::max(1, p->grid) : 8;
}

QVariantMap LayoutController::background() const
{
    return activeLayout().resolveBackground(nav_.current()).toVariantMap();
}

void LayoutController::activate(const QString &zoneId)
{
    if (editing())
        return;   // the editor overlay owns touches
    for (const Layout::PlacedZone &pz : layout_.effectiveZones(nav_.current())) {
        if (pz.zone->id != zoneId)
            continue;
        if (!pz.zone->enabled)
            return;
        // Actions run in order; a page change ends the chain because the
        // remaining actions belonged to the page that is no longer shown.
        const QString before = nav_.current();
        for (const Action &a : pz.zone->actions) {
            runAction(a);
            if (nav_.current() != before || editing())
                break;
        }
        return;
    }
    qCWarning(lcLayout) << "activate: no zone" << zoneId << "on page" << nav_.current();
}

void LayoutController::goBack()
{
    navigate(Navigator::Mode::Back);
}

void LayoutController::goHome()
{
    navigate(Navigator::Mode::Home);
}

bool LayoutController::jumpTo(const QString &pageId)
{
    if (!activeLayout().page(pageId))
        return false;
    navigate(Navigator::Mode::Push, pageId);
    return true;
}

bool LayoutController::showPage(const QString &pageId)
{
    if (!activeLayout().page(pageId))
        return false;
    navigate(Navigator::Mode::Replace, pageId);
    return true;
}

bool LayoutController::triggerHotkey(const QString &key)
{
    if (key.isEmpty() || editing())
        return false;
    const auto zones = layout_.effectiveZones(nav_.current());
    // Topmost zone wins, matching touch order.
    for (auto it = zones.rbegin(); it != zones.rend(); ++it) {
        if (it->zone->enabled && it->zone->hotkey.compare(key, Qt::CaseInsensitive) == 0) {
            activate(it->zone->id);
            return true;
        }
    }
    return false;
}

// --- edit mode -------------------------------------------------------------------

void LayoutController::enterEditMode()
{
    if (editor_)
        return;
    editor_ = new EditorController(layout_, this);
    connect(editor_, &EditorController::layoutChanged, this, &LayoutController::onDraftChanged);
    connect(editor_, &EditorController::showPageRequested, this, &LayoutController::showPage);
    nav_.setLayout(editor_->layout());
    editor_->setPageId(nav_.current());
    emit editingChanged();
    refresh();
    emit pageChanged();
}

bool LayoutController::saveEdits()
{
    if (!editor_)
        return false;
    const Layout draft = editor_->layout();
    if (store_) {
        QString error;
        if (!store_->save(draft, &error)) {
            setStatus(tr("Could not save: %1").arg(error));
            return false;
        }
    }
    layout_ = draft;
    editor_->editor().markClean();
    setStatus(store_ ? tr("Saved") : tr("Applied (not saved to disk)"));
    return true;
}

bool LayoutController::leaveEditMode(bool save)
{
    if (!editor_)
        return true;
    if (save && editor_->dirty() && !saveEdits())
        return false;

    nav_.setLayout(layout_);
    EditorController *old = editor_;
    editor_ = nullptr;
    emit editingChanged();   // QML drops its references before the object goes
    old->deleteLater();

    nav_.setHome(homePageOf(layout_));
    ensureCurrentPageExists();
    refresh();
    emit pageChanged();
    return true;
}

void LayoutController::onDraftChanged()
{
    nav_.setHome(homePageOf(activeLayout()));
    ensureCurrentPageExists();
    refresh();
    emit pageChanged();
}

void LayoutController::ensureCurrentPageExists()
{
    if (!activeLayout().page(nav_.current()))
        nav_.jump(Navigator::Mode::Back);   // skips vanished pages, falls back home
    if (editor_)
        editor_->setPageId(nav_.current());
}

// --- actions ---------------------------------------------------------------------

bool LayoutController::runAction(const Action &a)
{
    const QString type = a.type();

    if (type == u"jump") {
        const QString modeName = a.str(u"mode");
        const auto mode = Navigator::parseMode(modeName);
        if (!mode) {
            setStatus(tr("Unknown jump mode '%1'").arg(modeName));
            return false;
        }
        const QString target = layout_.resolveTarget(a.data);
        if ((*mode == Navigator::Mode::Push || *mode == Navigator::Mode::Replace) && target.isEmpty()) {
            setStatus(tr("This button's page does not exist"));
            return false;
        }
        navigate(*mode, target);
        return true;
    }

    if (type == u"addItem") {
        const QString item = a.str(u"item");
        emit itemAdded(item);
        setStatus(tr("Added %1").arg(item));
        QStringList sequence;
        for (const QJsonValue &v : a.data.value(u"modifierSequence").toArray())
            sequence.append(v.toString());
        if (!sequence.isEmpty() && nav_.startSequence(sequence)) {
            refresh();
            emit pageChanged();
        }
        return true;
    }

    if (type == u"qualifier") {
        setStatus(tr("Qualifier: %1").arg(a.str(u"qualifier")));
        return true;
    }

    if (type == u"tender") {
        setStatus(tr("Tender: %1 (payments arrive in M3)").arg(a.str(u"tender")));
        return true;
    }

    if (type == u"command") {
        const QString name = a.str(u"name");
        if (name == u"editMode") {
            enterEditMode();
            return true;
        }
        emit commandRequested(name, a.data.value(u"args").toObject().toVariantMap());
        setStatus(tr("'%1' is not available yet").arg(name));
        return true;
    }

    setStatus(tr("Action '%1' is not supported yet").arg(type));
    return false;
}

void LayoutController::navigate(Navigator::Mode mode, const QString &target)
{
    if (nav_.jump(mode, target)) {
        if (editor_)
            editor_->setPageId(nav_.current());
        refresh();
        emit pageChanged();
    }
}

void LayoutController::refresh()
{
    const Layout &l = activeLayout();
    const QString pageId = nav_.current();
    const auto *page = currentPage();
    const bool onItemPage = page && (page->kind == u"items" || page->kind == u"modifier");

    QList<ZoneModel::Row> rows;
    for (const Layout::PlacedZone &pz : l.effectiveZones(pageId)) {
        const vt::layout::Zone &z = *pz.zone;
        const QString target = primaryJumpTarget(l, z);
        const bool current = !editing() && !target.isEmpty()
            && (target == pageId || (onItemPage && target == nav_.lastIndex()));

        rows.append({
            {ZoneModel::ZoneIdRole, z.id},
            {ZoneModel::KindRole, z.kind},
            {ZoneModel::ZoneNameRole, z.name},
            {ZoneModel::LabelRole, z.label},
            {ZoneModel::ZoneXRole, z.rect.x()},
            {ZoneModel::ZoneYRole, z.rect.y()},
            {ZoneModel::ZoneWRole, z.rect.width()},
            {ZoneModel::ZoneHRole, z.rect.height()},
            {ZoneModel::ShapeRole, z.shape},
            {ZoneModel::BehaviorRole, z.behavior},
            {ZoneModel::ZoneEnabledRole, z.enabled},
            {ZoneModel::InheritedRole, pz.inherited},
            {ZoneModel::CurrentRole, current},
            {ZoneModel::HotkeyRole, z.hotkey},
            {ZoneModel::GroupRole, z.group},
            {ZoneModel::ImagePathRole, z.imagePath},
            {ZoneModel::StyleNormalRole, l.resolveStyle(z, pageId, ZoneState::Normal).toVariantMap()},
            {ZoneModel::StyleSelectedRole, l.resolveStyle(z, pageId, ZoneState::Selected).toVariantMap()},
            {ZoneModel::StyleDisabledRole, l.resolveStyle(z, pageId, ZoneState::Disabled).toVariantMap()},
            {ZoneModel::PropsRole, z.props.toVariantMap()},
        });
    }
    zones_.setRows(std::move(rows));
}

void LayoutController::setStatus(const QString &text)
{
    status_ = text;
    emit statusChanged();
}
