# SQL persistence: migrating a site, and getting back

ViewTouch is moving its data from the legacy file format to an embedded SQLite
database. This is the operator-facing procedure. It is written to be followed
by someone responsible for a live restaurant, so it says plainly what each step
costs and what it cannot undo.

Nothing here happens automatically. A site that changes no configuration keeps
writing files exactly as it does today.

## Why

The legacy format is one file per check, rewritten whole, with no way to group
several writes. Ending a day touches a dozen files and can be interrupted
halfway. Individual writes became atomic and durable in an earlier change
(temp file plus rename, plus `fsync`), but atomicity *across* files is not
something the format can offer. That is the single largest thing this migration
buys.

Two smaller things it also fixes, both of which currently lose data silently:

- The modifier tree is not stored. It is rebuilt on load from item type plus
  adjacency, so the parent of a modifier is inferred rather than recorded.
- `call_order` is never written. Modifiers are sorted by it, so kitchen ticket
  order changes across a save — on every order, not just ones somebody edited.

## The three modes

Set `mode` in the `[persistence]` section of `/usr/viewtouch/dat/persistence.conf`:

```ini
[persistence]
mode = dual
database_path = /usr/viewtouch/dat/viewtouch.db
```

The file does not exist by default, and its absence means `legacy`. The backend
is chosen once at startup, after the current day is loaded and before the
terminals come up, so changing it takes effect on the next restart.

| Mode | Authoritative | What it is for |
|---|---|---|
| `legacy` | files | The default. What every site runs today. |
| `dual` | files | Files stay authoritative; SQLite shadows every write. |
| `sqlite` | database | Cutover. Files are no longer written. |

An unrecognised mode, or a database mode with no `database_path`, falls back to
`legacy` and logs why. A missing configuration file is not an error.

A database that cannot be **opened** is different: that aborts startup rather
than falling back. A site that asked for SQLite and silently got files would be
writing a day's takings somewhere nobody is looking for them, and would find out
at the next reconciliation.

## Procedure

### 1. Import the history

Stop ViewTouch, then run:

```
vt_import --data-path /usr/viewtouch/dat
```

It reads `database_path` from `persistence.conf`, so you cannot type the path
differently from what the running system will open. `--database PATH` overrides
that, and `--dry-run` imports into a throwaway copy and deletes it, so you can
see what a real run would report before committing to one.

Exit codes: `0` clean, `2` finished but at least one archive could not be read
(it is named in the output), `1` the import itself failed.

The originals are opened read-only and never renamed, truncated or deleted. A
test asserts that on the file bytes rather than trusting it.

Re-running is safe: archives already imported are skipped, so a run interrupted
partway can simply be repeated. One unreadable archive does not stop the rest —
it is counted and named in the result.

Two things imported data cannot recover, because the format never stored them:

- **`call_order`.** Historical modifiers arrive with the constructor default.
  The original kitchen ordering is gone and cannot be reconstructed.
- **Historical totals.** Every money field on a subcheck is *derived*; the
  format stored none of them, and loading a check recomputes them. So imported
  totals are what today's engine computes from the orders against that day's
  frozen tax rates — not the figure that was printed on the receipt. They are
  marked `source = 2` ("recomputed during import") and frozen so they stop
  drifting, which is an improvement on the legacy behaviour of recomputing on
  every single read.

Imported days also carry `snapshot_complete = 0`, because the legacy end-of-day
never copied `tax_VAT` or `advertise_fund` into the archive. Reports can
therefore distinguish "the rate was zero" from "the rate was never recorded" —
a distinction the file format could not express at all.

### 2. Run in dual mode

Set `mode = dual` and trade normally. Every save goes to both backends. The
database is exercised on real data from that site while files remain the source
of truth.

A shadow failure never fails a save. The primary's result is what the till sees;
shadow problems are counted and logged.

**The divergence report is written at every end of day**, to
`dat/divergence_YYYYMMDDHHMM.txt`, and a summary line goes to the log. It walks
both backends in full — which is why it runs then and not on the save path — and
lists every field that differs, naming each side and why.

**A non-empty report is expected.** The two backends genuinely disagree in three
known places, and each line in the report carries its own explanation:

- **`call_order`.** As above. Appears on essentially every order.
- **Strings containing `_`, `~` or runs of spaces.** The legacy writer maps `' '`
  and `'~'` onto `'_'` and the reader maps `'_'` back to `' '`, so `bar_side`
  reads back as `bar side`. Existing production data is already damaged this way;
  the report makes it visible rather than theoretical.
- **`drawer[...].balance_count` differing.** `Drawer::Write` emits a balance row
  only when `entered` is non-zero, so a file cannot say "this tender was counted
  and came to nothing" — a real outcome, and different from never having counted
  it. Expect the SQLite side to hold more balance rows than the legacy side.
- **`payment.flags` differing by 128 (`TF_FINAL`).** `Payment::Read` sets that
  flag unconditionally on every payment it reads, so a payment that was *not*
  final becomes final simply by surviving a save and reload. The legacy side
  reports the mutated value; SQLite reports what was actually in memory.

What matters is that *only* those appear. Item names, costs, families, seats,
tree shape, payment and subcheck counts must all agree. **A divergence outside
that list, or one with no explanation attached, means something is wrong and
cutover should wait.**

Stay in dual mode long enough that cutover is boring. This is the only mode
where the new backend is exercised on real data and rollback still costs
nothing.

### 3. Cut over

Set `mode = sqlite` and restart.

## Rollback

**From `legacy` or `dual`:** change the mode back and restart. There is nothing
to undo — files were authoritative throughout and are complete. Leave the
database file in place; deleting it discards the shadow data that would justify
trying again.

**From `sqlite`:** reinstall the previous version and point it at the data
directory, which is intact because the importer never modified it.

Everything written after cutover is lost, because those writes went only to the
database.

That is the real cost and it is stated rather than glossed. **Cutting over is
reversible only back to the moment of cutover.** A site that has traded for a
week on SQLite and then rolls back loses that week. The mitigation is a long
dual-mode period, not a promise that cannot be kept.

### Why there is no legacy-format exporter

An exporter would narrow that window, and it was deliberately not built.

The legacy writers can only emit the *current* format version. They take a
version argument, but `Check::Write` rejects anything except the current
constant and every caller passes that constant. So an exporter could not produce
files an older binary would read once the format has moved on. Shipping one
without that caveat understood would be selling a rollback that silently fails
at the worst possible moment.

## What is migrated, and what is not

`sqlite` mode moves **checks and drawers** — the two halves of end-of-day
reconciliation. Everything else still writes files on every mode:

archives (the whole-day file rewrite), settings, employees, labor and work
records, tips, expenses, inventory, customers, accounts, and the credit
databases.

So a site in `sqlite` mode is in a coherent but partial state: current checks
and drawers live in the database, the archive of each closed day is still a
file, and `EndDay` still performs its whole-day rewrite. Plan accordingly —
this is a staged migration, not a finished one.

## Checking a database

- The health check is a cheap readiness probe, safe to run at startup.
- `PRAGMA integrity_check` is the thorough one. It is slow on a real data set;
  run it during maintenance, not on the path to opening a till.
- The `business_day` table records, per imported day, which archive file it came
  from and the format versions that file used, so any import can be audited back
  to its source.
