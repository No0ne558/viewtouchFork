#include "layoutcontroller.hh"

#include "core/employee.hh"
#include "storage/layout_store.hh"

#include <QJsonArray>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;
using vt::app::Navigator;
using vt::app::PosService;
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

// Qualifier a zone sets ("no", "extra"...), used to light it while pending.
QString qualifierOf(const vt::layout::Zone &zone)
{
    for (const Action &a : zone.actions) {
        if (a.type() == u"qualifier")
            return a.str(u"qualifier");
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

void LayoutController::setPos(PosService *pos)
{
    if (pos_ == pos)
        return;
    if (pos_)
        disconnect(pos_, nullptr, this, nullptr);
    pos_ = pos;
    if (pos_) {
        connect(pos_, &PosService::notice, this, &LayoutController::setStatus);
        connect(pos_, &PosService::loggedInChanged, this, &LayoutController::onLoggedInChanged);
        connect(pos_, &PosService::checkClosed, this, [this] { navigate(Navigator::Mode::Home); });
        connect(pos_, &PosService::qualifierChanged, this, &LayoutController::refresh);
    }
    emit posChanged();
    refresh();
}

QString LayoutController::rolePage(const QString &role) const
{
    const vt::layout::Page *p = activeLayout().pageByRole(role);
    return p ? p->id : QString();
}

void LayoutController::onLoggedInChanged(bool loggedIn)
{
    // Logged in, "home" is the floor (tables); logged out, it is the login page.
    const QString login = homePageOf(layout_);
    const QString tables = rolePage(u"tables"_s);
    const QString home = loggedIn && !tables.isEmpty() ? tables : login;
    nav_.reset(home);
    if (editing_ && editor_)
        editor_->setPageId(nav_.current());
    refresh();
    emit pageChanged();
}

bool LayoutController::mayOpen(const QString &pageId)
{
    if (!pos_ || editing())
        return true;
    const vt::layout::Page *p = activeLayout().page(pageId);
    if (!p)
        return false;
    if (!pos_->loggedIn() && p->role != u"login") {
        setStatus(tr("Log in first."));
        return false;
    }
    if (!p->permission.isEmpty() && !pos_->can(p->permission)) {
        setStatus(tr("%1 cannot open %2.").arg(pos_->userName(), p->name));
        return false;
    }
    return true;
}

void LayoutController::selectTable(const QString &label)
{
    if (!pos_ || editing())
        return;
    switch (pos_->selectTable(label)) {
    case PosService::TableResult::OpenedExisting:
        navigate(Navigator::Mode::Index);
        break;
    case PosService::TableResult::NeedsGuestCount:
        if (const QString page = rolePage(u"guestCount"_s); !page.isEmpty())
            navigate(Navigator::Mode::Push, page);
        else if (pos_->startCheck(vt::core::CheckType::DineIn))
            navigate(Navigator::Mode::Index);
        break;
    case PosService::TableResult::Failed:
        break;
    }
}

void LayoutController::openCheck(qint64 checkId)
{
    if (pos_ && !editing() && pos_->openCheck(checkId))
        navigate(Navigator::Mode::Index);
}

void LayoutController::login()
{
    if (pos_ && !editing())
        pos_->login();   // navigation follows loggedInChanged
}

bool LayoutController::requestEditMode()
{
    if (pos_ && !pos_->can(QString::fromLatin1(vt::core::perm::EditLayout))) {
        setStatus(pos_->loggedIn() ? tr("%1 may not edit pages.").arg(pos_->userName())
                                   : tr("Log in as a manager to edit pages."));
        return false;
    }
    enterEditMode();
    return true;
}

const Layout &LayoutController::activeLayout() const
{
    return editing_ && editor_ ? editor_->layout() : layout_;
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
            if (!runAction(a) || nav_.current() != before || editing())
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
    return navigate(Navigator::Mode::Push, pageId) || nav_.current() == pageId;
}

bool LayoutController::showPage(const QString &pageId)
{
    if (!activeLayout().page(pageId))
        return false;
    return navigate(Navigator::Mode::Replace, pageId) || nav_.current() == pageId;
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
    if (editing_)
        return;
    editor_ = new EditorController(layout_, this);
    connect(editor_, &EditorController::layoutChanged, this, &LayoutController::onDraftChanged);
    connect(editor_, &EditorController::showPageRequested, this, &LayoutController::showPage);
    emit editorChanged();
    editing_ = true;
    nav_.setLayout(editor_->layout());
    editor_->setPageId(nav_.current());
    emit editingChanged();
    refresh();
    emit pageChanged();
}

bool LayoutController::saveEdits()
{
    if (!editing_ || !editor_)
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
    if (!editing_)
        return true;
    if (save && editor_->dirty() && !saveEdits())
        return false;

    nav_.setLayout(layout_);
    editing_ = false;
    emit editingChanged();   // editor panels unload while the editor still exists

    EditorController *old = editor_;
    disconnect(old, nullptr, this, nullptr);
    connect(old, &QObject::destroyed, this, &LayoutController::editorChanged);
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
    if (editing_ && editor_)
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
        return navigate(*mode, target);
    }

    if (type == u"addItem") {
        const QString item = a.str(u"item");
        if (pos_) {
            if (!pos_->addItem(item))
                return false;
        } else {
            setStatus(tr("Added %1").arg(item));
        }
        emit itemAdded(item);
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
        if (pos_)
            pos_->setQualifier(a.str(u"qualifier"));
        else
            setStatus(tr("Qualifier: %1").arg(a.str(u"qualifier")));
        return true;
    }

    if (type == u"tender") {
        if (!pos_) {
            setStatus(tr("Tender: %1").arg(a.str(u"tender")));
            return true;
        }
        const QJsonValue amount = a.data.value(u"amount");
        return pos_->tender(a.str(u"tender"),
                            amount.isDouble() ? std::optional<std::int64_t>(amount.toInteger()) : std::nullopt);
    }

    if (type == u"command")
        return runCommand(a.str(u"name"), a.data.value(u"args").toObject().toVariantMap());

    setStatus(tr("Action '%1' is not supported yet").arg(type));
    return false;
}

bool LayoutController::runCommand(const QString &name, const QVariantMap &args)
{
    if (name == u"editMode")
        return requestEditMode();

    if (pos_) {
        using vt::core::CheckType;
        if (name == u"login") return pos_->login();
        if (name == u"logout") { pos_->logout(); return true; }
        if (name == u"clockIn") return pos_->clockIn();
        if (name == u"clockOut") return pos_->clockOut();
        if (name == u"startCheck") return pos_->startCheck(CheckType::DineIn);
        if (name == u"startQuick") return pos_->startCheck(CheckType::Quick);
        if (name == u"startTakeout") return pos_->startCheck(CheckType::Takeout);
        if (name == u"releaseCheck") { pos_->releaseCheck(); return true; }
        if (name == u"sendOrder") return pos_->sendOrder();
        if (name == u"voidItem") return pos_->voidItem();
        if (name == u"addComment") return pos_->addComment();
        if (name == u"removePayment") return pos_->removePayment();
        if (name == u"closeCheck") return pos_->closeCheck();
    }

    // Not built yet (printing and drawers are M4, admin screens M4).
    emit commandRequested(name, args);
    setStatus(tr("'%1' is not available yet").arg(name));
    return true;
}

bool LayoutController::navigate(Navigator::Mode mode, const QString &target)
{
    if ((mode == Navigator::Mode::Push || mode == Navigator::Mode::Replace) && !mayOpen(target))
        return false;
    if (nav_.jump(mode, target)) {
        if (editing_ && editor_)
            editor_->setPageId(nav_.current());
        refresh();
        emit pageChanged();
        return true;
    }
    return false;
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
        const QString qualifier = pos_ ? qualifierOf(z) : QString();
        const bool current = !editing()
            && ((!target.isEmpty() && (target == pageId || (onItemPage && target == nav_.lastIndex())))
                || (!qualifier.isEmpty() && qualifier == pos_->pendingQualifier()));

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
