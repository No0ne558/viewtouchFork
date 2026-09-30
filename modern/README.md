# ViewTouch Modern

A ground-up rewrite of ViewTouch in C++23 and Qt6/QML. It lives on the `Modernization` branch. The legacy code in the repo root stays as a reference until the new app reaches feature parity.

The full design and milestones are in [docs/PLAN.md](docs/PLAN.md).

## What carries over from ViewTouch

- **Pages are made of freely placed buttons (zones).** The owner can move, resize, restyle and rewire every button in an in-app **edit mode**.
- **Template pages.** A page can inherit the buttons of a template page, for example an order bar shared by every menu page.
- **Style inheritance.** Button styles resolve in this order: zone → page → theme. Each button has normal, selected and disabled looks.
- **Modifier page sequences.** For example, "Steak" → Temperature → Sides. Meal-period index pages and index tabs work as before.

## What changes

| Legacy | Modern |
|---|---|
| X11/Motif, the server draws every pixel | Qt Quick (GPU); the UI runs locally on each terminal |
| Fixed pixel page sizes | Logical canvas scaled to any screen |
| Positional `.dat` files | Versioned JSON pages and SQLite (WAL, transactional) |
| Negative magic page IDs | Page `role`s (`login`, `tables`, `settle`, …) |
| About 75 zone types | One `button` with an action list, plus about 20 widgets |
| Motif dialogs, no undo | Inspector panel, snap and guides, full undo/redo |
| Starter pages downloaded at runtime | Seed pages shipped in `seed/` |
| All I/O on one event loop | Printing and storage on worker threads |

## Status

| Milestone | State |
|---|---|
| M0 Scaffold | done |
| M1 Layout engine: pages, templates, styles, navigation, starter pages | done |
| M2 Page editor | done |
| M3 Core POS flow (login → order → pay) | done |
| M4 Printing, drawers, reports, end of day, admin screens, split check | done |
| M5 Several terminals on one server, kitchen display, takeout/delivery details | done |
| M6 Tips, party gratuity, per-terminal drawers and printers, pay outs / paid ins, tip cash-out | done |
| M7 Packages (Fedora, Debian/Raspberry Pi OS, Ubuntu), service and kiosk at boot, backups, meal periods | done |
| M8 Card payments | last |

## Installing

Packages are built for Fedora 44, Debian 13 (and Raspberry Pi OS based on it) and Ubuntu 26.04, on x86_64 and aarch64 (64-bit Raspberry Pi). They need Qt 6.8 or newer, so older releases (Debian 12, Ubuntu 24.04) are not supported.

```sh
sudo dnf install ./vtmodern-0.7.0-1.fc44.x86_64.rpm            # Fedora
sudo apt install ./vtmodern_0.7.0-1~debian13_arm64.deb         # Debian / Raspberry Pi OS
```

Installing starts nothing. Pick this machine's part in the store:

| Command | This machine |
|---|---|
| `sudo vtmodern-setup store` | Has a screen and keeps the data; other terminals may join |
| `sudo vtmodern-setup server` | Keeps the data with no screen (back office, closet box) |
| `sudo vtmodern-setup terminal <server> [name]` | Has a screen and joins the server |
| `sudo vtmodern-setup off` | Stops starting ViewTouch at boot |
| `vtmodern-setup status` | Shows what is set up and the newest backup |

- **Screens start full screen at boot** (`vtmodern-kiosk.service`) in the `cage` kiosk on the first console, with no desktop and no mouse pointer. Turn off any desktop login (`sudo systemctl disable gdm`) on a dedicated POS screen. On a desktop, *ViewTouch* is also in the applications menu.
- **The server** (`vtmodern.service`) runs sandboxed as the `viewtouch` account and restarts if it stops. Open port 7719/tcp in the firewall for the terminals.
- **Settings** live in `/etc/viewtouch/kiosk.conf` and `/etc/viewtouch/server.conf`: any `vtmodern --help` option, written `option = value`. Restart the service after a change.
- **Data** lives in `/var/lib/viewtouch` and is kept when the package is removed. Only one ViewTouch can use a database at a time; a second one is refused and told to `--connect` instead.
- **Logs:** `journalctl -u vtmodern` or `journalctl -u vtmodern-kiosk`.

**Building the packages:** `modern/packaging/build-packages.sh` builds for this machine; add `fedora`, `debian` or `ubuntu` to build in a podman container. The *Modern packages* GitHub workflow (Actions tab, or a `modern-v*` tag) builds all six.

## Backups

- **Automatic:** the machine that keeps the data backs up when it starts, every 24 hours and after every End of Day, keeping the newest 30. Backups go to `backups/` next to the database (`/var/lib/viewtouch/backups` when installed). A USB stick or network share is safer: set `backup-dir` in the .conf file. `backup-every` and `backup-keep` change the timing and count.
- **By hand:** `vtmodern --backup` (add `--data-dir /var/lib/viewtouch` for an installed store). It is safe while ViewTouch runs.
- **Restore:**
  ```sh
  sudo systemctl stop vtmodern vtmodern-kiosk
  sudo vtmodern --data-dir /var/lib/viewtouch --restore /var/lib/viewtouch/backups/viewtouch-20260929-230000.db
  sudo systemctl start vtmodern      # or vtmodern-kiosk
  ```
  The backup is checked first. The database it replaces is kept beside it as `viewtouch.db.before-restore-<time>`.

Each backup is a complete SQLite copy. Checks, pages, menu, staff, settings and past days are all in it.

## Meal periods

Manager → Meal Periods sets when breakfast, lunch and dinner start (the starter set is 04:00, 11:00 and 16:00). You can add others, such as Late Night. Each period runs until the next one starts, and past midnight until the first one. The Menu button opens the index page whose *Meal period* (page inspector) matches the time. With no page for the current period, it opens an *All day* index page, else the first one.

## Several terminals

One machine keeps the data; the others connect to it:

```sh
# The server. It is also a terminal unless --headless.
./modern/build/vtmodern --serve                 # port 7719; --port to change
./modern/build/vtmodern --serve --headless      # a back-office box with no screen

# Every other terminal (no database of its own)
./modern/build/vtmodern --connect 192.168.1.10 --terminal "Bar"
./modern/build/vtmodern --connect 192.168.1.10 --terminal "Line" --page kitchen   # kitchen screen
```

- **Shared data.** Checks, tables, the menu, staff, the drawer and reports are the same on every terminal. Check numbers run in one sequence, and End of Day sees every terminal.
- **Check locks.** A check that is open on one terminal can't be opened on another (the table shows "on Bar"). Closing it, putting it away, logging out or losing the connection releases it.
- **Page edits reach every terminal.** Pages edited on any terminal are saved on the server and pushed to the others. Saving needs a manager on that terminal.
- **Lost connection.** A terminal that loses the server shows *Reconnecting…*, keeps trying, and returns to the login page when the server is back.
- **Responsiveness.** Terminals never wait on the network. Button actions continue when the server answers, and touches are ignored until then.
- **Printing.** All printing happens at the server's printers. Each terminal has its own drawer and prints receipts on the printer set for it in Manager → Terminals (default: the "receipt" printer).
- **Security.** The connection is plain TCP on your local network, with no encryption or terminal passwords. Keep it on a trusted network; PINs are still required for everything.

## Kitchen display and takeout / delivery

- **Kitchen and bar displays.** Manager → Kitchen Display / Bar Display, or start a terminal with `--page kitchen` or `--page bar-display`. These screens need no login.
  - Each Send is one ticket, oldest first, turning amber after 5 minutes and red after 10.
  - Touch a ticket when it's ready. Recall brings the last one back.
  - Each station bumps only its own lines, so the bar clearing drinks leaves the kitchen's food on its screen.
  - Paid-first counter orders stay on screen until they are bumped.
- **Takeout / Delivery** (floor plan) asks for the customer: name, phone, address and a note. The details save as you type. They print on the receipt and kitchen tickets and show on the order, the kitchen card and the check list. A takeout or delivery that is put away with nothing ordered is discarded.

## Tips and cash handling

- **Tips** go on card payments. On the settle page, touch 15%, 18% or 20%, or type an amount and touch Amount. The receipt, the Tips report and the server's "tips owed" show them.
- **Party gratuity.** Dine-in checks with at least *min guests* get the store's gratuity (Manager → Settings; 18% for 6+ by default, 0 turns it off). Anyone can add it on the settle page; lowering an automatic one needs a manager. Gratuity is part of the check total; tips are on top.
- **Drawers per terminal.** Each terminal starts, counts and balances its own drawer ("Bar drawer"). Cash payments need this terminal's drawer open, and End of Day waits until every drawer is counted.
- **Pay outs and paid ins** (Drawer page, manager): type the amount, optionally touch Reason…, then Pay Out or Paid In. They show in the drawer's expected cash and on the Drawers report.
- **Cash out tips.** Logout → Cash Out My Tips pays the card tips owed to you from this terminal's drawer.
- **Manager → Terminals** sets each terminal's receipt printer (and so its drawer kick).

## Running the POS

Log in with a demo PIN: **1234** (manager), **1111** (server) or **2222** (cashier). Then:

1. Touch a table on the floor plan and enter the number of guests. Quick Order and Takeout skip the table.
2. Order from the menu pages. Burgers walk through Temperature and then Side. Tap No / Extra / Lite / Side before an item to qualify it. Touch a line in the check to select it.
3. **Send** the order. **Void** removes an item that hasn't been sent; voiding a sent item needs a manager.
4. **Pay**, then type an amount (or use the balance due) and choose a payment: Cash gives change, card payments are capped at the balance, and 10% Off and Comp are discounts. Then **Close Check**. Cash needs an open drawer: **Drawer…** on the Settle page.
5. **Split Check** on the Settle page moves items to another check at the same table. Touching a table that has several checks lets you choose one.

**Manager** (PIN 1234 → Manager):
- **Menu, Employees, Payments (Tenders), Printers, Taxes, Settings, Terminals, Meal Periods:** edit and Save. Staff are deactivated rather than deleted. PINs must be unique, and you can't lock yourself out.
- **Reports:** Sales, Items, Servers, Labor and Drawer, for today (live) or any closed day (◀ ▶). Print sends a report to the receipt printer.
- **Drawers:** start with a bank, then count at the end of the shift to see over or short. No Sale opens the drawer.
- **End of Day:** once every check is settled and the drawer is counted, this saves the day's reports and starts a new day.

**Printing**
- **When tickets print:** Send prints a kitchen ticket per station (each menu item names its printer, such as kitchen or bar). Voiding a sent item prints a VOID ticket. Closing a cash sale opens the drawer.
- **Where they go:** the starter printers write text files to `printouts/` in the data folder (`~/.local/share/ViewTouch/ViewTouch/`, or `/var/lib/viewtouch` when installed). In Manager → Printers, switch them to **Network** (raw TCP 9100, ESC/POS) or **CUPS**.
- **Printer failures:** printing runs on its own thread. A printer that is off or unplugged is retried and then reported on screen; it never freezes the POS.

**Behind the scenes**
- Every change to a check is saved to SQLite at once, on a background thread, so an open check survives a crash or restart. Tax follows the legacy rule: each tax class is taxed on its total, rounding half away from zero.
- Starter menu, staff and tax settings come from `seed/pos/`. `--reset-menu` reloads them without touching checks.
- Pages can require a permission (the inspector's *Who may open it*). The Manager page needs `manager`. Editing pages needs `layout.edit`, which the manager has.

**Widgets that work** (add them from *+ Panel* in the editor)

| Widget | What it does |
|---|---|
| loginPad | PIN entry |
| tableMap | Floor plan; tables are set in the zone's `props.tables` |
| guestCount | Number of guests |
| numPad | Number or amount entry (`props.mode: "amount"`) |
| orderList | The current check |
| paymentPanel | Payments and the balance |
| checkList | Open checks |
| keyboard | Kitchen notes |
| clock | Time and date |
| logoutPanel | Who is on shift |
| statusBar | Latest message |
| kitchenDisplay | Kitchen/bar tickets (`props.station`: kitchen, bar, or empty for all) |
| customerInfo | Takeout/delivery customer details |

Manager widgets:

| Widget | What it does |
|---|---|
| adminPanel | Manager editors (`props.panel`: menu, employees, tenders, printers, taxes, store, terminals, mealPeriods) |
| reportView | Reports |
| drawerPanel | Cash drawer |
| endOfDay | End of day |
| splitCheck | Split a check |

## Editing pages

Press **F1**, or touch **Manager → Edit Pages**. Changes go into a draft. The running app keeps the saved pages until you press **Save**, which writes to the database in one transaction. **Done** asks whether to save or discard.

**Mouse**
- Click a zone to select it. Shift- or Ctrl-click adds it to the selection or removes it.
- Drag across empty canvas to select every zone the box touches.
- Drag a zone to move it. Drag one of its 8 handles to resize it.
- Zones snap to the page grid, or to pink guides at other zones' edges and centers and the canvas center.
- Zones from a template are dimmed and locked. Double-click one to open its template page.
- Right-click a zone for copy, cut, duplicate, delete and layer order.

**Keys**

| Key | Action |
|---|---|
| Arrows | Move one grid step |
| Shift+Arrows | Move one unit |
| Ctrl+D | Duplicate |
| Del / Backspace | Delete |
| Ctrl+C / X / V | Copy, cut, paste (works across pages) |
| Ctrl+A | Select all |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z or Ctrl+Y | Redo |
| Ctrl+] / Ctrl+[ | Bring to front / send to back |
| PgUp / PgDn | Previous / next page |
| Ctrl+S | Save |
| Esc | Clear selection |

**Panels**
- **Left: page list.** Create, copy or delete pages. Delete lists the buttons that lead to the page first.
- **Right: inspector, for the zone, the page, or the theme.** Every field is built from `layout/schema.cpp`, so a new property needs one line there and no QML. Style fields show the inherited value until you override them, and ↺ resets them. "When touched" edits the action list.
- **Toolbar.** Add a button, text, image, note or panel; undo and redo; arrange (align, space evenly, match size); and File, which exports or imports one page (`.vtpage.json`) or every page (`.vtlayout.json`).

## Layout

```
core/     pure C++ domain: money, tax, menu, checks, employees — no Qt
layout/   page / zone / action model, JSON, field schema
app/      navigator, layout editor (undo), PosSession API, PosService + PosShared store, JSON mapping
net/      terminal server, remote session, page sync (newline-delimited JSON over TCP)
storage/  SQLite: layout + POS stores, background AsyncWriter
print/    ticket formatting (text + ESC/POS) and the background PrintSpooler
ui/cpp/   QML-facing controllers (LayoutController, EditorController)
ui/qml/   QML views and the editor (static QML module 'ViewTouch')
seed/     starter pages, theme, menu, staff, settings (generated by tools/gen_seed.py)
tests/    Catch2 unit tests
```

## Building

Prerequisites: a C++23 compiler, CMake, Ninja and Qt 6.8 or newer. `sudo modern/packaging/build-packages.sh deps` installs them on Fedora, Debian and Ubuntu, or by hand on Fedora:

```sh
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel
```

```sh
cmake -S modern -B modern/build -G Ninja
cmake --build modern/build
ctest --test-dir modern/build
./modern/build/vtmodern                   # data in ~/.local/share/ViewTouch/ViewTouch/
./modern/build/vtmodern --reset-layout    # back to the starter pages
./modern/build/vtmodern --data-dir /tmp/t # use another data folder (database, backups, printouts)
./modern/build/vtmodern --kiosk           # full screen, no mouse pointer
./modern/build/vtmodern --login 1234      # start logged in (testing)
```

Tests run headless (offscreen). They include UI tests that drag, resize and type in the real editor.

If Qt6 is not found, only the core library and tests are built. To build it from the legacy root project instead, pass `-DVT_BUILD_MODERN=ON`.
