#include "sequence.hh"

#include "statement.hh"

namespace vt::sql {

Status NextSequenceValue(Database &db, std::string_view name, int64_t &out)
{
    // One statement: increment and read back what was claimed. Splitting this
    // into an UPDATE followed by a SELECT would leave a window in which a second
    // writer could claim the same value -- the exact failure the legacy counter
    // had, reintroduced.
    Statement stmt;
    if (Status s = stmt.Prepare(db,
            "UPDATE sequence SET next_value = next_value + 1 "
            "WHERE name = ?1 RETURNING next_value;");
        s != Status::Ok)
    {
        return s;
    }

    if (Status s = stmt.BindText(1, name); s != Status::Ok)
        return s;

    Status status = Status::Ok;
    if (!stmt.Step(status))
    {
        // No row means the sequence does not exist. Silently creating one would
        // hand back 1 and collide with everything already allocated.
        return (status == Status::Ok) ? Status::SqlError : status;
    }

    // RETURNING gives the value after the increment, which is the one claimed.
    out = stmt.ColumnInt(0);
    return Status::Ok;
}

Status PeekSequenceValue(Database &db, std::string_view name, int64_t &out)
{
    Statement stmt;
    if (Status s = stmt.Prepare(db,
            "SELECT next_value FROM sequence WHERE name = ?1;");
        s != Status::Ok)
    {
        return s;
    }

    if (Status s = stmt.BindText(1, name); s != Status::Ok)
        return s;

    Status status = Status::Ok;
    if (!stmt.Step(status))
        return (status == Status::Ok) ? Status::SqlError : status;

    out = stmt.ColumnInt(0);
    return Status::Ok;
}

Status RaiseSequenceTo(Database &db, std::string_view name, int64_t minimum_next)
{
    // max() rather than a plain assignment: an importer processing days out of
    // order, or re-run over a database that already holds newer data, must never
    // move a sequence backwards. Going backwards is what produces duplicates.
    Statement stmt;
    if (Status s = stmt.Prepare(db,
            "UPDATE sequence SET next_value = max(next_value, ?2) WHERE name = ?1;");
        s != Status::Ok)
    {
        return s;
    }

    if (Status s = stmt.BindText(1, name); s != Status::Ok)
        return s;
    if (Status s = stmt.BindInt(2, minimum_next); s != Status::Ok)
        return s;

    return stmt.Execute();
}

} // namespace vt::sql
