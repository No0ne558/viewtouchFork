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

## Layout

```
core/     pure C++ domain (money, tax, checks, menu…) — no Qt
layout/   page / zone / action model + JSON         (M1)
app/      services + command bus                     (M1+)
storage/  SQLite repositories                        (M2+)
print/    ESC/POS printing                           (M4)
ui/qml/   QML views, widgets, editor
seed/     starter pages, menu, theme                 (M1)
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
./modern/build/vtmodern
```

If Qt6 is not found, only the core library and tests are built. To build it from the legacy root project instead, pass `-DVT_BUILD_MODERN=ON`.
