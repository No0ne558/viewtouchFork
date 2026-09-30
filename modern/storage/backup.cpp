#include "storage/backup.hh"

#include <QAtomicInt>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcBackup, "vt.backup")

namespace vt::storage {

namespace {

QString nextConnectionName()
{
    static QAtomicInt counter;
    return u"vt-backup-%1"_s.arg(counter.fetchAndAddRelaxed(1));
}

void setError(QString *error, const QString &text)
{
    if (error)
        *error = text;
}

// Runs `work` with a connection of its own to `file`, then drops it.
template <typename Work>
bool withConnection(const QString &file, bool readOnly, QString *error, Work work)
{
    const QString name = nextConnectionName();
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, name);
        db.setDatabaseName(file);
        if (readOnly)
            db.setConnectOptions(u"QSQLITE_OPEN_READONLY"_s);
        if (!db.open()) {
            setError(error, db.lastError().text());
        } else {
            QSqlQuery q(db);
            q.exec(u"PRAGMA busy_timeout=5000"_s);
            ok = work(q);
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

bool moveAside(const QString &from, const QString &to)
{
    return !QFile::exists(from) || QFile::rename(from, to);
}

// A restore run with sudo must leave the database to the account the POS
// runs as: give `file` the owner of `like` (the old database, or the folder).
void takeOwnership(const QString &file, const QString &like)
{
#ifdef Q_OS_UNIX
    const QFileInfo ref(like);
    if (ref.exists() && ::geteuid() == 0) {
        if (::chown(QFile::encodeName(file).constData(), ref.ownerId(), ref.groupId()) != 0)
            qCWarning(lcBackup).noquote() << "Could not hand" << file << "to its owner";
    }
#else
    Q_UNUSED(file)
    Q_UNUSED(like)
#endif
}

} // namespace

QString backupFileName(const QDateTime &when)
{
    return u"viewtouch-%1.db"_s.arg(when.toString(u"yyyyMMdd-HHmmss"_s));
}

bool backupDatabase(const QString &db, const QString &target, QString *error)
{
    if (!QFile::exists(db)) {
        setError(error, u"There is no database at %1."_s.arg(db));
        return false;
    }
    if (QFile::exists(target)) {
        setError(error, u"%1 already exists."_s.arg(target));
        return false;
    }
    const QString part = target + u".part"_s;
    QFile::remove(part);
    const bool copied = withConnection(db, false, error, [&](QSqlQuery &q) {
        q.prepare(u"VACUUM INTO ?"_s);
        q.addBindValue(part);
        if (!q.exec()) {
            setError(error, q.lastError().text());
            return false;
        }
        return true;
    });
    if (!copied || !verifyDatabase(part, error) || !QFile::rename(part, target)) {
        if (copied && error && error->isEmpty())
            *error = u"Could not rename %1."_s.arg(part);
        QFile::remove(part);
        return false;
    }
    return true;
}

bool verifyDatabase(const QString &file, QString *error)
{
    if (!QFileInfo(file).isFile()) {
        setError(error, u"%1 is not a file."_s.arg(file));
        return false;
    }
    return withConnection(file, true, error, [&](QSqlQuery &q) {
        if (!q.exec(u"PRAGMA quick_check"_s) || !q.next() || q.value(0).toString() != u"ok") {
            setError(error, u"%1 is damaged or not a database."_s.arg(QFileInfo(file).fileName()));
            return false;
        }
        int tables = 0;
        if (q.exec(u"SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name IN ('meta', 'layout_pages', 'settings')"_s)
            && q.next())
            tables = q.value(0).toInt();
        if (tables < 3) {
            setError(error, u"%1 is not a ViewTouch database."_s.arg(QFileInfo(file).fileName()));
            return false;
        }
        return true;
    });
}

bool restoreDatabase(const QString &backup, const QString &db, QString *keptAs, QString *error)
{
    if (!verifyDatabase(backup, error))
        return false;
    QString kept;
    if (QFile::exists(db)) {
        kept = db + u".before-restore-"_s + QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s);
        // The -wal file can hold the latest sales: it moves with the database.
        if (!moveAside(db, kept) || !moveAside(db + u"-wal"_s, kept + u"-wal"_s)
            || !moveAside(db + u"-shm"_s, kept + u"-shm"_s)) {
            setError(error, u"Could not move the current database aside."_s);
            return false;
        }
    }
    if (!QFile::copy(backup, db)) {
        setError(error, u"Could not copy %1 to %2."_s.arg(backup, db));
        if (!kept.isEmpty()) {   // put the old one back
            QFile::rename(kept, db);
            QFile::rename(kept + u"-wal"_s, db + u"-wal"_s);
            QFile::rename(kept + u"-shm"_s, db + u"-shm"_s);
        }
        return false;
    }
    QFile(db).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup
                             | QFileDevice::WriteGroup);
    takeOwnership(db, !kept.isEmpty() ? kept : QFileInfo(db).absolutePath());
    if (keptAs)
        *keptAs = kept;
    return true;
}

QStringList listBackups(const QString &dir)
{
    QStringList out;
    const QFileInfoList files = QDir(dir).entryInfoList({u"viewtouch-*.db"_s}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &f : files)
        out.append(f.absoluteFilePath());
    return out;
}

QStringList pruneBackups(const QString &dir, int keep)
{
    QStringList removed;
    if (keep <= 0)
        return removed;
    const QStringList all = listBackups(dir);
    for (qsizetype i = keep; i < all.size(); ++i) {
        if (QFile::remove(all[i]))
            removed.append(all[i]);
    }
    return removed;
}

// --- scheduler -----------------------------------------------------------------------

BackupScheduler::BackupScheduler(QString db, QString dir, int keep, int everyHours, QObject *parent)
    : QObject(parent)
    , db_(std::move(db))
    , dir_(std::move(dir))
    , keep_(keep)
    , everyHours_(everyHours)
{
    timer_.setInterval(60 * 60 * 1000);
    connect(&timer_, &QTimer::timeout, this, &BackupScheduler::check);
}

BackupScheduler::~BackupScheduler()
{
    pool_.waitForDone();
}

void BackupScheduler::start()
{
    if (everyHours_ <= 0)
        return;
    timer_.start();
    check();
}

void BackupScheduler::check()
{
    const QStringList existing = listBackups(dir_);
    const QDateTime due = QDateTime::currentDateTime().addSecs(-qint64(everyHours_) * 3600);
    if (existing.isEmpty() || QFileInfo(existing.first()).lastModified() <= due)
        backupNow();
}

void BackupScheduler::backupNow()
{
    if (running_)
        return;
    running_ = true;
    pool_.start([this, db = db_, dir = dir_, keep = keep_] {
        QString error;
        QString target;
        bool ok = QDir().mkpath(dir);
        if (!ok) {
            error = u"Cannot create %1."_s.arg(dir);
        } else {
            const QDateTime now = QDateTime::currentDateTime();
            target = QDir(dir).filePath(backupFileName(now));
            for (int n = 2; QFile::exists(target); ++n)
                target = QDir(dir).filePath(backupFileName(now).replace(u".db"_s, u"-%1.db"_s.arg(n)));
            ok = backupDatabase(db, target, &error);
            if (ok)
                pruneBackups(dir, keep);
        }
        QMetaObject::invokeMethod(this, [this, ok, target, error] {
            running_ = false;
            if (ok)
                qCInfo(lcBackup).noquote() << "Backed up the database to" << target;
            else
                qCWarning(lcBackup).noquote() << "Backup failed:" << error;
            emit finished(ok, target, error);
        }, Qt::QueuedConnection);
    });
}

} // namespace vt::storage
