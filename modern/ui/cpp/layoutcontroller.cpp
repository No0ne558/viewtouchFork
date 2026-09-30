#include "layoutcontroller.hh"

#include "core/employee.hh"
#include "storage/layout_store.hh"

#include <QPointer>

#include <QJsonArray>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;
using vt::app::Navigator;
using vt::app::PosSession;
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

void LayoutController::setStore(vt::storage::LayoutStore *store)
{
    if (!store) {
        saver_ = {};
        return;
    }
    saver_ = [store](const Layout &layout, QString *error) { return store->save(layout, error); };
}

void LayoutController::replaceLayout(Layout layout)
{
    layout_ = std::move(layout);
    if (editing_) {
        setStatus(tr("Pages were changed on another terminal. Saving your edits will replace them."));
        return;
    }
    nav_.setHome(homePageOf(layout_));
    ensureCurrentPageExists();
    refresh();
    emit pageChanged();
}

void LayoutController::setPos(PosSession *pos)
{
    if (pos_ == pos)
        return;
    if (pos_)
        disconnect(pos_, nullptr, this, nullptr);
    pos_ = pos;
    if (pos_) {
        connect(pos_, &PosSession::notice, this, &LayoutController::setStatus);
        connect(pos_, &PosSession::loggedInChanged, this, &LayoutController::onLoggedInChanged);
        connect(pos_, &PosSession::checkClosed, this, [this] { navigate(Navigator::Mode::Home); });
        connect(pos_, &PosSession::qualifierChanged, this, &LayoutController::refresh);
        // Whoever is editing must stay allowed to: logging out, being
        // deactivated or losing the role closes the editor (unsaved edits
        // are dropped, as in the legacy system).
        connect(pos_, &PosSession::sessionChanged, this, [this] {
            if (editing_ && !pos_->can(QString::fromLatin1(vt::core::perm::EditLayout))) {
                leaveEditMode(false);
                setStatus(tr("Edit mode closed: the page editor needs a manager logged in."));
            }
        });
    }
    emit posChanged();
    refresh();
}

void LayoutController::call(const QString &method, const QVariantList &args,
                            std::function<void(const QVariant &)> then)
{
    ++pending_;
    if (pending_ == 1)
        emit busyChanged();
    // QPointer: a remote reply may arrive after this controller is gone.
    QPointer<LayoutController> self(this);
    pos_->invoke(method, args, [self, then = std::move(then)](const QVariant &result) {
        if (!self)
            return;
        if (--self->pending_ == 0)
            emit self->busyChanged();
        if (then)
            then(result);
    });
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
    if (p->permission == u"public")
        return true;   // e.g. kitchen displays that nobody logs in to
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
    if (!pos_ || editing() || busy())
        return;
    call(u"selectTable"_s, {label}, [this](const QVariant &result) {
        switch (result.toInt()) {
        case PosSession::TableOpened:
            navigate(Navigator::Mode::Index);
            break;
        case PosSession::TableNeedsGuests:
            if (const QString page = rolePage(u"guestCount"_s); !page.isEmpty()) {
                navigate(Navigator::Mode::Push, page);
            } else {
                call(u"startCheck"_s, {u"dineIn"_s}, [this](const QVariant &ok) {
                    if (ok.toBool())
                        navigate(Navigator::Mode::Index);
                });
            }
            break;
        case PosSession::TableChooseCheck:
            // Several checks at the table: pick one from the (filtered) list.
            if (const QString page = rolePage(u"checkList"_s); !page.isEmpty())
                navigate(Navigator::Mode::Push, page);
            break;
        default:
            break;
        }
    });
}

void LayoutController::openCheck(qint64 checkId)
{
    if (!pos_ || editing() || busy())
        return;
    call(u"openCheck"_s, {checkId}, [this](const QVariant &ok) {
        if (ok.toBool())
            navigate(Navigator::Mode::Index);
    });
}

void LayoutController::login()
{
    if (pos_ && !editing())
        call(u"login"_s, {}, {});   // navigation follows loggedInChanged
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
    if (busy())
        return;   // still waiting for the server on the previous touch
    for (const Layout::PlacedZone &pz : layout_.effectiveZones(nav_.current())) {
        if (pz.zone->id != zoneId)
            continue;
        if (!pz.zone->enabled)
            return;
        runChain(pz.zone->actions, 0, nav_.current());
        return;
    }
    qCWarning(lcLayout) << "activate: no zone" << zoneId << "on page" << nav_.current();
}

void LayoutController::runChain(QList<Action> actions, int index, QString startPage)
{
    if (index >= actions.size())
        return;
    const Action action = actions.at(index);
    runAction(action, [this, actions = std::move(actions), index, startPage = std::move(startPage)](bool ok) {
        // A page change ends the chain: the rest belonged to the page left.
        if (!ok || nav_.current() != startPage || editing())
            return;
        runChain(actions, index + 1, startPage);
    });
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
    if (saver_) {
        QString error;
        if (!saver_(draft, &error)) {
            setStatus(tr("Could not save: %1").arg(error));
            return false;
        }
    }
    layout_ = draft;
    editor_->editor().markClean();
    setStatus(saver_ ? tr("Saved") : tr("Applied (not saved to disk)"));
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

void LayoutController::runAction(const Action &a, Done done)
{
    const QString type = a.type();

    if (type == u"jump") {
        const QString modeName = a.str(u"mode");
        const auto mode = Navigator::parseMode(modeName);
        if (!mode) {
            setStatus(tr("Unknown jump mode '%1'").arg(modeName));
            return done(false);
        }
        const QString target = layout_.resolveTarget(a.data);
        if ((*mode == Navigator::Mode::Push || *mode == Navigator::Mode::Replace) && target.isEmpty()) {
            setStatus(tr("This button's page does not exist"));
            return done(false);
        }
        return done(navigate(*mode, target));
    }

    if (type == u"addItem") {
        const QString item = a.str(u"item");
        QStringList sequence;
        for (const QJsonValue &v : a.data.value(u"modifierSequence").toArray())
            sequence.append(v.toString());
        auto added = [this, item, sequence, done](bool ok) {
            if (!ok)
                return done(false);
            emit itemAdded(item);
            if (!sequence.isEmpty() && nav_.startSequence(sequence)) {
                refresh();
                emit pageChanged();
            }
            done(true);
        };
        if (!pos_) {
            setStatus(tr("Added %1").arg(item));
            return added(true);
        }
        return call(u"addItem"_s, {item}, [added](const QVariant &ok) { added(ok.toBool()); });
    }

    if (type == u"qualifier") {
        if (!pos_) {
            setStatus(tr("Qualifier: %1").arg(a.str(u"qualifier")));
            return done(true);
        }
        return call(u"setQualifier"_s, {a.str(u"qualifier")}, [done](const QVariant &) { done(true); });
    }

    if (type == u"tender") {
        if (!pos_) {
            setStatus(tr("Tender: %1").arg(a.str(u"tender")));
            return done(true);
        }
        const QJsonValue amount = a.data.value(u"amount");
        return call(u"tender"_s, {a.str(u"tender"), amount.isDouble() ? QVariant(amount.toInteger()) : QVariant()},
                    [done](const QVariant &ok) { done(ok.toBool()); });
    }

    if (type == u"command")
        return runCommand(a.str(u"name"), a.data.value(u"args").toObject().toVariantMap(), std::move(done));

    setStatus(tr("Action '%1' is not supported yet").arg(type));
    done(false);
}

void LayoutController::runCommand(const QString &name, const QVariantMap &args, Done done)
{
    if (name == u"editMode")
        return done(requestEditMode());

    // Manager screens are pages ("admin-menu", "reports"...). Kept as a
    // command so buttons made before those pages existed still work.
    if (name == u"openAdmin") {
        static const QHash<QString, QString> pages = {
            {u"menu"_s, u"admin-menu"_s}, {u"employees"_s, u"admin-employees"_s},
            {u"tenders"_s, u"admin-tenders"_s}, {u"printers"_s, u"admin-printers"_s},
            {u"taxes"_s, u"admin-taxes"_s}, {u"settings"_s, u"admin-store"_s},
            {u"reports"_s, u"reports"_s}, {u"drawers"_s, u"drawer"_s}, {u"endOfDay"_s, u"end-of-day"_s},
            {u"terminals"_s, u"admin-terminals"_s},
        };
        const QString page = pages.value(args.value(u"panel"_s).toString());
        if (!page.isEmpty() && activeLayout().page(page))
            return done(navigate(Navigator::Mode::Push, page));
        setStatus(tr("This screen is not in your pages yet (start with --reset-layout to get it)."));
        return done(false);
    }

    // Commands that are POS operations: name -> (operation, arguments).
    static const QHash<QString, std::pair<QString, QVariantList>> operations = {
        {u"login"_s, {u"login"_s, {}}}, {u"logout"_s, {u"logout"_s, {}}},
        {u"clockIn"_s, {u"clockIn"_s, {}}}, {u"clockOut"_s, {u"clockOut"_s, {}}},
        {u"startCheck"_s, {u"startCheck"_s, {u"dineIn"_s}}}, {u"startQuick"_s, {u"startCheck"_s, {u"quick"_s}}},
        {u"startTakeout"_s, {u"startCheck"_s, {u"takeout"_s}}},
        {u"startDelivery"_s, {u"startCheck"_s, {u"delivery"_s}}},
        {u"releaseCheck"_s, {u"releaseCheck"_s, {}}}, {u"sendOrder"_s, {u"sendOrder"_s, {}}},
        {u"voidItem"_s, {u"voidItem"_s, {}}}, {u"addComment"_s, {u"addComment"_s, {}}},
        {u"removePayment"_s, {u"removePayment"_s, {}}}, {u"closeCheck"_s, {u"closeCheck"_s, {}}},
        {u"printReceipt"_s, {u"printReceipt"_s, {}}}, {u"noSale"_s, {u"noSale"_s, {}}},
        {u"openDrawer"_s, {u"noSale"_s, {}}}, {u"openDrawerSession"_s, {u"openDrawerSession"_s, {}}},
        {u"countDrawer"_s, {u"countDrawer"_s, {}}}, {u"endOfDay"_s, {u"endOfDay"_s, {}}},
        {u"recallTicket"_s, {u"recallTicket"_s, {}}}, {u"cashOutTips"_s, {u"cashOutTips"_s, {}}},
        {u"clearText"_s, {u"textKey"_s, {u"clear"_s}}},
    };
    // Commands that carry arguments.
    if (pos_ && name == u"addTip")   // args.percent: 15, 18...; none = keypad amount
        return call(u"addTip"_s, {qint64(args.value(u"percent"_s).toDouble() * 100 + 0.5)},
                    [done](const QVariant &ok) { done(ok.toBool()); });
    if (pos_ && name == u"gratuity")
        return call(u"setGratuity"_s, {qint64(args.value(u"percent"_s).toDouble() * 100 + 0.5)},
                    [done](const QVariant &ok) { done(ok.toBool()); });
    if (pos_ && (name == u"payout" || name == u"paidIn"))
        return call(u"payout"_s, {name}, [done](const QVariant &ok) { done(ok.toBool()); });

    if (pos_) {
        if (const auto it = operations.constFind(name); it != operations.cend())
            return call(it->first, it->second, [done](const QVariant &ok) { done(ok.toBool()); });
    }

    // Unknown here: let the host application handle it.
    emit commandRequested(name, args);
    setStatus(tr("'%1' is not available yet").arg(name));
    done(true);
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
