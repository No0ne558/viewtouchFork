#include "database.hh"

#include <sqlite3.h>

#include <string>
#include <utility>

namespace vt::sql {

namespace {

Status TranslateStatus(int rc) noexcept
{
    switch (rc)
    {
    case SQLITE_OK:
    case SQLITE_DONE:
    case SQLITE_ROW:
        return Status::Ok;
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        return Status::Busy;
    case SQLITE_CONSTRAINT:
        return Status::Constraint;
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB:
        return Status::Corrupt;
    case SQLITE_CANTOPEN:
        return Status::CannotOpen;
    default:
        return Status::SqlError;
    }
}

} // namespace

const char *StatusName(Status status) noexcept
{
    switch (status)
    {
    case Status::Ok:         return "ok";
    case Status::CannotOpen: return "cannot open";
    case Status::Busy:       return "busy";
    case Status::Constraint: return "constraint violation";
    case Status::Corrupt:    return "corrupt";
    case Status::SqlError:   return "sql error";
    case Status::NotOpen:    return "not open";
    }
    return "unknown";
}

Database::~Database()
{
    Close();
}

Database::Database(Database &&other) noexcept
    : db_(std::exchange(other.db_, nullptr))
{
}

Database &Database::operator=(Database &&other) noexcept
{
    if (this != &other)
    {
        Close();
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

Status Database::Open(const std::string &path)
{
    Close();

    const int rc = sqlite3_open_v2(
        path.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
        nullptr);

    if (rc != SQLITE_OK)
    {
        // sqlite3_open_v2 hands back a handle even on failure so the error can
        // be read from it; close it rather than leaking.
        Close();
        return TranslateStatus(rc);
    }

    // Wait rather than failing immediately if another connection holds the
    // write lock. Set before anything else so the pragmas below benefit too.
    sqlite3_busy_timeout(db_, 5000);

    // WAL is persistent in the database file, so this is a no-op after the
    // first open, but setting it every time keeps a hand-restored file honest.
    if (Status s = Exec("PRAGMA journal_mode=WAL;"); s != Status::Ok)
        return s;

    // Off by default in SQLite, per-connection, and not remembered. Every
    // connection has to ask, or the schema's foreign keys do nothing at all.
    if (Status s = Exec("PRAGMA foreign_keys=ON;"); s != Status::Ok)
        return s;

    if (Status s = Exec("PRAGMA synchronous=NORMAL;"); s != Status::Ok)
        return s;

    return Status::Ok;
}

void Database::Close() noexcept
{
    if (db_ != nullptr)
    {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

Status Database::SetSynchronousFull(bool full)
{
    return Exec(full ? "PRAGMA synchronous=FULL;" : "PRAGMA synchronous=NORMAL;");
}

Status Database::Exec(std::string_view sql)
{
    if (db_ == nullptr)
        return Status::NotOpen;

    const std::string statement(sql);
    char *error_text = nullptr;
    const int rc = sqlite3_exec(db_, statement.c_str(), nullptr, nullptr, &error_text);
    if (error_text != nullptr)
        sqlite3_free(error_text);

    return TranslateStatus(rc);
}

Status Database::QueryInt(std::string_view sql, int64_t &out)
{
    if (db_ == nullptr)
        return Status::NotOpen;

    const std::string statement(sql);
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, statement.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
    {
        sqlite3_finalize(stmt);
        return TranslateStatus(rc);
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
        out = sqlite3_column_int64(stmt, 0);

    const int final_rc = sqlite3_finalize(stmt);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE)
        return TranslateStatus(rc);
    return TranslateStatus(final_rc);
}

int64_t Database::UserVersion()
{
    int64_t version = 0;
    if (QueryInt("PRAGMA user_version;", version) != Status::Ok)
        return -1;
    return version;
}

Status Database::SetUserVersion(int version)
{
    // PRAGMA does not accept bound parameters, so this is built by hand. The
    // input is an int rather than anything caller-supplied, so there is nothing
    // to inject.
    return Exec("PRAGMA user_version=" + std::to_string(version) + ";");
}

std::string Database::LastError() const
{
    if (db_ == nullptr)
        return "database not open";
    const char *text = sqlite3_errmsg(db_);
    return (text != nullptr) ? std::string(text) : std::string("unknown error");
}

Transaction::Transaction(Database &db, Mode mode)
    : db_(db), mode_(mode)
{
}

Transaction::~Transaction()
{
    // An un-committed transaction here means an early return or an exception on
    // the way out. Rolling back is the only safe reading of that.
    Rollback();
}

Status Transaction::Begin()
{
    if (active_)
        return Status::Ok;

    const Status status = db_.Exec(mode_ == Mode::Immediate ? "BEGIN IMMEDIATE;"
                                                            : "BEGIN DEFERRED;");
    if (status == Status::Ok)
        active_ = true;
    return status;
}

Status Transaction::Commit()
{
    if (!active_)
        return Status::Ok;

    const Status status = db_.Exec("COMMIT;");
    if (status == Status::Ok)
    {
        active_ = false;
    }
    return status;
}

void Transaction::Rollback() noexcept
{
    if (!active_)
        return;

    active_ = false;
    (void)db_.Exec("ROLLBACK;");
}

} // namespace vt::sql
