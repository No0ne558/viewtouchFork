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

// ---------------------------------------------------------------------------
// 0002 - the transactional core: checks, subchecks, orders, payments, totals.
//
// Three things here are not translations of the legacy model but corrections to
// it, and each is called out at its table:
//   * order_item.parent_order_id makes the modifier tree explicit instead of
//     re-deriving it from item type and adjacency on every load
//   * every child collection carries an explicit seq, because list order is
//     semantically meaningful and previously survived only as byte position
//   * subcheck_total freezes the money, which the legacy format recomputed on
//     every read -- see the table comment for why that had to change
// ---------------------------------------------------------------------------
constexpr std::string_view kMigration0002 = R"SQL(

CREATE TABLE tender_type_ref (
    id         INTEGER PRIMARY KEY,
    code       TEXT NOT NULL UNIQUE,
    name       TEXT NOT NULL,
    -- Which media table tender_id points into, when it points anywhere.
    -- NULL means tender_id is not a media reference for this tender.
    media_kind TEXT,
    sort_order INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE check_type_ref (
    id   INTEGER PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL
);

CREATE TABLE pos_check (
    id                   INTEGER PRIMARY KEY,
    business_day_id      INTEGER NOT NULL REFERENCES business_day(id),

    -- serial_number is scoped to the day rather than global. That is the only
    -- scope in which it was ever meaningful ("check #143 today"), and it makes
    -- the duplicate serials that already exist in historical archives legal
    -- rather than a migration blocker: the legacy counter was never persisted
    -- and was recovered by scanning archives backwards, so it could restart.
    serial_number        INTEGER NOT NULL,
    serial_disambiguator INTEGER NOT NULL DEFAULT 0,

    -- EndDay can move an open check into the next day, and when only some
    -- subchecks are open it mints a brand new check that keeps only the table,
    -- time_open and the two user ids -- type, flags, label, comment and
    -- customer_id are all dropped. That lineage was unrecoverable before.
    split_from_check_id  INTEGER REFERENCES pos_check(id),

    type                 INTEGER NOT NULL REFERENCES check_type_ref(id),
    flags                INTEGER NOT NULL DEFAULT 0,   -- CF_* bitmask
    check_state          INTEGER NOT NULL DEFAULT 0,
    user_open            INTEGER,
    user_owner           INTEGER,
    customer_id          INTEGER,
    call_center_id       INTEGER NOT NULL DEFAULT 0,
    guests               INTEGER NOT NULL DEFAULT 0,
    has_takeouts         INTEGER NOT NULL DEFAULT 0,
    checknum             INTEGER NOT NULL DEFAULT 0,
    is_training          INTEGER NOT NULL DEFAULT 0 CHECK(is_training IN (0,1)),

    label                TEXT,
    comment              TEXT,
    termname             TEXT,
    -- The pre-substitution text. The legacy writer maps ' ' and '~' onto '_'
    -- and the reader maps '_' back to ' ', so stored names are already damaged.
    -- Keeping the raw token turns an invisible loss into an auditable one.
    label_raw            TEXT,
    comment_raw          TEXT,

    time_open_local      INTEGER,
    time_open_utc        INTEGER,
    chef_time_local      INTEGER,
    made_time_local      INTEGER,
    check_in_local       INTEGER,
    check_out_local      INTEGER,
    date_local           INTEGER,

    UNIQUE(business_day_id, serial_number, serial_disambiguator)
);

CREATE INDEX ix_pos_check_day  ON pos_check(business_day_id);
CREATE INDEX ix_pos_check_open ON pos_check(time_open_local);

CREATE TABLE subcheck (
    id                INTEGER PRIMARY KEY,
    check_id          INTEGER NOT NULL REFERENCES pos_check(id) ON DELETE CASCADE,
    -- Denormalized from pos_check. This breaks 3NF deliberately: the salesmix
    -- and day-range reports are the entire point of the migration, and without
    -- it every one of them is a three-table join over millions of order rows.
    -- A trigger below asserts it agrees with the parent.
    business_day_id   INTEGER NOT NULL REFERENCES business_day(id),

    -- SubCheck::number was assigned positionally by Check::Add and never
    -- persisted; the file format recovered it purely from record order.
    seq               INTEGER NOT NULL,

    status            INTEGER NOT NULL REFERENCES check_status(id),
    check_type        INTEGER NOT NULL REFERENCES check_type_ref(id),
    settle_user       INTEGER,
    settle_time_local INTEGER,
    settle_time_utc   INTEGER,
    drawer_id         INTEGER,
    tax_exempt        TEXT,
    new_QST_method    INTEGER NOT NULL DEFAULT 0,

    -- Non-NULL means the totals below are immutable. Corrections after this
    -- point are new rows, never edits.
    frozen_at_local   INTEGER,

    UNIQUE(check_id, seq)
);

CREATE INDEX ix_subcheck_day ON subcheck(business_day_id);

CREATE TRIGGER trg_subcheck_day_matches
BEFORE INSERT ON subcheck
WHEN NEW.business_day_id <>
     (SELECT business_day_id FROM pos_check WHERE id = NEW.check_id)
BEGIN
    SELECT RAISE(ABORT, 'subcheck.business_day_id disagrees with its check');
END;

CREATE TABLE order_item (
    id              INTEGER PRIMARY KEY,
    subcheck_id     INTEGER NOT NULL REFERENCES subcheck(id) ON DELETE CASCADE,
    business_day_id INTEGER NOT NULL REFERENCES business_day(id),

    -- The correction that matters most. Legacy stored a flat run of orders and
    -- rebuilt the tree on load from Order::IsModifier() plus adjacency, so the
    -- parent/child relationship was never recorded and could not be recovered
    -- when the inference was wrong.
    parent_order_id INTEGER REFERENCES order_item(id) ON DELETE CASCADE,
    seq             INTEGER NOT NULL,

    -- Never persisted by Order::Write, yet Order::Add sorts modifiers by it, so
    -- modifiers reordered across every save/load. Historical rows cannot
    -- recover a real value and import as the constructor default of 1.
    call_order      INTEGER NOT NULL DEFAULT 1,

    item_name       TEXT NOT NULL,
    item_name_raw   TEXT,
    item_type       INTEGER NOT NULL,
    item_family     INTEGER NOT NULL REFERENCES item_family(id),
    sales_type      INTEGER NOT NULL DEFAULT 0,
    item_cost       INTEGER NOT NULL,
    reduced_cost    INTEGER NOT NULL DEFAULT 0,
    qualifier       INTEGER NOT NULL DEFAULT 0 CHECK(qualifier >= 0),
    status          INTEGER NOT NULL DEFAULT 0,
    user_id         INTEGER,
    seat            INTEGER NOT NULL DEFAULT 0,
    count           INTEGER NOT NULL DEFAULT 1 CHECK(count > 0),
    employee_meal   INTEGER NOT NULL DEFAULT 0,
    is_reduced      INTEGER NOT NULL DEFAULT 0,
    auto_coupon_id  INTEGER NOT NULL DEFAULT -1,

    -- Frozen derived values, see subcheck_total.
    total_cost      INTEGER,
    total_comp      INTEGER,

    CHECK(parent_order_id IS NULL OR parent_order_id <> id)
);

-- Two partial indexes rather than one UNIQUE(subcheck_id, seq): SQLite treats
-- NULLs as distinct in a unique index, so a single constraint spanning both
-- root orders and modifiers would silently permit duplicate seq values on
-- exactly the rows that need it most.
CREATE UNIQUE INDEX ux_order_root_seq ON order_item(subcheck_id, seq)
    WHERE parent_order_id IS NULL;
CREATE UNIQUE INDEX ux_order_mod_seq  ON order_item(parent_order_id, seq)
    WHERE parent_order_id IS NOT NULL;

CREATE INDEX ix_order_subcheck ON order_item(subcheck_id);
CREATE INDEX ix_order_parent   ON order_item(parent_order_id);
-- The salesmix query shape, covered so a month of sales is one index scan
-- rather than a walk over every archive in the range.
CREATE INDEX ix_order_salesmix ON order_item(business_day_id, item_family, item_name);

-- The in-memory model has exactly one level of modifier. Making that an
-- invariant means an importer bug surfaces as a failed insert rather than as a
-- tree nobody notices is wrong.
CREATE TRIGGER trg_order_depth
BEFORE INSERT ON order_item
WHEN NEW.parent_order_id IS NOT NULL
 AND (SELECT parent_order_id FROM order_item WHERE id = NEW.parent_order_id) IS NOT NULL
BEGIN
    SELECT RAISE(ABORT, 'modifier of a modifier is not allowed');
END;

CREATE TABLE payment (
    id                INTEGER PRIMARY KEY,
    subcheck_id       INTEGER NOT NULL REFERENCES subcheck(id) ON DELETE CASCADE,
    business_day_id   INTEGER NOT NULL REFERENCES business_day(id),
    seq               INTEGER NOT NULL,

    tender_type       INTEGER NOT NULL REFERENCES tender_type_ref(id),

    -- Kept verbatim regardless of whether the referent can be resolved. The
    -- legacy tender_id is polymorphic across five media tables keyed by
    -- tender_type, and media lists were only snapshotted into archives from
    -- some versions onward -- so a historical payment can point at something
    -- that no longer exists anywhere. Preserving the raw value keeps the import
    -- lossless even then.
    legacy_tender_id  INTEGER NOT NULL DEFAULT 0,

    amount            INTEGER NOT NULL,   -- raw amount, or percent if TF_IS_PERCENT
    flags             INTEGER NOT NULL DEFAULT 0,
    user_id           INTEGER,
    drawer_id         INTEGER,

    -- Computed by FigureTotals; frozen with the rest of the money.
    value             INTEGER,

    -- FigureTotals deletes and recreates TENDER_CHANGE, TENDER_OVERAGE and
    -- TENDER_MONEY_LOST rows on every call. Without this flag an imported
    -- change row is indistinguishable from one about to be regenerated.
    synthesized       INTEGER NOT NULL DEFAULT 0 CHECK(synthesized IN (0,1)),

    UNIQUE(subcheck_id, seq)
);

CREATE INDEX ix_payment_subcheck ON payment(subcheck_id);
CREATE INDEX ix_payment_day      ON payment(business_day_id, tender_type);

-- Frozen money.
--
-- The legacy model persisted none of this: SubCheck::Read called FigureTotals
-- at the end of every load and recomputed everything from the orders plus the
-- day's tax rates. Three reasons that had to change, all verified against the
-- current code rather than assumed:
--
--  1. FigureTotals is not a pure function. It removes and deletes payment rows
--     and overwrites order->discount and order->is_reduced, so recompute-on-read
--     means opening a historical report mutates historical records.
--  2. The rates it recomputes against are incomplete. EndDay never snapshotted
--     tax_VAT, so every archive holds zero and VAT recomputes to nothing;
--     tax_takeout_food was read live and so moved retroactively.
--  3. The number on the customer's receipt and remitted to the tax authority is
--     the frozen one. Storing a different, later-recomputed value is wrong even
--     when the arithmetic is better.
--
-- FigureTotals stays the computation engine. This only stores its output.
CREATE TABLE subcheck_total (
    subcheck_id        INTEGER PRIMARY KEY
                       REFERENCES subcheck(id) ON DELETE CASCADE,
    raw_sales          INTEGER NOT NULL DEFAULT 0,
    total_sales        INTEGER NOT NULL DEFAULT 0,  -- pre-tax, unlike total_cost
    tax_food           INTEGER NOT NULL DEFAULT 0,
    tax_alcohol        INTEGER NOT NULL DEFAULT 0,
    tax_room           INTEGER NOT NULL DEFAULT 0,
    tax_merchandise    INTEGER NOT NULL DEFAULT 0,
    tax_GST            INTEGER NOT NULL DEFAULT 0,
    tax_PST            INTEGER NOT NULL DEFAULT 0,
    tax_HST            INTEGER NOT NULL DEFAULT 0,
    tax_QST            INTEGER NOT NULL DEFAULT 0,
    tax_VAT            INTEGER NOT NULL DEFAULT 0,
    total_cost         INTEGER NOT NULL DEFAULT 0,  -- sales + taxes - comps
    item_comps         INTEGER NOT NULL DEFAULT 0,
    payment            INTEGER NOT NULL DEFAULT 0,
    balance            INTEGER NOT NULL DEFAULT 0,
    tab_total          INTEGER NOT NULL DEFAULT 0,
    delivery_charge    INTEGER NOT NULL DEFAULT 0,
    computed_at_local  INTEGER NOT NULL DEFAULT 0,

    -- Bumped whenever FigureTotals changes, so "would today's code reproduce
    -- this?" is a query rather than an archaeology exercise. That predicate is
    -- exactly what dual-run verification needs.
    engine_version     INTEGER NOT NULL DEFAULT 1,
    -- 0 = computed live, 1 = imported as found, 2 = recomputed during import
    source             INTEGER NOT NULL DEFAULT 0 CHECK(source IN (0,1,2))
);

-- A frozen subcheck's totals are immutable. Corrections become new rows.
CREATE TRIGGER trg_subcheck_total_frozen
BEFORE UPDATE ON subcheck_total
WHEN (SELECT frozen_at_local FROM subcheck WHERE id = NEW.subcheck_id) IS NOT NULL
BEGIN
    SELECT RAISE(ABORT, 'totals for a frozen subcheck cannot be modified');
END;

-- check_total is a view rather than a table: there is no value in freezing a
-- sum of already-frozen numbers, and a view cannot drift from them.
CREATE VIEW check_total AS
SELECT s.check_id                AS check_id,
       SUM(t.total_sales)        AS total_sales,
       SUM(t.total_cost)         AS total_cost,
       SUM(t.payment)            AS payment,
       SUM(t.balance)            AS balance
FROM subcheck s
JOIN subcheck_total t ON t.subcheck_id = s.id
GROUP BY s.check_id;

)SQL";

constexpr std::string_view kSeed0002 = R"SQL(

INSERT INTO tender_type_ref(id, code, name, media_kind, sort_order) VALUES
    (0,  'CASH',           'Cash',                  NULL,         0),
    (1,  'CHECK',          'Check',                 NULL,         1),
    (2,  'CHARGE_CARD',    'Charge Card',           'creditcard', 2),
    (3,  'COUPON',         'Coupon',                'coupon',     3),
    (4,  'GIFT',           'Gift Certificate',      NULL,         4),
    (5,  'COMP',           'Comp',                  'comp',       5),
    (6,  'ACCOUNT',        'On Account',            NULL,         6),
    (7,  'CHARGE_ROOM',    'Room Charge',           NULL,         7),
    (8,  'DISCOUNT',       'Discount',              'discount',   8),
    (9,  'CAPTURED_TIP',   'Captured Tip',          NULL,         9),
    (10, 'EMPLOYEE_MEAL',  'Employee Meal',         'meal',      10),
    (11, 'CREDIT_CARD',    'Credit Card',           'creditcard',11),
    (12, 'DEBIT_CARD',     'Debit Card',            'creditcard',12),
    (13, 'CHARGED_TIP',    'Charged Tip',           NULL,        13),
    (16, 'PAID_TIP',       'Paid Tip',              NULL,        16),
    (17, 'OVERAGE',        'Overage',               NULL,        17),
    (18, 'CHANGE',         'Change',                NULL,        18),
    (19, 'PAYOUT',         'Payout',                NULL,        19),
    (20, 'MONEY_LOST',     'Money Lost',            NULL,        20),
    (21, 'GRATUITY',       'Gratuity',              NULL,        21),
    (22, 'ITEM_COMP',      'Item Comp',             'comp',      22),
    (23, 'EXPENSE',        'Expense',               NULL,        23),
    (24, 'CASH_AVAIL',     'Cash Available',        NULL,        24),
    (25, 'CC_FEE_DOLLAR',  'Credit Fee (amount)',   NULL,        25),
    (26, 'CC_FEE_PERCENT', 'Credit Fee (percent)',  NULL,        26),
    (27, 'DC_FEE_DOLLAR',  'Debit Fee (amount)',    NULL,        27),
    (28, 'DC_FEE_PERCENT', 'Debit Fee (percent)',   NULL,        28);

-- Values come from enum class CheckType (check.hh). Only the types the code
-- actually constructs are seeded; the rest arrive with their migration.
INSERT INTO check_type_ref(id, code, name) VALUES
    (0, 'RESTAURANT', 'Restaurant'),
    (1, 'TAKEOUT',    'Takeout'),
    (2, 'DELIVERY',   'Delivery'),
    (3, 'CATERING',   'Catering'),
    (4, 'HOTEL',      'Hotel'),
    (5, 'RETAIL',     'Retail'),
    (6, 'FASTFOOD',   'Fast Food'),
    (7, 'TOGO',       'To Go'),
    (8, 'FORHERE',    'For Here'),
    (9, 'CALLIN',     'Call In');

INSERT INTO not_migrated(entity, reason, decided_at) VALUES
    ('GroupItem',
     'Read/Write are stubs returning 1; ItemDB::group_list was never persisted',
     strftime('%s','now')),
    ('SalesItem.component_list',
     'SalesItem::Read discards the component count; the relationship was never stored',
     strftime('%s','now')),
    ('LaborPeriod/LaborDB',
     'marked obsolete in system.hh and duplicated by WorkDB',
     strftime('%s','now')),
    ('Archive::LoadUnpacked',
     'unimplemented stub returning 1',
     strftime('%s','now'));

)SQL";

std::string_view SeedFor(int version)
{
    switch (version)
    {
    case 1:  return kSeed0001;
    case 2:  return kSeed0002;
    default: return {};
    }
}

} // namespace

const std::vector<Migration> &AllMigrations()
{
    static const std::vector<Migration> migrations = {
        Migration{1, "core schema: business days, policy snapshot, lookups",
                  kMigration0001},
        Migration{2, "transactional core: checks, subchecks, orders, payments, totals",
                  kMigration0002},
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

        // Seed data runs inside the same transaction as its DDL, so a database
        // can never exist with the tables but not the lookup rows they depend
        // on -- a foreign key to an unseeded lookup would reject every insert.
        const std::string_view seed = SeedFor(migration.version);
        if (!seed.empty())
        {
            if (Status s = db.Exec(seed); s != Status::Ok)
            {
                result.status = s;
                result.error = "seeding migration " +
                               std::to_string(migration.version) + " failed: " +
                               db.LastError();
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
