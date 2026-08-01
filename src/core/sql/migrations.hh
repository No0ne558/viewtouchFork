/*
 * migrations.hh - Ordered schema migrations.
 *
 * Replaces the positional whole-file version integers the legacy format used
 * (CHECK_VERSION 25, SETTINGS_VERSION 107, ARCHIVE_VERSION 14), where adding a
 * field meant appending to a byte stream and teaching every reader a new
 * `if (version >= N)` branch.
 *
 * Two records are kept, deliberately:
 *
 *   PRAGMA user_version   a single integer SQLite stores in the file header.
 *                         Cheap to read, so it is the gate on startup.
 *   schema_migrations     one row per applied migration with a checksum and a
 *                         timestamp. This is the audit trail, and it is what
 *                         catches a migration whose text changed after it had
 *                         already been applied somewhere.
 *
 * Every migration runs inside a transaction together with its bookkeeping, so a
 * migration cannot be recorded as applied unless its DDL committed.
 */

#ifndef VT_SQL_MIGRATIONS_HH
#define VT_SQL_MIGRATIONS_HH

#include "database.hh"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vt::sql {

struct Migration
{
    int version;                 // strictly increasing, starting at 1
    std::string_view description;
    std::string_view up;         // DDL applied to move to `version`
};

// The compiled-in migration list, in ascending version order.
[[nodiscard]] const std::vector<Migration> &AllMigrations();

// Highest version in AllMigrations(), i.e. what a fully migrated database reads.
[[nodiscard]] int LatestSchemaVersion();

struct MigrationResult
{
    Status status{Status::Ok};
    int applied_count{0};
    int final_version{0};
    std::string error;           // populated when status != Ok
};

// Apply every migration newer than the database's current user_version.
// Idempotent: running it against an up-to-date database applies nothing.
//
// Refuses to run against a database whose user_version is *newer* than this
// build knows about, rather than guessing -- that means an older binary has
// been pointed at data written by a newer one, and continuing would corrupt it.
[[nodiscard]] MigrationResult MigrateToLatest(Database &db);

// Checksum used in schema_migrations. Not cryptographic: its only job is to
// notice that a migration's text changed after it shipped.
[[nodiscard]] std::string MigrationChecksum(std::string_view text);

} // namespace vt::sql

#endif // VT_SQL_MIGRATIONS_HH
