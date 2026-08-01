#include "statement.hh"

#include <sqlite3.h>

#include <string>
#include <utility>

namespace vt::sql {

namespace {

Status Translate(int rc) noexcept
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
    default:
        return Status::SqlError;
    }
}

} // namespace

Statement::~Statement()
{
    Finalize();
}

Statement::Statement(Statement &&other) noexcept
    : stmt_(std::exchange(other.stmt_, nullptr))
{
}

Statement &Statement::operator=(Statement &&other) noexcept
{
    if (this != &other)
    {
        Finalize();
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

void Statement::Finalize() noexcept
{
    if (stmt_ != nullptr)
    {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
}

Status Statement::Prepare(Database &db, std::string_view sql)
{
    Finalize();

    if (!db.IsOpen())
        return Status::NotOpen;

    const std::string text(sql);
    const int rc = sqlite3_prepare_v2(db.handle(), text.c_str(), -1, &stmt_, nullptr);
    if (rc != SQLITE_OK)
    {
        Finalize();
        return Translate(rc);
    }
    return Status::Ok;
}

Status Statement::BindInt(int index, int64_t value)
{
    if (stmt_ == nullptr)
        return Status::NotOpen;
    return Translate(sqlite3_bind_int64(stmt_, index, value));
}

Status Statement::BindText(int index, std::string_view value)
{
    if (stmt_ == nullptr)
        return Status::NotOpen;
    // SQLITE_TRANSIENT: SQLite copies the bytes, so the caller's buffer does not
    // have to outlive the statement. The alternative saves a copy and is a
    // reliable source of use-after-free.
    return Translate(sqlite3_bind_text(stmt_, index, value.data(),
                                       static_cast<int>(value.size()),
                                       SQLITE_TRANSIENT));
}

Status Statement::BindNull(int index)
{
    if (stmt_ == nullptr)
        return Status::NotOpen;
    return Translate(sqlite3_bind_null(stmt_, index));
}

Status Statement::BindOptionalInt(int index, std::optional<int64_t> value)
{
    return value.has_value() ? BindInt(index, *value) : BindNull(index);
}

bool Statement::Step(Status &status)
{
    if (stmt_ == nullptr)
    {
        status = Status::NotOpen;
        return false;
    }

    const int rc = sqlite3_step(stmt_);
    status = Translate(rc);
    return rc == SQLITE_ROW;
}

Status Statement::Execute()
{
    Status status = Status::Ok;
    while (Step(status))
    {
        // A statement expected to be row-free may still return rows (RETURNING,
        // for one). Draining rather than asserting keeps this usable for both.
    }
    return status;
}

int64_t Statement::ColumnInt(int index) const
{
    return (stmt_ != nullptr) ? sqlite3_column_int64(stmt_, index) : 0;
}

std::string Statement::ColumnText(int index) const
{
    if (stmt_ == nullptr)
        return {};
    const auto *text = sqlite3_column_text(stmt_, index);
    if (text == nullptr)
        return {};
    const int bytes = sqlite3_column_bytes(stmt_, index);
    return std::string(reinterpret_cast<const char *>(text),
                       static_cast<std::size_t>(bytes));
}

bool Statement::ColumnIsNull(int index) const
{
    return (stmt_ == nullptr) ||
           sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

std::optional<int64_t> Statement::ColumnOptionalInt(int index) const
{
    if (ColumnIsNull(index))
        return std::nullopt;
    return ColumnInt(index);
}

Status Statement::Reset()
{
    if (stmt_ == nullptr)
        return Status::NotOpen;

    // sqlite3_reset reports the error from the *previous* execution, which is
    // not what a caller resetting for reuse is asking about; clear_bindings has
    // no such quirk. Both are called, and only the binding result is returned.
    sqlite3_reset(stmt_);
    return Translate(sqlite3_clear_bindings(stmt_));
}

} // namespace vt::sql
