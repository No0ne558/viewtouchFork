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
| M3 Core POS flow (login → order → pay) | next |

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
core/     pure C++ domain (money, tax, checks, menu…) — no Qt
layout/   page / zone / action model, JSON, field schema
app/      navigator, layout editor (undo), services  (M3+)
storage/  SQLite: layout store (more repos in M3)
print/    ESC/POS printing                           (M4)
ui/cpp/   QML-facing controllers (LayoutController, EditorController)
ui/qml/   QML views and the editor (static QML module 'ViewTouch')
seed/     starter pages and theme
tests/    Catch2 unit tests
```

## Building

Prerequisites (Fedora):

```sh
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel
```

```sh
cmake -S modern -B modern/build -G Ninja
cmake --build modern/build
ctest --test-dir modern/build
./modern/build/vtmodern                   # pages saved in ~/.local/share/ViewTouch/ViewTouch/viewtouch.db
./modern/build/vtmodern --reset-layout    # back to the starter pages
./modern/build/vtmodern --db /tmp/t.db    # use another database
```

Tests run headless (offscreen). They include UI tests that drag, resize and type in the real editor.

If Qt6 is not found, only the core library and tests are built. To build it from the legacy root project instead, pass `-DVT_BUILD_MODERN=ON`.
