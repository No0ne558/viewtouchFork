#include "migrations.hh"

#include <sqlite3.h>

#include <cstdio>
#include <string>

namespace vt::sql {

namespace {

// ---------------------------------------------------------------------------
// 0001 - core schema
//
// Scope is deliberately the spine rather than the whole model: the bookkeeping
// tables, the business-day container that replaces both `archive/` and
// `current/`, the frozen tax policy that makes historical totals reproducible,
// and the lookup tables that report labels come from. Checks, subchecks, orders
// and payments land in the next migration, on top of this.
//
// Conventions, applied throughout:
//   money       INTEGER cents, never REAL
//   tax rates   REAL, matching Flt (double) exactly
//   times       INTEGER, `_local` is the legacy wall-clock value and `_utc` its
//               resolved companion, NULL when the local time is ambiguous
//   `check` and `order` are SQL keywords, hence pos_check / order_item
// ---------------------------------------------------------------------------
constexpr std::string_view kMigration0001 = R"SQL(

CREATE TABLE db_meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE schema_migrations (
    version     INTEGER PRIMARY KEY,
    applied_at  INTEGER NOT NULL,
    checksum    TEXT NOT NULL,
    description TEXT NOT NULL
);

-- Replaces System::NewSerialNumber(), which was never persisted and was
-- recovered at boot by scanning archives backwards until one had a nonzero
-- last_serial_number. If the newest archives were empty or corrupt the counter
-- silently restarted and serials collided. Allocation happens inside the
-- writing transaction, so it is exact.
CREATE TABLE sequence (
    name       TEXT PRIMARY KEY,
    next_value INTEGER NOT NULL
);

-- Checks and drawers deliberately continue to share one counter, matching
-- System::NewSerialNumber. Splitting them is correct but visibly changes
-- numbering, so it is a separate decision from the migration.
INSERT INTO sequence(name, next_value) VALUES ('pos_serial', 1);

-- One row per business day. The open day is the row with closed_at_local IS
-- NULL, which is what lets EndDay stop being "copy everything into a new file"
-- and become "stamp a timestamp, insert the next row".
CREATE TABLE business_day (
    id                 INTEGER PRIMARY KEY,
    legacy_filename    TEXT UNIQUE,
    start_local        INTEGER,
    end_local          INTEGER,
    start_utc          INTEGER,
    end_utc            INTEGER,
    closed_at_local    INTEGER,
    last_serial_number INTEGER NOT NULL DEFAULT 0,
    corrupt            INTEGER NOT NULL DEFAULT 0 CHECK(corrupt IN (0,1)),

    -- Provenance only. Each legacy archive recorded its own per-entity versions
    -- and each embedded record set decoded with its own; once imported the data
    -- is version-free, but keeping these makes an import auditable.
    src_file_version      INTEGER,
    src_check_version     INTEGER,
    src_drawer_version    INTEGER,
    src_tip_version       INTEGER,
    src_work_version      INTEGER,
    src_exception_version INTEGER,
    src_expense_version   INTEGER,
    src_media_version     INTEGER,
    src_settings_version  INTEGER
);

-- At most one open day, enforced by the database rather than by convention.
--
-- Indexing the expression rather than the column is required, not stylistic.
-- SQLite treats NULLs as distinct in a UNIQUE index, so indexing
-- closed_at_local directly lets any number of rows hold NULL and constrains
-- nothing. The expression evaluates to 1 for every row the partial index
-- covers, so uniqueness on it permits exactly one open day.
CREATE UNIQUE INDEX ux_business_day_open
    ON business_day((closed_at_local IS NULL)) WHERE closed_at_local IS NULL;

CREATE INDEX ix_business_day_start ON business_day(start_local);

-- The tax policy in force on a given day, frozen so that changing a rate today
-- cannot restate closed days. SubCheck::FigureTotals reads these rather than
-- live Settings.
--
-- tax_takeout_food is present here even though the legacy Archive had no such
-- field: FigureTotals read it live from Settings, so toggling it retroactively
-- changed food tax on every archived takeout check. Capturing it closes that.
--
-- snapshot_complete is 0 for imported days, because the legacy EndDay omitted
-- tax_VAT and advertise_fund from its copy and every archive it produced holds
-- zero for both. Reports can then distinguish "the rate was zero" from "the
-- rate was never recorded", which the legacy format could not express.
CREATE TABLE day_policy (
    business_day_id       INTEGER PRIMARY KEY
                          REFERENCES business_day(id) ON DELETE CASCADE,
    tax_food              REAL    NOT NULL DEFAULT 0,
    tax_alcohol           REAL    NOT NULL DEFAULT 0,
    tax_room              REAL    NOT NULL DEFAULT 0,
    tax_merchandise       REAL    NOT NULL DEFAULT 0,
    tax_GST               REAL    NOT NULL DEFAULT 0,
    tax_PST               REAL    NOT NULL DEFAULT 0,
    tax_HST               REAL    NOT NULL DEFAULT 0,
    tax_QST               REAL    NOT NULL DEFAULT 0,
    tax_VAT               REAL    NOT NULL DEFAULT 0,
    royalty_rate          REAL    NOT NULL DEFAULT 0,
    advertise_fund        REAL    NOT NULL DEFAULT 0,
    price_rounding        INTEGER NOT NULL DEFAULT 0,
    change_for_credit     INTEGER NOT NULL DEFAULT 0,
    change_for_roomcharge INTEGER NOT NULL DEFAULT 0,
    change_for_checks     INTEGER NOT NULL DEFAULT 0,
    change_for_gift       INTEGER NOT NULL DEFAULT 0,
    discount_alcohol      INTEGER NOT NULL DEFAULT 0,
    tax_takeout_food      INTEGER NOT NULL DEFAULT 0,
    store_tz              TEXT    NOT NULL DEFAULT '',
    snapshot_complete     INTEGER NOT NULL DEFAULT 1
                          CHECK(snapshot_complete IN (0,1))
);

-- Lookup tables rather than CHECK constraints, for the enums reports need
-- labels for. Settings::TenderName is a hardcoded parallel array that is
-- already out of sync with the TENDER_* constants; a table makes adding a
-- tender a migration instead of a recompile, and SQLite cannot alter a CHECK
-- constraint without rebuilding the table anyway.
CREATE TABLE tender_type (
    id         INTEGER PRIMARY KEY,
    code       TEXT NOT NULL UNIQUE,
    name       TEXT NOT NULL,
    sort_order INTEGER NOT NULL DEFAULT 0,
    retired_at INTEGER
);

CREATE TABLE check_status (
    id   INTEGER PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL
);

CREATE TABLE item_family (
    id         INTEGER PRIMARY KEY,
    code       TEXT NOT NULL UNIQUE,
    name       TEXT NOT NULL,
    sort_order INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE sales_group (
    id   INTEGER PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL
);

-- Records entities intentionally left behind, so an omission is a decision
-- someone can read rather than a gap someone rediscovers years later.
CREATE TABLE not_migrated (
    entity     TEXT PRIMARY KEY,
    reason     TEXT NOT NULL,
    decided_at INTEGER NOT NULL
);

)SQL";

// Seed rows are separate from the DDL so the lookup contents can be diffed
// against the C++ constants by a test without re-parsing the schema.
constexpr std::string_view kSeed0001 = R"SQL(

-- Values come from enum class CheckStatus (check.hh), which starts at 1, not 0.
-- Seeding 0/1/2 here would have silently shifted every imported check's status
-- by one; the lookup-vs-constants test is what catches that.
INSERT INTO check_status(id, code, name) VALUES
    (1, 'OPEN',   'Open'),
    (2, 'CLOSED', 'Closed'),
    (3, 'VOIDED', 'Voided');

INSERT INTO sales_group(id, code, name) VALUES
    (0, 'NONE',        'None'),
    (1, 'FOOD',        'Food'),
    (2, 'BEVERAGE',    'Beverage'),
    (3, 'BEER',        'Beer'),
    (4, 'WINE',        'Wine'),
    (5, 'ALCOHOL',     'Alcohol'),
    (6, 'MERCHANDISE', 'Merchandise'),
    (7, 'ROOM',        'Room');

INSERT INTO item_family(id, code, name, sort_order) VALUES
    (0,   'APPETIZERS',        'Appetizers',        0),
    (1,   'BEVERAGES',         'Beverages',         1),
    (2,   'LUNCH_ENTREES',     'Lunch Entrees',     2),
    (3,   'CHILDRENS_MENU',    'Childrens Menu',    3),
    (4,   'DESSERTS',          'Desserts',          4),
    (5,   'SANDWICHES',        'Sandwiches',        5),
    (6,   'SIDE_ORDERS',       'Side Orders',       6),
    (7,   'BREAKFAST_ENTREES', 'Breakfast Entrees', 7),
    (14,  'PIZZA',             'Pizza',             14),
    (16,  'BEER',              'Beer',              16),
    (18,  'WINE',              'Wine',              18),
    (20,  'COCKTAIL',          'Cocktail',          20),
    (23,  'MODIFIER',          'Modifier',          23),
    (26,  'MERCHANDISE',       'Merchandise',       26),
    (255, 'UNKNOWN',           'Unknown',           255);

INSERT INTO db_meta(key, value) VALUES
    ('created_by_schema_version', '1');

)SQL";

} // namespace

const std::vector<Migration> &AllMigrations()
{
    static const std::vector<Migration> migrations = {
        Migration{1, "core schema: business days, policy snapshot, lookups",
                  kMigration0001},
    };
    return migrations;
}

int LatestSchemaVersion()
{
    const auto &migrations = AllMigrations();
    return migrations.empty() ? 0 : migrations.back().version;
}

std::string MigrationChecksum(std::string_view text)
{
    // FNV-1a. Not a security primitive -- it only needs to notice that a
    // migration's text changed after it was applied somewhere.
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char ch : text)
    {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }

    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(hash));
    return std::string(buffer);
}

MigrationResult MigrateToLatest(Database &db)
{
    MigrationResult result;

    if (!db.IsOpen())
    {
        result.status = Status::NotOpen;
        result.error = "database is not open";
        return result;
    }

    const int64_t current = db.UserVersion();
    if (current < 0)
    {
        result.status = Status::SqlError;
        result.error = "could not read user_version";
        return result;
    }

    const int latest = LatestSchemaVersion();
    result.final_version = static_cast<int>(current);

    if (current > latest)
    {
        // Older binary, newer data. Guessing here would corrupt a database this
        // build does not understand, so refuse instead.
        result.status = Status::SqlError;
        result.error = "database schema version " + std::to_string(current) +
                       " is newer than this build supports (" +
                       std::to_string(latest) + "); upgrade ViewTouch";
        return result;
    }

    for (const Migration &migration : AllMigrations())
    {
        if (migration.version <= current)
            continue;

        Transaction tx(db, Transaction::Mode::Immediate);
        if (Status s = tx.Begin(); s != Status::Ok)
        {
            result.status = s;
            result.error = "could not begin migration " +
                           std::to_string(migration.version) + ": " + db.LastError();
            return result;
        }

        if (Status s = db.Exec(migration.up); s != Status::Ok)
        {
            result.status = s;
            result.error = "migration " + std::to_string(migration.version) +
                           " failed: " + db.LastError();
            return result;   // Transaction rolls back in its destructor
        }

        // Seed data for migration 1 lives alongside its DDL, inside the same
        // transaction, so a database can never exist with the tables but not
        // the lookup rows they depend on.
        if (migration.version == 1)
        {
            if (Status s = db.Exec(kSeed0001); s != Status::Ok)
            {
                result.status = s;
                result.error = "seeding migration 1 failed: " + db.LastError();
                return result;
            }
        }

        const std::string record =
            "INSERT INTO schema_migrations(version, applied_at, checksum, description) "
            "VALUES (" + std::to_string(migration.version) + ", strftime('%s','now'), '" +
            MigrationChecksum(migration.up) + "', '" +
            std::string(migration.description) + "');";

        if (Status s = db.Exec(record); s != Status::Ok)
        {
            result.status = s;
            result.error = "could not record migration " +
                           std::to_string(migration.version) + ": " + db.LastError();
            return result;
        }

        if (Status s = db.SetUserVersion(migration.version); s != Status::Ok)
        {
            result.status = s;
            result.error = "could not set user_version: " + db.LastError();
            return result;
        }

        if (Status s = tx.Commit(); s != Status::Ok)
        {
            result.status = s;
            result.error = "could not commit migration " +
                           std::to_string(migration.version) + ": " + db.LastError();
            return result;
        }

        ++result.applied_count;
        result.final_version = migration.version;
    }

    return result;
}

} // namespace vt::sql
