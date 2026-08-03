/*
 * statement.hh - Prepared statement wrapper.
 *
 * Every repository needs to bind parameters and read columns; doing that
 * against the raw C API at each call site is where SQL layers accumulate leaks
 * and unchecked return codes. This owns the sqlite3_stmt, finalises it in the
 * destructor, and makes binding positional and type-checked.
 *
 * Parameters are always bound, never interpolated. Beyond the injection
 * argument, binding is what keeps money and ids as exact integers rather than
 * round-tripping them through decimal text.
 */

#ifndef VT_SQL_STATEMENT_HH
#define VT_SQL_STATEMENT_HH

#include "database.hh"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3_stmt;

namespace vt::sql {

class Statement
{
public:
    Statement() = default;
    ~Statement();

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    Statement(Statement &&other) noexcept;
    Statement &operator=(Statement &&other) noexcept;

    [[nodiscard]] Status Prepare(Database &db, std::string_view sql);

    // Parameter indexes are 1-based, matching SQLite.
    [[nodiscard]] Status BindInt(int index, int64_t value);
    // For the tax rates only. Money is INTEGER cents throughout; rates are Flt
    // (double) in Settings and Archive and REAL in the schema, so binding one
    // as text would round-trip it through decimal and move the third decimal
    // place of a rate that multiplies every sale on a day.
    [[nodiscard]] Status BindDouble(int index, double value);
    [[nodiscard]] Status BindText(int index, std::string_view value);
    [[nodiscard]] Status BindNull(int index);
    // Binds the value, or NULL when the optional is empty. Used for the many
    // nullable time and id columns, where "absent" is meaningfully different
    // from zero -- a distinction the legacy format could not express.
    [[nodiscard]] Status BindOptionalInt(int index, std::optional<int64_t> value);

    // Advance one row. Returns true while a row is available, false at the end.
    // `status` reports why iteration stopped.
    [[nodiscard]] bool Step(Status &status);

    // Run a statement expected to produce no rows.
    [[nodiscard]] Status Execute();

    // Column accessors, 0-based, valid only after Step() returned true.
    [[nodiscard]] int64_t ColumnInt(int index) const;
    [[nodiscard]] double ColumnDouble(int index) const;
    [[nodiscard]] std::string ColumnText(int index) const;
    [[nodiscard]] bool ColumnIsNull(int index) const;
    [[nodiscard]] std::optional<int64_t> ColumnOptionalInt(int index) const;

    // Reset for reuse with new bindings, which is what makes a per-row insert
    // loop cheap rather than re-parsing the SQL each time.
    [[nodiscard]] Status Reset();

    [[nodiscard]] bool IsPrepared() const noexcept { return stmt_ != nullptr; }

private:
    void Finalize() noexcept;

    sqlite3_stmt *stmt_{nullptr};
};

} // namespace vt::sql

#endif // VT_SQL_STATEMENT_HH
