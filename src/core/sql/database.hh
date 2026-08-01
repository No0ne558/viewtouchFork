/*
 * database.hh - RAII wrapper over a SQLite connection.
 *
 * This is the bottom of the new persistence layer. It owns the connection,
 * applies the pragmas the design depends on, and exposes just enough to run
 * statements and transactions. It deliberately does not know anything about
 * ViewTouch's data model -- repositories sit above it.
 *
 * Threading: one Database belongs to one thread. vt_main is the only process
 * that opens the database, and after the auto-save change it is the only thread
 * that writes. Reader connections for reporting get their own Database.
 */

#ifndef VT_SQL_DATABASE_HH
#define VT_SQL_DATABASE_HH

#include <string>
#include <string_view>

struct sqlite3;

namespace vt::sql {

// Result of a database operation. Mirrors the int-return convention used
// throughout ViewTouch rather than introducing exceptions at this boundary.
enum class Status
{
    Ok = 0,
    CannotOpen,
    Busy,
    Constraint,
    Corrupt,
    SqlError,
    NotOpen,
};

[[nodiscard]] const char *StatusName(Status status) noexcept;

class Database
{
public:
    Database() = default;
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    Database(Database &&other) noexcept;
    Database &operator=(Database &&other) noexcept;

    // Open (creating if absent) and apply the standard pragmas:
    //
    //   journal_mode = WAL   readers never block the writer, and a crash
    //                        recovers from the log rather than losing the tail
    //   foreign_keys = ON    off by default in SQLite; the schema's referential
    //                        integrity is worthless without it
    //   busy_timeout         wait rather than failing instantly on contention
    //   synchronous = NORMAL survives process crash on WAL; see SetSynchronousFull
    //                        for the durability barrier used at settle/end-of-day
    //
    // Pass ":memory:" for a temporary database, which is what the tests use.
    [[nodiscard]] Status Open(const std::string &path);
    void Close() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept { return db_ != nullptr; }

    // Raise durability for the operations that must not lose their last commit
    // even to a power cut -- settling a check, closing a day. NORMAL is the right
    // default elsewhere: it survives a process crash, and paying the full fsync
    // cost on every write would be felt on the SD cards these run on.
    [[nodiscard]] Status SetSynchronousFull(bool full);

    // Run one or more statements with no result rows.
    [[nodiscard]] Status Exec(std::string_view sql);

    // Read a single integer, e.g. a PRAGMA or a COUNT(*). Returns NotOpen or
    // SqlError without touching `out` on failure.
    [[nodiscard]] Status QueryInt(std::string_view sql, int64_t &out);

    [[nodiscard]] int64_t UserVersion();
    [[nodiscard]] Status SetUserVersion(int version);

    // Last error text from SQLite, for logging.
    [[nodiscard]] std::string LastError() const;

    // Escape hatch for the repository layer, which needs prepared statements.
    [[nodiscard]] sqlite3 *handle() const noexcept { return db_; }

private:
    sqlite3 *db_{nullptr};
};

// RAII transaction. Rolls back if it goes out of scope without a Commit, so an
// early return or a thrown exception cannot leave a half-applied change.
class Transaction
{
public:
    // Immediate acquires the write lock up front rather than on first write,
    // which avoids a busy failure partway through a multi-statement change.
    enum class Mode { Deferred, Immediate };

    explicit Transaction(Database &db, Mode mode = Mode::Immediate);
    ~Transaction();

    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    [[nodiscard]] Status Begin();
    [[nodiscard]] Status Commit();
    void Rollback() noexcept;

    [[nodiscard]] bool IsActive() const noexcept { return active_; }

private:
    Database &db_;
    Mode mode_;
    bool active_{false};
};

} // namespace vt::sql

#endif // VT_SQL_DATABASE_HH
