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

/*
 * Migration 0003 corrects two lookup tables that were seeded from memory rather
 * than from the headers. Both were found by writing the first repository that
 * actually inserts rows against them -- neither had a way to surface until then.
 *
 * check_type_ref was wrong, not merely incomplete. It was seeded 0..9 with a
 * comment claiming the values came from `enum class CheckType`, but that enum
 * starts at 1 and runs to 15. Every id was therefore off by one against the
 * value pos_check.type actually holds: a Bar check (3) resolved to 'CATERING',
 * a Delivery check (5) to 'RETAIL', and the four self-service types plus DineIn
 * and ToGo had no row at all, so saving one failed the foreign key outright.
 * 'FORHERE' is not a member of the enum in any form.
 *
 * item_family was missing 17 of the 32 FAMILY_* constants in sales.hh --
 * burgers, salads, soup, seafood, bakery, room and more. Any order in one of
 * them failed to insert. The gaps are not a subset with a rationale; the seed
 * simply stopped partway.
 *
 * A new migration rather than an edit to 0001/0002: the runner records a
 * checksum per applied migration, so rewriting an applied one is exactly the
 * corruption the checksum exists to catch. Doing it the append-only way here,
 * while no site has a database yet and the cost is zero, is what proves the
 * mechanism works before it has to.
 *
 * The DELETE below runs with foreign_keys ON. If any pos_check row already
 * referenced a type being replaced, the migration aborts rather than silently
 * repointing live rows -- which is the correct outcome, not a limitation.
 */
constexpr std::string_view kMigration0003 = R"SQL(

DELETE FROM check_type_ref;

INSERT INTO check_type_ref(id, code, name) VALUES
    (1,  'RESTAURANT',   'Restaurant'),
    (2,  'TAKEOUT',      'Takeout'),
    (3,  'BAR',          'Bar'),
    (4,  'MERCHANDISE',  'Merchandise'),
    (5,  'DELIVERY',     'Delivery'),
    (6,  'CATERING',     'Catering'),
    (7,  'HOTEL',        'Hotel'),
    (8,  'RETAIL',       'Retail'),
    (9,  'FASTFOOD',     'Fast Food'),
    (10, 'SELFORDER',    'Self Order'),
    (11, 'DINEIN',       'Dine In'),
    (12, 'TOGO',         'To Go'),
    (13, 'CALLIN',       'Call In'),
    (14, 'SELFDINEIN',   'Self Order Dine In'),
    (15, 'SELFTAKEOUT',  'Self Order Take Out');

-- The families 0001 missed. INSERT OR IGNORE so the fifteen it did seed keep
-- their existing rows and sort_order rather than being churned.
INSERT OR IGNORE INTO item_family(id, code, name, sort_order) VALUES
    (8,  'ALACARTE',          'A La Carte',           8),
    (10, 'BURGERS',           'Burgers',             10),
    (11, 'DINNER_ENTREES',    'Dinner Entrees',      11),
    (12, 'SALADS',            'Salads',              12),
    (13, 'SOUP',              'Soup',                13),
    (15, 'SPECIALTY',         'Specialty',           15),
    (17, 'BOTTLED_BEER',      'Bottled Beer',        17),
    (19, 'BOTTLED_WINE',      'Bottled Wine',        19),
    (21, 'BOTTLED_COCKTAIL',  'Bottled Cocktail',    21),
    (22, 'SEAFOOD',           'Seafood',             22),
    (24, 'LIGHT_DINNER',      'Light Dinner',        24),
    (25, 'REORDER',           'Reorder',             25),
    (27, 'SPECIALTY_ENTREE',  'Specialty Entree',    27),
    (28, 'RESERVED_WINE',     'Reserved Wine',       28),
    (29, 'BANQUET',           'Banquet',             29),
    (30, 'BAKERY',            'Bakery',              30),
    (31, 'ROOM',              'Room',                31);

)SQL";

/*
 * Migration 0004 - drawers.
 *
 * The other half of end-of-day cash reconciliation. Checks say what was owed;
 * drawers say what was in the till, and the difference between them is what a
 * manager actually signs off on.
 *
 * Three things the legacy format could not express, each of which is why the
 * columns below are not a straight transcription of Drawer::Write:
 *
 *   Status is derived, never stored. Drawer::GetStatus() reads it off which of
 *   start_time / pull_time / balance_time are set. Keeping those three and
 *   deriving the same way means the status cannot drift from the timestamps
 *   that justify it, which a status column would allow.
 *
 *   Only counted cash is real. Drawer::Write emits a balance row only when
 *   `entered` is non-zero, and recomputes `amount` and `count` from the checks
 *   on every load. So the expected side is derived and the counted side is
 *   stored -- expected_amount and expected_count are recorded here as the value
 *   in force when the drawer was balanced, and are nullable because before that
 *   moment there is no meaningful answer rather than a zero one.
 *
 *   Payments and balances are different things sharing a tender type. A payment
 *   is money leaving the till (a tip paid out, an expense); a balance is a
 *   count of what should be in it. The legacy file interleaves them in one
 *   record stream; separate tables stop a report having to know which is which
 *   by position.
 */
constexpr std::string_view kMigration0004 = R"SQL(

CREATE TABLE drawer (
    id                INTEGER PRIMARY KEY,
    business_day_id   INTEGER NOT NULL REFERENCES business_day(id),

    -- Shares the pos_serial sequence with checks, matching
    -- System::NewSerialNumber, and carries the same historical-duplicate
    -- problem, so it is scoped and disambiguated exactly like pos_check.
    serial_number        INTEGER NOT NULL,
    serial_disambiguator INTEGER NOT NULL DEFAULT 0,

    host              TEXT,
    position          INTEGER NOT NULL DEFAULT 0,
    number            INTEGER NOT NULL DEFAULT 0,
    owner_id          INTEGER,
    puller_id         INTEGER,
    media_balanced    INTEGER NOT NULL DEFAULT 0,   -- bitfield of media flags

    -- The three timestamps GetStatus() derives from. All nullable: a drawer
    -- that has not been pulled has no pull time, which is different from a pull
    -- time of zero.
    start_time_local   INTEGER,
    pull_time_local    INTEGER,
    balance_time_local INTEGER,

    -- Non-NULL means the counted amounts below are immutable, same contract as
    -- subcheck.frozen_at_local.
    frozen_at_local   INTEGER,

    UNIQUE(business_day_id, serial_number, serial_disambiguator)
);

CREATE INDEX ix_drawer_day ON drawer(business_day_id);

-- Money out of the till: tips paid, expenses, payouts.
CREATE TABLE drawer_payment (
    id              INTEGER PRIMARY KEY,
    drawer_id       INTEGER NOT NULL REFERENCES drawer(id) ON DELETE CASCADE,
    business_day_id INTEGER NOT NULL REFERENCES business_day(id),
    seq             INTEGER NOT NULL,

    tender_type     INTEGER NOT NULL REFERENCES tender_type_ref(id),
    amount          INTEGER NOT NULL,
    user_id         INTEGER,
    target_id       INTEGER,
    time_local      INTEGER,

    UNIQUE(drawer_id, seq)
);

CREATE INDEX ix_drawer_payment_drawer ON drawer_payment(drawer_id);

-- What was counted, against what was expected.
CREATE TABLE drawer_balance (
    id              INTEGER PRIMARY KEY,
    drawer_id       INTEGER NOT NULL REFERENCES drawer(id) ON DELETE CASCADE,
    business_day_id INTEGER NOT NULL REFERENCES business_day(id),
    seq             INTEGER NOT NULL,

    tender_type     INTEGER NOT NULL REFERENCES tender_type_ref(id),
    legacy_tender_id INTEGER NOT NULL DEFAULT 0,

    -- The counted side. This is the only figure a person actually produced.
    entered         INTEGER NOT NULL,

    -- The expected side, as computed when the drawer was balanced. NULL before
    -- that: the legacy code recomputes these on every load from whatever checks
    -- are currently in scope, so a stored zero would be indistinguishable from
    -- "nothing was owed" when the truth is "nobody has counted yet".
    expected_amount INTEGER,
    expected_count  INTEGER,

    UNIQUE(drawer_id, seq)
);

CREATE INDEX ix_drawer_balance_drawer ON drawer_balance(drawer_id);

-- Same denormalization guard the subchecks get: business_day_id is copied down
-- for reporting speed, so a trigger has to keep it honest.
CREATE TRIGGER trg_drawer_payment_day_matches
BEFORE INSERT ON drawer_payment
WHEN NEW.business_day_id <>
     (SELECT business_day_id FROM drawer WHERE id = NEW.drawer_id)
BEGIN
    SELECT RAISE(ABORT, 'drawer_payment.business_day_id disagrees with its drawer');
END;

CREATE TRIGGER trg_drawer_balance_day_matches
BEFORE INSERT ON drawer_balance
WHEN NEW.business_day_id <>
     (SELECT business_day_id FROM drawer WHERE id = NEW.drawer_id)
BEGIN
    SELECT RAISE(ABORT, 'drawer_balance.business_day_id disagrees with its drawer');
END;

-- A frozen drawer's counted amounts are what a manager signed off on.
CREATE TRIGGER trg_drawer_balance_frozen
BEFORE UPDATE ON drawer_balance
WHEN (SELECT frozen_at_local FROM drawer WHERE id = NEW.drawer_id) IS NOT NULL
BEGIN
    SELECT RAISE(ABORT, 'balances for a frozen drawer cannot be modified');
END;

-- Over/short per drawer, derived rather than stored: it is a subtraction of two
-- columns that are already frozen, and a stored copy could only ever drift.
CREATE VIEW drawer_difference AS
SELECT d.id                                   AS drawer_id,
       d.business_day_id                      AS business_day_id,
       SUM(b.entered)                         AS counted,
       SUM(COALESCE(b.expected_amount, 0))    AS expected,
       SUM(b.entered - COALESCE(b.expected_amount, 0)) AS difference
FROM drawer d
JOIN drawer_balance b ON b.drawer_id = d.id
GROUP BY d.id;

)SQL";

/*
 * Migration 0006 - the media snapshot a day traded under.
 *
 * This is what makes `payment.tender_id` mean something. A payment's tender_id
 * is a polymorphic reference into one of five tables chosen by tender_type --
 * discounts, coupons, credit cards, comps, meals -- and those definitions live
 * in Settings, which is mutable. So a payment recorded against discount 4 says
 * "whatever discount 4 is called today", not what it was called that night.
 *
 * The legacy format solved it the same way, by copying the media lists into
 * each archive from version 10 on. Older archives fall back to a static
 * alternate-media file for exactly this reason: the comment in Archive::
 * LoadPacked says reports should not change every time a discount is added.
 *
 * Shape: one table for what all five share, plus an extension table for the
 * coupon-only fields. Not five tables -- resolving a payment would then need to
 * know which one to join before it could look -- and not one wide table with
 * ten mostly-NULL columns either. media_kind carries the discriminator, and
 * (business_day_id, media_kind, legacy_id) is what a payment resolves against.
 *
 * `active` and `flags` are snapshotted rather than dropped: a discount that was
 * inactive that day still explains why nothing used it.
 */
constexpr std::string_view kMigration0006 = R"SQL(

CREATE TABLE media_kind_ref (
    id   INTEGER PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL
);

INSERT INTO media_kind_ref(id, code, name) VALUES
    (1, 'DISCOUNT',    'Discount'),
    (2, 'COUPON',      'Coupon'),
    (3, 'CREDIT_CARD', 'Credit card'),
    (4, 'COMP',        'Comp'),
    (5, 'MEAL',        'Employee meal');

CREATE TABLE day_media (
    id               INTEGER PRIMARY KEY,
    business_day_id  INTEGER NOT NULL REFERENCES business_day(id) ON DELETE CASCADE,
    media_kind       INTEGER NOT NULL REFERENCES media_kind_ref(id),

    -- MediaInfo::id, what payment.tender_id holds. Unique only within a day
    -- and a kind, which is exactly the resolution a payment needs.
    legacy_id        INTEGER NOT NULL,

    name             TEXT NOT NULL DEFAULT '',
    -- MediaInfo::local: defined on this terminal rather than store-wide.
    is_local         INTEGER NOT NULL DEFAULT 0,

    -- CreditCardInfo and CompInfo have no amount; zero is correct for them
    -- rather than absent, because there is no amount to be missing.
    amount           INTEGER NOT NULL DEFAULT 0,
    flags            INTEGER NOT NULL DEFAULT 0,
    -- Snapshotted rather than dropped: an inactive discount explains why
    -- nothing used it that day.
    active           INTEGER NOT NULL DEFAULT 0,

    UNIQUE(business_day_id, media_kind, legacy_id)
);

-- Coupons carry six fields none of the others do. An extension table keeps
-- them off the four kinds that would only ever hold NULL there.
CREATE TABLE day_media_coupon (
    day_media_id     INTEGER PRIMARY KEY
                     REFERENCES day_media(id) ON DELETE CASCADE,

    automatic        INTEGER NOT NULL DEFAULT 0,
    item_family      INTEGER NOT NULL DEFAULT 0,
    item_id          INTEGER NOT NULL DEFAULT 0,
    item_name        TEXT NOT NULL DEFAULT '',

    -- A coupon's validity window: a time of day range and a date range, plus
    -- day-of-week and month bitmasks. All four times are nullable because a
    -- coupon with no window is different from one starting at midnight.
    start_time_local INTEGER,
    end_time_local   INTEGER,
    start_date_local INTEGER,
    end_date_local   INTEGER,

    days             INTEGER NOT NULL DEFAULT 0,
    months           INTEGER NOT NULL DEFAULT 0
);

-- The resolution a report performs per payment row.
CREATE INDEX ix_day_media_lookup ON day_media(business_day_id, media_kind, legacy_id);

)SQL";

/*
 * Migration 0007 - labor periods and the work entries in them.
 *
 * Payroll. A labor period is a pay period, and it does not line up with a
 * business day: one period spans many days and closes on its own schedule, so
 * this hangs off nothing in the day tables.
 *
 * A correction worth recording, because the plan for this migration had it
 * backwards. `WorkDB` is the class marked "will replace LaborPeriod & LaborDB",
 * and it is wired to nothing at all -- System::work_db and Archive::work_db are
 * members no code outside those classes touches. LaborDB, marked "obsolete", is
 * the live one: manager.cc loads it at startup, terminal.cc reads it to decide
 * whether someone is clocked in, and system_report.cc costs labor out of it.
 * So this migrates LaborPeriod, not WorkDB.
 *
 * Shape notes:
 *
 *   serial_number identifies a period and is what the file name carries. It is
 *   UNIQUE here, unlike check and drawer serials, because periods come from
 *   LaborDB::last_serial rather than the shared pos_serial counter and do not
 *   have the historical-duplicate problem those two do.
 *
 *   `end_time` unset means the period is still open. That is the one open
 *   period LaborDB::CurrentPeriod returns, so it is nullable rather than
 *   defaulted -- an open period has no end, which is different from ending at
 *   the epoch.
 *
 *   `overtime` is recorded but is NOT authoritative, and the column says so.
 *   It is never written to the legacy file at all, and the only code that
 *   assigns it is LaborPeriod::WorkReport -- as a side effect of drawing a
 *   report line. So it is zero unless somebody happened to open that report.
 *   MinutesOvertime is the real figure, computed on demand. The column exists
 *   because recording what was in memory is honest and dropping it would lose
 *   the one case where it was computed; it must not be read as payroll.
 */
constexpr std::string_view kMigration0007 = R"SQL(

CREATE TABLE labor_period (
    id              INTEGER PRIMARY KEY,
    serial_number   INTEGER NOT NULL UNIQUE,

    -- NULL while the period is open. LaborDB::CurrentPeriod is the one with no
    -- end, and an open period is not a period that ended at zero.
    end_time_local  INTEGER,
    end_time_utc    INTEGER,

    -- Where it came from, for auditing an import back to its source.
    legacy_filename TEXT
);

CREATE TABLE work_entry (
    id              INTEGER PRIMARY KEY,
    labor_period_id INTEGER NOT NULL REFERENCES labor_period(id) ON DELETE CASCADE,

    user_id         INTEGER NOT NULL,
    job             INTEGER NOT NULL DEFAULT 0,

    pay_rate        INTEGER NOT NULL DEFAULT 0,   -- PERIOD_HOUR and friends
    pay_amount      INTEGER NOT NULL DEFAULT 0,   -- cents, per pay_rate unit
    tips            INTEGER NOT NULL DEFAULT 0,

    -- NOT authoritative. Never written to the legacy file, and assigned only
    -- by LaborPeriod::WorkReport as a side effect of rendering, so it is zero
    -- unless that report was opened. MinutesOvertime computes the real figure.
    overtime        INTEGER NOT NULL DEFAULT 0,
    end_shift       INTEGER NOT NULL DEFAULT 0,

    -- An entry with no end is someone still on the clock.
    start_local     INTEGER,
    start_utc       INTEGER,
    end_local       INTEGER,
    end_utc         INTEGER,

    -- Order within the period, which the file carried only as position.
    sequence        INTEGER NOT NULL,
    UNIQUE(labor_period_id, sequence)
);

-- "What did this employee work over this range" is the payroll query.
CREATE INDEX ix_work_entry_user ON work_entry(user_id, start_local);

)SQL";

/*
 * Migration 0008 - the credit databases.
 *
 * Last on purpose, and gated on the cardholder-data hardening that landed
 * first. The rule this schema exists to enforce is that there is no column
 * anywhere that can hold a full card number unless the operator has explicitly
 * asked for one, and that asking is visible in the row itself.
 *
 * A day's exceptions, refunds and voids -- CC_DBTYPE_EXCEPT / _REFUND / _VOID.
 * The legacy archive holds three separate CreditDB blocks; one table with a
 * `db_kind` discriminator keeps a reconciliation report from having to union
 * three shapes that differ only in which list they came from.
 *
 * What is deliberately NOT stored:
 *
 *   Track data. t1_disc, t2_disc and t3_disc are raw magnetic-stripe
 *   discretionary data -- the fields that overflowed and were fixed earlier in
 *   this work. They are transient authorisation inputs, never needed after the
 *   transaction, and storing them would be storing the stripe.
 *
 *   The CV and AVS response values. They are decision inputs, not records.
 *
 *   The swipe buffer itself, for the same reason as track data.
 *
 * `card_number` holds whatever Credit::PAN(save_entire_cc_num) returned, which
 * is the masked form unless the operator turned the setting on -- the same
 * single decision point Credit::Write uses, so the file and the database can
 * never disagree about what left memory. `pan_is_masked` records which it was,
 * so a later audit does not have to infer it from the bytes.
 */
constexpr std::string_view kMigration0008 = R"SQL(

CREATE TABLE credit_db_kind (
    id   INTEGER PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL
);

INSERT INTO credit_db_kind(id, code, name) VALUES
    (1, 'VOID',      'Voids'),
    (2, 'REFUND',    'Refunds'),
    (3, 'EXCEPTION', 'Exceptions');

CREATE TABLE credit_transaction (
    id              INTEGER PRIMARY KEY,
    business_day_id INTEGER NOT NULL REFERENCES business_day(id) ON DELETE CASCADE,
    db_kind         INTEGER NOT NULL REFERENCES credit_db_kind(id),

    -- Whatever Credit::PAN(save_entire_cc_num) returned. Masked unless the
    -- operator explicitly configured otherwise; never the raw member.
    card_number     TEXT NOT NULL DEFAULT '',
    -- 1 when card_number is masked. Recorded rather than inferred so an audit
    -- can answer "did this site ever store full numbers" with a query.
    pan_is_masked   INTEGER NOT NULL DEFAULT 1
                    CHECK(pan_is_masked IN (0,1)),
    -- Always safe to keep, and what receipts and reconciliation actually use.
    last_four       TEXT NOT NULL DEFAULT '',

    expire          TEXT NOT NULL DEFAULT '',
    card_holder     TEXT NOT NULL DEFAULT '',
    card_type       INTEGER NOT NULL DEFAULT 0,   -- credit / debit / gift
    credit_type     INTEGER NOT NULL DEFAULT 0,   -- Visa, MasterCard, ...
    processor       INTEGER NOT NULL DEFAULT 0,

    -- The identifiers a settlement is reconciled by.
    approval        TEXT NOT NULL DEFAULT '',
    auth_code       TEXT NOT NULL DEFAULT '',
    response_code   TEXT NOT NULL DEFAULT '',
    batch           INTEGER NOT NULL DEFAULT 0,
    item            INTEGER NOT NULL DEFAULT 0,
    ttid            INTEGER NOT NULL DEFAULT 0,

    amount          INTEGER NOT NULL DEFAULT 0,
    tip             INTEGER NOT NULL DEFAULT 0,
    full_amount     INTEGER NOT NULL DEFAULT 0,

    last_action     INTEGER NOT NULL DEFAULT 0,
    state           INTEGER NOT NULL DEFAULT 0,
    auth_state      INTEGER NOT NULL DEFAULT 0,
    trans_success   INTEGER NOT NULL DEFAULT 0,

    sequence        INTEGER NOT NULL,
    UNIQUE(business_day_id, db_kind, sequence)
);

-- Reconciling a settlement batch is the query these exist for.
CREATE INDEX ix_credit_transaction_batch ON credit_transaction(batch, item);

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


/*
 * Migration 0005 - the rest of a closed day: tips, expenses and exceptions.
 *
 * These three are what an archive file holds beyond its checks, drawers and
 * frozen policy. They are money and audit trail respectively, and until now a
 * site in `sqlite` mode kept them only in the archive file.
 *
 * Notes on shape, each of which is a place a straight transcription of the
 * legacy records would lose something:
 *
 *   A tip entry is a running balance, not an event. TipEntry carries `amount`
 *   owed, `previous_amount` carried in from the day before, and `paid` this
 *   day. Keeping all three means a day answers "what did this employee take
 *   home" without needing the previous day loaded -- which is exactly what
 *   reading a chain of archive files was for.
 *
 *   An expense has two account references and they are not interchangeable.
 *   `account_id` is what it was spent on, `tax_account_id` the tax portion's
 *   account, and `dest_account_id` where the money went. The legacy record
 *   writes all three as bare integers with no way to tell a real id from an
 *   unset one, so they are nullable here and zero means nobody.
 *
 *   `entered` is the counted side, and it is the same distinction drawers
 *   have: an expense with entered = 0 was recorded but never reconciled
 *   against a drawer, which is different from one reconciled to zero.
 *
 *   Exceptions are three different events, not one table with a type column.
 *   An item exception voids or comps a line; a table exception moves a check
 *   between tables; a rebuild exception records a check being reconstructed.
 *   They share only a timestamp, a user and a check serial. ExceptionDB reads
 *   them as three separate counted lists for that reason, and splitting them
 *   here keeps a report from having to know which columns are meaningful for
 *   which type.
 *
 * Every timestamp keeps the local/utc pair and the same NULL-means-ambiguous
 * rule as everywhere else -- see check_writer.hh.
 */
constexpr std::string_view kMigration0005 = R"SQL(

CREATE TABLE tip_entry (
    id               INTEGER PRIMARY KEY,
    business_day_id  INTEGER NOT NULL REFERENCES business_day(id) ON DELETE CASCADE,

    user_id          INTEGER NOT NULL,

    -- Captured tips still owed to the employee at the end of this day.
    amount           INTEGER NOT NULL DEFAULT 0,
    -- Carried in from the previous day. Stored rather than derived so a day
    -- stands alone; the legacy reports had to walk archives backwards for it.
    previous_amount  INTEGER NOT NULL DEFAULT 0,
    -- Paid out during this day.
    paid             INTEGER NOT NULL DEFAULT 0,

    -- One row per employee per day. A second entry for the same employee is
    -- an update to the running balance, not a new fact.
    UNIQUE(business_day_id, user_id)
);

CREATE TABLE expense (
    id               INTEGER PRIMARY KEY,
    business_day_id  INTEGER NOT NULL REFERENCES business_day(id) ON DELETE CASCADE,

    -- Expense::eid, the id the legacy record carries. Not unique across days.
    legacy_id        INTEGER NOT NULL DEFAULT 0,

    account_id       INTEGER,        -- what it was spent on
    tax_account_id   INTEGER,        -- account for the tax portion
    dest_account_id  INTEGER,        -- where the money went
    employee_id      INTEGER,
    drawer_id        INTEGER,

    amount           INTEGER NOT NULL DEFAULT 0,
    tax              INTEGER NOT NULL DEFAULT 0,
    -- Counted against a drawer, or 0 for never reconciled. Same distinction as
    -- drawer_balance.entered: reconciled-to-zero is not the same as unreconciled.
    entered          INTEGER NOT NULL DEFAULT 0,

    flags            INTEGER NOT NULL DEFAULT 0,
    document         TEXT NOT NULL DEFAULT '',
    explanation      TEXT NOT NULL DEFAULT '',

    exp_date_local   INTEGER,
    exp_date_utc     INTEGER,

    -- Order within the day, which the file format carried only as position.
    sequence         INTEGER NOT NULL,
    UNIQUE(business_day_id, sequence)
);

CREATE TABLE item_exception (
    id               INTEGER PRIMARY KEY,
    business_day_id  INTEGER NOT NULL REFERENCES business_day(id) ON DELETE CASCADE,

    user_id          INTEGER,
    check_serial     INTEGER NOT NULL DEFAULT 0,

    item_name        TEXT NOT NULL DEFAULT '',
    item_cost        INTEGER NOT NULL DEFAULT 0,
    item_type        INTEGER NOT NULL DEFAULT 0,
    item_family      INTEGER NOT NULL DEFAULT 0,

    exception_type   INTEGER NOT NULL DEFAULT 0,
    reason           INTEGER NOT NULL DEFAULT 0,

    time_local       INTEGER,
    time_utc         INTEGER,

    sequence         INTEGER NOT NULL,
    UNIQUE(business_day_id, sequence)
);

CREATE TABLE table_exception (
    id               INTEGER PRIMARY KEY,
    business_day_id  INTEGER NOT NULL REFERENCES business_day(id) ON DELETE CASCADE,

    user_id          INTEGER,
    check_serial     INTEGER NOT NULL DEFAULT 0,

    source_id        INTEGER NOT NULL DEFAULT 0,
    target_id        INTEGER NOT NULL DEFAULT 0,
    table_name       TEXT NOT NULL DEFAULT '',

    time_local       INTEGER,
    time_utc         INTEGER,

    sequence         INTEGER NOT NULL,
    UNIQUE(business_day_id, sequence)
);

CREATE TABLE rebuild_exception (
    id               INTEGER PRIMARY KEY,
    business_day_id  INTEGER NOT NULL REFERENCES business_day(id) ON DELETE CASCADE,

    user_id          INTEGER,
    check_serial     INTEGER NOT NULL DEFAULT 0,

    time_local       INTEGER,
    time_utc         INTEGER,

    sequence         INTEGER NOT NULL,
    UNIQUE(business_day_id, sequence)
);

-- The report workload for all five is "everything for this day", which the
-- business_day_id prefix of each UNIQUE already serves -- except tip_entry,
-- whose reports are per employee across days.
CREATE INDEX ix_tip_entry_user ON tip_entry(user_id);
CREATE INDEX ix_expense_drawer ON expense(business_day_id, drawer_id);

)SQL";

const std::vector<Migration> &AllMigrations()
{
    static const std::vector<Migration> migrations = {
        Migration{1, "core schema: business days, policy snapshot, lookups",
                  kMigration0001},
        Migration{2, "transactional core: checks, subchecks, orders, payments, totals",
                  kMigration0002},
        Migration{3, "correct check_type_ref ids and complete item_family",
                  kMigration0003},
        Migration{4, "drawers: payments, balances and over/short",
                  kMigration0004},
        Migration{5, "closed-day contents: tips, expenses and exceptions",
                  kMigration0005},
        Migration{6, "media snapshot: what payment.tender_id resolved against",
                  kMigration0006},
        Migration{7, "labor periods and work entries",
                  kMigration0007},
        Migration{8, "credit databases: exceptions, refunds and voids",
                  kMigration0008},
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
