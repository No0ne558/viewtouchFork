#include "storage/async_writer.hh"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace vt::storage {

namespace {
constexpr int kRetryMs = 2000;
}

// Lives on the writer thread and owns that thread's SQLite connection.
class AsyncWriter::Worker : public QObject {
public:
    explicit Worker(QString path)
        : path_(std::move(path))
        , connection_(u"vt-writer-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
    }

    QSqlDatabase db(QString *error)
    {
        QSqlDatabase db = QSqlDatabase::database(connection_, false);
        if (db.isValid() && db.isOpen())
            return db;
        db = QSqlDatabase::addDatabase(u"QSQLITE"_s, connection_);
        db.setDatabaseName(path_);
        if (!db.open()) {
            *error = db.lastError().text();
            return db;
        }
        QSqlQuery q(db);
        q.exec(u"PRAGMA journal_mode=WAL"_s);
        q.exec(u"PRAGMA synchronous=NORMAL"_s);
        q.exec(u"PRAGMA busy_timeout=5000"_s);
        return db;
    }

    void close()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(connection_, false);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(connection_);
    }

private:
    QString path_;
    QString connection_;
};

AsyncWriter::AsyncWriter(QString databasePath, QObject *parent)
    : QObject(parent)
    , path_(std::move(databasePath))
{
    thread_.setObjectName(u"vt-db-writer"_s);
    worker_ = new Worker(path_);
    worker_->moveToThread(&thread_);
    thread_.start();
}

AsyncWriter::~AsyncWriter()
{
    flush();
    QMetaObject::invokeMethod(worker_, [w = worker_] { w->close(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
    delete worker_;
}

void AsyncWriter::upsert(const QString &table, const QString &key, const QVariantMap &row)
{
    QMutexLocker lock(&mutex_);
    const QString id = table + u'\x1f' + key;
    if (auto it = index_.find(id); it != index_.end()) {
        queue_[*it].values = row;   // newest wins, keeps its place in line
    } else {
        index_.insert(id, queue_.size());
        queue_.append({table, key, row});
    }
    if (!scheduled_) {
        scheduled_ = true;
        QMetaObject::invokeMethod(worker_, [this] { drain(); }, Qt::QueuedConnection);
    }
}

void AsyncWriter::flush()
{
    QMetaObject::invokeMethod(worker_, [this] { drain(); }, Qt::BlockingQueuedConnection);
}

int AsyncWriter::pending() const
{
    QMutexLocker lock(&mutex_);
    return int(queue_.size());
}

void AsyncWriter::drain()
{
    QList<Row> batch;
    {
        QMutexLocker lock(&mutex_);
        scheduled_ = false;
        batch.swap(queue_);
        index_.clear();
    }
    if (batch.isEmpty())
        return;

    QString error;
    QSqlDatabase db = worker_->db(&error);
    bool ok = error.isEmpty() && db.transaction();
    if (ok) {
        QSqlQuery q(db);
        for (const Row &row : std::as_const(batch)) {
            const QStringList cols = row.values.keys();
            QStringList marks;
            marks.fill(u"?"_s, cols.size());
            q.prepare(u"INSERT OR REPLACE INTO %1 (%2) VALUES (%3)"_s.arg(row.table, cols.join(u','), marks.join(u',')));
            for (const QString &c : cols)
                q.addBindValue(row.values.value(c));
            if (!q.exec()) {
                error = u"%1: %2"_s.arg(row.table, q.lastError().text());
                ok = false;
                break;
            }
        }
        if (ok && !db.commit()) {
            error = db.lastError().text();
            ok = false;
        }
        if (!ok)
            db.rollback();
    } else if (error.isEmpty()) {
        error = db.lastError().text();
    }

    if (ok)
        return;

    // Put the batch back in front of anything queued meanwhile, unless a
    // newer version of the same row has arrived.
    {
        QMutexLocker lock(&mutex_);
        QList<Row> merged;
        QHash<QString, qsizetype> index;
        for (const Row &row : std::as_const(batch)) {
            const QString id = row.table + u'\x1f' + row.key;
            if (index_.contains(id))
                continue;
            index.insert(id, merged.size());
            merged.append(row);
        }
        for (const Row &row : std::as_const(queue_)) {
            index.insert(row.table + u'\x1f' + row.key, merged.size());
            merged.append(row);
        }
        queue_ = merged;
        index_ = index;
        if (!retryPending_) {
            retryPending_ = true;
            QTimer::singleShot(kRetryMs, worker_, [this] {
                {
                    QMutexLocker lock(&mutex_);
                    retryPending_ = false;
                }
                drain();
            });
        }
    }
    emit writeFailed(error);   // queued to the owner's thread
}

} // namespace vt::storage
