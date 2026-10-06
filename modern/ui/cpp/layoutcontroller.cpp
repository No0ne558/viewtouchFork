#include "layoutcontroller.hh"

#include <QFile>
#include <QFontDatabase>
#include <QImage>

#include "app/looks.hh"
#include <QUrl>

#include <QCoreApplication>
#include <QEvent>

#include "core/employee.hh"
#include "core/settings.hh"
#include "layout/reflow.hh"
#include "reportexport.hh"
#include "storage/layout_store.hh"

#include <QPointer>

#include <QDir>
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
    updateMealPeriod();
    mealTimer_.setInterval(60 * 1000);
    connect(&mealTimer_, &QTimer::timeout, this, &LayoutController::updateMealPeriod);
    mealTimer_.start();
    idleTimer_.setSingleShot(true);
    connect(&idleTimer_, &QTimer::timeout, this, &LayoutController::idleTimeout);
    sleepTimer_.setSingleShot(true);
    connect(&sleepTimer_, &QTimer::timeout, this, &LayoutController::sleepTimeout);
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installEventFilter(this);
    nav_.reset(homePageOf(layout_));
    refresh();
}

bool LayoutController::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::TouchBegin:
    case QEvent::KeyPress:
    case QEvent::Wheel:
        restartIdle();
        if (!asleep_)   // asleep: the dim layer takes this touch and wakes it
            restartSleep();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void LayoutController::restartIdle()
{
    const int minutes = pos_ ? pos_->autoLogoutMinutes() : 0;
    const int ms = idleOverrideMs_ > 0 ? idleOverrideMs_ : minutes * 60 * 1000;
    if (ms <= 0 || !pos_ || !pos_->loggedIn()) {
        idleTimer_.stop();
        return;
    }
    idleTimer_.start(ms);
}

void LayoutController::restartSleep()
{
    const int minutes = pos_ ? pos_->screenSaverMinutes() : 0;
    const int ms = sleepOverrideMs_ > 0 ? sleepOverrideMs_ : minutes * 60 * 1000;
    if (ms <= 0) {
        sleepTimer_.stop();
        return;
    }
    sleepTimer_.start(ms);
}

void LayoutController::sleepTimeout()
{
    // Screens people watch stay on: kitchen, bar and expo; the kiosk has its own pictures.
    const vt::layout::Page *page = activeLayout().page(pageId());
    const bool watched = page && page->kind == u"kitchen";
    const bool kiosk = pos_ && pos_->selfOrderInfo().value(u"on"_s).toBool();
    if (watched || kiosk || editing()) {
        restartSleep();
        return;
    }
    asleep_ = true;
    emit asleepChanged();
}

void LayoutController::wake()
{
    if (!asleep_)
        return;
    asleep_ = false;
    emit asleepChanged();
    restartSleep();
}

void LayoutController::idleTimeout()
{
    // Not while a page is being edited (the editor guards its own changes).
    if (!pos_ || !pos_->loggedIn() || editing())
        return;
    pos_->invoke(u"logout"_s, {});
    setStatus(tr("Logged out after no use"));
}

LayoutController::~LayoutController()
{
    delete editor_.data();
}

QString LayoutController::mealPeriodAt(QTime time)
{
    return QString::fromStdString(vt::core::mealPeriodAt(vt::core::defaultMealPeriods(), time.msecsSinceStartOfDay() / 60000));
}

QString LayoutController::mealPeriodAt(const QVariantList &periods, QTime time)
{
    std::vector<vt::core::MealPeriod> list;
    for (const QVariant &v : periods) {
        const QVariantMap m = v.toMap();
        list.push_back({m.value(u"id"_s).toString().toStdString(), {}, m.value(u"start"_s).toInt()});
    }
    return QString::fromStdString(vt::core::mealPeriodAt(list, time.msecsSinceStartOfDay() / 60000));
}

void LayoutController::setMealPeriod(const QString &period)
{
    mealPeriodFixed_ = true;
    nav_.setMealPeriod(period);
    emit mealPeriodChanged();
}

void LayoutController::updateMealPeriod()
{
    if (mealPeriodFixed_)
        return;
    const QTime now = QTime::currentTime();
    const QString before = nav_.mealPeriod();
    nav_.setMealPeriod(pos_ ? mealPeriodAt(pos_->mealPeriods(), now) : mealPeriodAt(now));
    if (nav_.mealPeriod() != before)
        emit mealPeriodChanged();
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
        connect(pos_, &PosSession::adminChanged, this, [this] {
            installStoreFonts();   // before pages and font lists look for them
            updateMealPeriod();
            updateFormFactor();
            if (!asleep_)
                restartSleep();   // the setting may have changed
            refresh();   // sold-out marks
            if (editor_)
                editor_->setMealPeriods(pos_->mealPeriods());
        });
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
    updateMealPeriod();
    updateFormFactor();
    emit posChanged();
    refresh();
    installStoreFonts();
    restartSleep();
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

QVariantList LayoutController::pageChoices() const
{
    QVariantList out{QVariantMap{{u"value"_s, QString()}, {u"text"_s, QString()}}};
    for (const vt::layout::Page &p : activeLayout().pages)
        if (p.kind != u"template" && p.kind != u"library")
            out.append(QVariantMap{{u"value"_s, p.id}, {u"text"_s, p.name}});
    return out;
}

QString LayoutController::rolePage(const QString &role) const
{
    const vt::layout::Page *p = activeLayout().pageByRole(role);
    return p ? p->id : QString();
}

void LayoutController::onLoggedInChanged(bool loggedIn)
{
    restartIdle();
    // A new store: the setup guide, for the first manager in, until it's finished.
    if (!loggedIn)
        closeSetup();
    else if (pos_ && !editing() && pos_->can(QString::fromLatin1(vt::core::perm::Manager))
             && !pos_->setupInfo().value(u"done"_s).toBool())
        openSetup();
    // Logged in, "home" is their start page (theirs, or their job's), else the
    // floor (tables); logged out, it is the login page.
    const QString login = homePageOf(layout_);
    QString tables = rolePage(u"tables"_s);
    if (const QString start = pos_ ? pos_->userPrefs().value(u"startPage"_s).toString() : QString();
        loggedIn && !start.isEmpty() && activeLayout().page(start))
        tables = start;
    const QString home = loggedIn && !tables.isEmpty() ? tables : login;
    nav_.reset(home);
    if (editing_ && editor_)
        editor_->setPageId(nav_.current());
    refresh();
    emit pageChanged();
    // Back to the check they had open (Switch User): its order screen.
    if (loggedIn && pos_ && pos_->hasCheck() && !editing())
        navigate(Navigator::Mode::Index, {});
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
    return shown().canvas;
}

// --- screens of different sizes ----------------------------------------------------

LayoutController::Shown LayoutController::shown() const
{
    const Layout &l = activeLayout();
    const QString id = nav_.current();
    const vt::layout::Page *page = l.page(id);
    Shown s{id, page ? page->canvas : QSize(1920, 1080), {}, {}};
    // The editor always works on the page as designed.
    if (page && formFactor_ == u"phone" && !editing() && page->variantOf.isEmpty()) {
        if (const vt::layout::Page *v = l.variantFor(id, u"phone"_s)) {
            s.pageId = v->id;
            s.canvas = v->canvas;
            s.zones = l.effectiveZones(v->id);
            return s;
        }
        // A template with a phone version: it frames this page's own buttons.
        const QList<const vt::layout::Page *> chain = l.templateChain(id);
        for (qsizetype i = 1; i < chain.size(); ++i) {
            const vt::layout::Page *frame = l.variantFor(chain[i]->id, u"phone"_s);
            if (!frame || frame->contentArea.isEmpty())
                continue;
            s.pageId = frame->id;
            s.canvas = frame->canvas;
            s.zones = l.effectiveZones(frame->id);
            QList<Layout::PlacedZone> own;
            for (const Layout::PlacedZone &pz : l.effectiveZones(id)) {
                if (chain.indexOf(pz.owner) < i)   // this page, or templates below the framed one
                    own.append(pz);
            }
            QList<const vt::layout::Zone *> zones;
            for (const Layout::PlacedZone &pz : own)
                zones.append(pz.zone);
            const QList<QRect> rects = vt::layout::reflowZones(zones, frame->contentArea);
            for (qsizetype k = 0; k < own.size(); ++k) {
                if (rects[k].isEmpty())
                    continue;
                s.zones.append(own[k]);
                s.moved.insert(own[k].zone, rects[k]);
            }
            return s;
        }
    }
    s.zones = l.effectiveZones(id);
    return s;
}

void LayoutController::setFormFactorOverride(const QString &formFactor)
{
    formFactorOverride_ = formFactor == u"auto" ? QString() : formFactor;
    updateFormFactor();
}

void LayoutController::setAutoFormFactor(bool on)
{
    autoFormFactor_ = on;
    updateFormFactor();
}

void LayoutController::windowResized(qreal width, qreal height)
{
    phoneSizedWindow_ = width > 0 && height > 0 && std::min(width, height) < 600;
    updateFormFactor();
}

void LayoutController::updateFormFactor()
{
    QString mode = formFactorOverride_;
    if (mode.isEmpty() && pos_)
        mode = pos_->screenMode();   // Manager -> Terminals
    if (mode != u"phone" && mode != u"standard")
        mode = autoFormFactor_ && phoneSizedWindow_ ? u"phone"_s : u"standard"_s;
    if (mode == formFactor_)
        return;
    formFactor_ = mode;
    emit formFactorChanged();
    refresh();
    emit pageChanged();
}

void LayoutController::exportReport(const QVariantMap &report, const QString &format)
{
    const QString dir = exportDir_.isEmpty() ? QDir::home().filePath(u"ViewTouch Exports"_s) : exportDir_;
    QString error;
    const QString file = format == u"pdf" ? exportReportPdf(report, dir, &error) : exportReportCsv(report, dir, &error);
    setStatus(file.isEmpty() ? tr("Could not save the report: %1").arg(error) : tr("Saved to %1").arg(file));
}

void LayoutController::chooseLine(qint64 lineId)
{
    if (!pos_ || busy())
        return;
    call(u"chooseLine"_s, {lineId}, [this](const QVariant &ok) {
        const QString page = rolePage(u"modifiers"_s);
        if (ok.toBool() && !page.isEmpty() && nav_.current() != page)
            navigate(Navigator::Mode::Push, page);
    });
}

void LayoutController::finishChoosing()
{
    if (!pos_ || busy())
        return;
    call(u"finishChoosing"_s, {}, [this](const QVariant &ok) {
        if (ok.toBool())
            goBack();
    });
}

QVariantList LayoutController::tables() const
{
    QVariantList out;
    QSet<QString> seen;
    for (const vt::layout::Page &p : layout_.pages) {
        for (const vt::layout::Zone &z : p.zones) {
            const QString name = z.label.trimmed();
            if (z.kind != u"table" || name.isEmpty() || seen.contains(name.toLower()))
                continue;
            seen.insert(name.toLower());
            out.append(QVariantMap{{u"name"_s, name}, {u"seats"_s, z.props.value(u"seats"_s).toInt()}});
        }
    }
    return out;
}

int LayoutController::pageGrid() const
{
    const auto *p = currentPage();
    return p ? std::max(1, p->grid) : 8;
}

QVariantMap LayoutController::background() const
{
    return activeLayout().resolveBackground(shown().pageId).toVariantMap();
}

void LayoutController::activate(const QString &zoneId)
{
    if (editing())
        return;   // the editor overlay owns touches
    if (busy())
        return;   // still waiting for the server on the previous touch
    for (const Layout::PlacedZone &pz : shown().zones) {
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

// A zone's show/hide rules (its "showWhen"), as ZoneItem.qml applies them.
bool LayoutController::ruleShows(const QJsonObject &rule) const
{
    if (rule.isEmpty())
        return true;
    const QString login = rule.value(u"login").toString();
    const bool in = pos_ && pos_->loggedIn();
    if ((login == u"loggedIn" && !in) || (login == u"loggedOut" && in)
        || (login == u"manager" && !(pos_ && pos_->can(QString::fromLatin1(vt::core::perm::Manager)))))
        return false;
    const QString check = rule.value(u"check").toString();
    const bool open = pos_ && pos_->hasCheck();
    if ((check == u"open" && !open) || (check == u"none" && open))
        return false;
    const QString type = rule.value(u"checkType").toString();
    if (!type.isEmpty() && (!open || pos_->checkInfo().value(u"type"_s).toString() != type))
        return false;
    const QString period = rule.value(u"mealPeriod").toString();
    if (!period.isEmpty() && period != mealPeriod())
        return false;
    const QString screen = rule.value(u"screen").toString();
    return screen.isEmpty() || screen == formFactor();
}

bool LayoutController::triggerHotkey(const QString &key)
{
    if (key.isEmpty() || editing())
        return false;
    const auto zones = shown().zones;
    // Topmost zone wins, matching touch order.
    for (auto it = zones.rbegin(); it != zones.rend(); ++it) {
        if (it->zone->enabled && it->zone->hotkey.compare(key, Qt::CaseInsensitive) == 0
            && ruleShows(it->zone->extra.value(u"showWhen").toObject())) {
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
    if (pos_)
        editor_->setMealPeriods(pos_->mealPeriods());
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

void LayoutController::openSetup()
{
    if (!pos_ || !pos_->can(QString::fromLatin1(vt::core::perm::Manager))) {
        setStatus(tr("The setup guide is for managers."));
        return;
    }
    if (!setupOpen_) {
        setupOpen_ = true;
        emit setupOpenChanged();
    }
}

void LayoutController::closeSetup()
{
    if (setupOpen_) {
        setupOpen_ = false;
        emit setupOpenChanged();
    }
}

QStringList LayoutController::fontFamilies() const
{
    return QFontDatabase::families();
}

void LayoutController::installStoreFonts()
{
    if (!pos_)
        return;
    bool added = false;
    for (const QVariant &v : pos_->storeImages()) {
        const QVariantMap f = v.toMap();
        const QString hash = f.value(u"hash"_s).toString();
        if (f.value(u"kind"_s).toString() != u"font" || installedFonts_.contains(hash))
            continue;
        // Its file here (a paired screen gets it from the store first).
        const QString url = pos_->imageUrl(f.value(u"ref"_s).toString());
        if (url.isEmpty())
            continue;
        QFile file(QUrl(url).toLocalFile());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (QFontDatabase::addApplicationFontFromData(file.readAll()) >= 0) {
            installedFonts_.insert(hash);
            added = true;
        }
    }
    if (added)
        emit fontsChanged();
}

namespace {
QList<vt::app::Look> allLooks(vt::app::PosSession *pos)
{
    QList<vt::app::Look> looks = vt::app::builtInLooks();
    if (!pos)
        return looks;
    const QString url = pos->imageUrl(u"logo:"_s);
    if (url.isEmpty())
        return looks;
    const QList<QColor> colors = vt::app::mainColors(QImage(QUrl(url).toLocalFile()));
    if (!colors.isEmpty()) {
        looks.prepend(vt::app::lookFromColors(colors, false));
        looks.prepend(vt::app::lookFromColors(colors, true));
    }
    return looks;
}
} // namespace

QVariantList LayoutController::looks() const
{
    QVariantList out;
    for (const vt::app::Look &l : allLooks(pos_))
        out.append(QVariantMap{{u"id"_s, l.id}, {u"name"_s, l.name},
                               {u"colors"_s, QStringList{l.background.name(), l.surface.name(), l.panel.name(),
                                                         l.text.name(), l.accent.name()}}});
    return out;
}

bool LayoutController::applyLook(const QString &id)
{
    const QList<vt::app::Look> looks = allLooks(pos_);
    const auto it = std::ranges::find_if(looks, [&](const vt::app::Look &l) { return l.id == id; });
    if (it == looks.end())
        return false;
    if (editing_ && editor_) {
        vt::layout::Theme theme = editor_->editor().layout().theme;
        vt::app::applyLook(theme, *it);
        const bool ok = editor_->editor().setTheme(theme, tr("Look: %1").arg(it->name));
        if (ok)
            setStatus(tr("Look: %1 (Save keeps it)").arg(it->name));
        return ok;
    }
    if (pos_ && !pos_->can(QString::fromLatin1(vt::core::perm::EditLayout))) {
        setStatus(tr("Changing the look needs a manager."));
        return false;
    }
    Layout changed = layout_;
    vt::app::applyLook(changed.theme, *it);
    if (saver_) {
        QString error;
        if (!saver_(changed, &error)) {
            setStatus(tr("Could not save: %1").arg(error));
            return false;
        }
    }
    replaceLayout(changed);
    setStatus(tr("Look: %1").arg(it->name));
    return true;
}

void LayoutController::orderItem(const QString &itemId, bool clearTyped)
{
    if (editing() || busy())
        return;
    if (clearTyped && pos_)
        pos_->invoke(u"textKey"_s, {u"clear"_s});
    // As a button that adds it would: then its choices, or its weight.
    Action a;
    a.data = QJsonObject{{u"type"_s, u"addItem"_s}, {u"item"_s, itemId}};
    runAction(a, [](bool) {});
}

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
            } else if (pos_ && pos_->weighingInfo().value(u"active"_s).toBool()) {
                // Sold by weight: how much, on the Weigh page.
                if (const QString page = rolePage(u"weigh"_s); !page.isEmpty())
                    navigate(Navigator::Mode::Push, page);
            } else if (pos_ && pos_->choosingInfo().value(u"active"_s).toBool()) {
                // Its modifier groups: choose on the modifiers page.
                if (const QString page = rolePage(u"modifiers"_s); !page.isEmpty())
                    navigate(Navigator::Mode::Push, page);
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

    // Close ViewTouch: a manager's way off a kiosk screen (it then stays
    // closed until the next boot; see vtmodern-kiosk.service).
    if (name == u"closeApp") {
        if (pos_ && !pos_->can(QString::fromLatin1(vt::core::perm::Manager))) {
            setStatus(tr("Closing ViewTouch needs a manager."));
            return done(false);
        }
        done(true);
        emit closeRequested();
        return;
    }

    // Manager screens are pages ("admin-menu", "reports"...). Kept as a
    // command so buttons made before those pages existed still work.
    if (name == u"openAdmin") {
        static const QHash<QString, QString> pages = {
            {u"menu"_s, u"admin-menu"_s}, {u"employees"_s, u"admin-employees"_s},
            {u"tenders"_s, u"admin-tenders"_s}, {u"printers"_s, u"admin-printers"_s},
            {u"taxes"_s, u"admin-taxes"_s}, {u"settings"_s, u"admin-store"_s},
            {u"reports"_s, u"reports"_s}, {u"drawers"_s, u"drawer"_s}, {u"endOfDay"_s, u"end-of-day"_s},
            {u"terminals"_s, u"admin-terminals"_s}, {u"mealPeriods"_s, u"admin-meal-periods"_s},
            {u"modifierGroups"_s, u"admin-modifier-groups"_s}, {u"inventory"_s, u"admin-inventory"_s},
            {u"schedule"_s, u"admin-schedule"_s}, {u"promotions"_s, u"admin-promotions"_s},
            {u"vendors"_s, u"admin-vendors"_s},
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
        {u"openTab"_s, {u"openTab"_s, {}}},
        {u"startDelivery"_s, {u"startCheck"_s, {u"delivery"_s}}},
        {u"releaseCheck"_s, {u"releaseCheck"_s, {}}}, {u"newTableCheck"_s, {u"newTableCheck"_s, {}}},
        {u"splitBySeat"_s, {u"splitBySeat"_s, {}}}, {u"printTableChecks"_s, {u"printTableChecks"_s, {}}},
        {u"combineTableChecks"_s, {u"combineTableChecks"_s, {}}}, {u"sendOrder"_s, {u"sendOrder"_s, {}}},
        {u"voidItem"_s, {u"voidItem"_s, {}}}, {u"lineMore"_s, {u"lineMore"_s, {0}}},
        {u"lineLess"_s, {u"lineLess"_s, {0}}}, {u"repeatLine"_s, {u"repeatLine"_s, {0}}},
        {u"undoLast"_s, {u"undoLast"_s, {}}}, {u"anotherRound"_s, {u"anotherRound"_s, {}}}, {u"addComment"_s, {u"addComment"_s, {}}},
        {u"removePayment"_s, {u"removePayment"_s, {}}}, {u"closeCheck"_s, {u"closeCheck"_s, {}}},
        {u"printReceipt"_s, {u"printReceipt"_s, {}}}, {u"noSale"_s, {u"noSale"_s, {}}},
        {u"openDrawer"_s, {u"noSale"_s, {}}}, {u"openDrawerSession"_s, {u"openDrawerSession"_s, {}}},
        {u"countDrawer"_s, {u"countDrawer"_s, {}}}, {u"endOfDay"_s, {u"endOfDay"_s, {}}},
        {u"recallTicket"_s, {u"recallTicket"_s, {}}}, {u"cashOutTips"_s, {u"cashOutTips"_s, {}}},
        {u"clearText"_s, {u"textKey"_s, {u"clear"_s}}},
        {u"backupNow"_s, {u"backupNow"_s, {}}},
        {u"askForTip"_s, {u"askForTip"_s, {}}},
        {u"toggleTraining"_s, {u"toggleTraining"_s, {}}},
        {u"selfOrder"_s, {u"setSelfOrder"_s, {true}}},
        {u"rush"_s, {u"toggleFlag"_s, {u"rush"_s}}}, {u"vip"_s, {u"toggleFlag"_s, {u"vip"_s}}},
        {u"startBreak"_s, {u"toggleBreak"_s, {}}}, {u"toggleBreak"_s, {u"toggleBreak"_s, {}}},
        // What widgets' own buttons do, for buttons placed anywhere.
        {u"expoRecall"_s, {u"expoRecall"_s, {}}}, {u"fireCourse"_s, {u"fireCourse"_s, {}}},
        {u"guestsMore"_s, {u"adjustGuests"_s, {1}}}, {u"guestsFewer"_s, {u"adjustGuests"_s, {-1}}},
    };
    // Widgets' own buttons that change the screen, not the store: the widget does them.
    if (name == u"kitchenStation" || name == u"kitchenAllDay") {
        emit widgetCommand(name, args);
        return done(true);
    }
    if (name == u"setupGuide") {
        openSetup();
        return done(true);
    }
    if (name == u"finishChoosing") {
        finishChoosing();
        return done(true);
    }
    if (pos_ && (name == u"addWeighed" || name == u"cancelWeighing"))
        return call(name, {}, [this, done, name](const QVariant &ok) {
            if (ok.toBool() || name == u"cancelWeighing")
                goBack();
            done(ok.toBool());
        });
    if (pos_ && name == u"cancelChoosing")
        return call(u"cancelChoosing"_s, {}, [this, done](const QVariant &ok) {
            goBack();
            done(ok.toBool());
        });
    if (pos_ && (name == u"seatNext" || name == u"seatPrev")) {
        const int seat = pos_->checkInfo().value(u"seat"_s).toInt();
        return call(u"setSeat"_s, {std::max(0, seat + (name == u"seatNext" ? 1 : -1))},
                    [done](const QVariant &ok) { done(ok.toBool()); });
    }
    if (pos_ && name == u"courseNext") {   // 1, 2, 3 and around
        const int course = std::max(1, pos_->checkInfo().value(u"course"_s).toInt());
        return call(u"setCourse"_s, {course % 3 + 1}, [done](const QVariant &ok) { done(ok.toBool()); });
    }
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

    const Shown s = shown();
    // Left-handed: on the order and Pay screens the check's column moves to the
    // right and what was right of it moves left, each row keeping its order.
    // What's above or below the check (the bottom buttons) stays.
    int colLeft = -1, colRight = -1, nextLeft = -1, colTop = 0, colBottom = 0;
    if (!editing() && pos_ && page && pos_->userPrefs().value(u"leftHanded"_s).toBool()
        && (page->kind == u"index" || page->kind == u"items" || page->kind == u"modifier" || page->kind == u"settle")) {
        for (const Layout::PlacedZone &pz : s.zones)
            if ((pz.zone->kind == u"orderList" || pz.zone->kind == u"paymentPanel") && !s.moved.contains(pz.zone)) {
                colLeft = pz.zone->rect.x();
                colRight = pz.zone->rect.x() + pz.zone->rect.width();
                colTop = pz.zone->rect.y();
                colBottom = pz.zone->rect.y() + pz.zone->rect.height();
                break;
            }
        for (const Layout::PlacedZone &pz : s.zones)
            if (colRight >= 0 && pz.zone->rect.x() >= colRight && pz.zone->rect.y() >= colTop
                && pz.zone->rect.y() + pz.zone->rect.height() <= colBottom && (nextLeft < 0 || pz.zone->rect.x() < nextLeft))
                nextLeft = pz.zone->rect.x();
    }
    const int canvasW = page ? page->canvas.width() : 1920;
    QList<ZoneModel::Row> rows;
    for (const Layout::PlacedZone &pz : s.zones) {
        const vt::layout::Zone &z = *pz.zone;
        // Laid out again for a phone: its own rect there, a plain shape, and
        // the look it has on its own page.
        const bool moved = s.moved.contains(pz.zone);
        QRect rect = moved ? s.moved.value(pz.zone) : z.rect;
        if (nextLeft >= 0 && !moved && rect.y() >= colTop && rect.y() + rect.height() <= colBottom) {
            if (rect.x() >= colLeft && rect.x() + rect.width() <= colRight)
                rect.translate(canvasW - colLeft - colRight, 0);          // the check's column: right
            else if (rect.x() >= colRight)
                rect.translate(colLeft - nextLeft, 0);                    // the rest: left
        }
        const QString shape = moved && z.shape != u"rect" ? u"rounded"_s : z.shape;
        const QString stylePage = moved ? pageId : s.pageId;
        // A button that orders an 86'd item: marked, and touching it does nothing.
        bool soldOut = false;
        if (pos_ && !editing() && !z.actions.isEmpty() && z.actions.first().type() == u"addItem") {
            const QString item = z.actions.first().str(u"item");
            const QStringList out = pos_->soldOut();
            soldOut = out.contains(item) || out.contains(item.toLower());
        }
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
            {ZoneModel::ZoneXRole, rect.x()},
            {ZoneModel::ZoneYRole, rect.y()},
            {ZoneModel::ZoneWRole, rect.width()},
            {ZoneModel::ZoneHRole, rect.height()},
            {ZoneModel::ShapeRole, shape},
            {ZoneModel::BehaviorRole, z.behavior},
            {ZoneModel::ZoneEnabledRole, z.enabled && !soldOut},
            {ZoneModel::InheritedRole, pz.inherited},
            {ZoneModel::CurrentRole, current},
            {ZoneModel::HotkeyRole, z.hotkey},
            {ZoneModel::GroupRole, z.group},
            {ZoneModel::ImagePathRole, z.imagePath},
            {ZoneModel::StyleNormalRole, l.resolveStyle(z, stylePage, ZoneState::Normal).toVariantMap()},
            {ZoneModel::StyleSelectedRole, l.resolveStyle(z, stylePage, ZoneState::Selected).toVariantMap()},
            {ZoneModel::StyleDisabledRole, l.resolveStyle(z, stylePage, ZoneState::Disabled).toVariantMap()},
            {ZoneModel::PropsRole, z.props.toVariantMap()},
            {ZoneModel::SoldOutRole, soldOut},
            {ZoneModel::ShowWhenRole, z.extra.value(u"showWhen").toObject().toVariantMap()},
        });
    }
    zones_.setRows(std::move(rows));
    if (const QVariantMap colors = l.theme.extra.value(u"status").toObject().toVariantMap(); colors != statusColors_) {
        statusColors_ = colors;
        emit statusColorsChanged();
    }
}

void LayoutController::setStatus(const QString &text)
{
    status_ = text;
    emit statusChanged();
}
