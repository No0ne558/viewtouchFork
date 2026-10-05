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
| `sudo vtmodern-setup terminal [server] [name]` | Has a screen and joins a store: shows the Join screen to pair it (the store is found on the network unless given) |
| `sudo vtmodern-setup pair` | On the server: prints a code to pair a terminal with |
| `sudo vtmodern-setup off` | Stops starting ViewTouch at boot |
| `vtmodern-setup status` | Shows what is set up and the newest backup |

- **Screens start full screen at boot** (`vtmodern-kiosk.service`) in the `cage` kiosk on the first console, with no desktop and no mouse pointer. To leave it, a manager uses **Manager → Close ViewTouch** (touch it twice). The kiosk then stays closed until the next boot and the screen switches to the second console: your desktop, or a text login on a dedicated register. A technician can also switch consoles with Ctrl+Alt+F2 (add Fn on keyboards whose F-keys need it). Turn off any desktop login (`sudo systemctl disable gdm`) on a dedicated POS screen. On a desktop, *ViewTouch* is also in the applications menu.
- **The server** (`vtmodern.service`) runs sandboxed as the `viewtouch` account and restarts if it stops. Open port 7719 (TCP and UDP) in the firewall for the terminals.
- **Settings** live in `/etc/viewtouch/kiosk.conf` and `/etc/viewtouch/server.conf`: any `vtmodern --help` option, written `option = value`. Restart the service after a change.
- **Data** lives in `/var/lib/viewtouch` and is kept when the package is removed. Only one ViewTouch can use a database at a time; a second one is refused and told to `--connect` instead.
- **Logs:** `journalctl -u vtmodern` or `journalctl -u vtmodern-kiosk`.

**Building the packages:** `modern/packaging/build-packages.sh` builds for this machine; add `fedora`, `debian` or `ubuntu` to build in a podman container. The *Modern packages* GitHub workflow (Actions tab, or a `modern-v*` tag) builds all six.

## Android tablets

The Android app is a terminal: it finds the store's server on the Wi-Fi, pairs with it, and then works like any other terminal (same pages, live updates). It needs Android 9 or newer on a 64-bit or 32-bit ARM tablet.

1. **Get the app:** run the *Modern Android* workflow (Actions tab → Modern Android → Run workflow). The run's artifacts hold `ViewTouch-arm64-v8a.apk` (most tablets) and `ViewTouch-armeabi-v7a.apk` (older or budget ones).
2. **Install:** copy the APK to the tablet and open it. Android asks to allow installing apps from that source (Settings → Apps → Special access → Install unknown apps).
3. **Pair:** open ViewTouch. The Join screen lists the stores on the Wi-Fi. Type a code from Manager → Terminals → **Pair a Device**, name the tablet, and **Join**.

**Phones.** On a phone (a screen whose shorter side is under 600dp, like a folded Fold) the app turns portrait and shows *phone pages*: big buttons in one column. Unfolded or on a tablet it goes back to the standard pages in landscape. Details are under *Phones and small screens*.

The app is full screen and keeps the screen on. Back goes back a page. If the server is out of reach, it starts with the pages it saved last and shows *Reconnecting…*. Its pairing key is never included in Android backups.

**Signing:** to install updates over the previous version, every build must be signed with the same key. Make one once and store it in the repository's secrets:
```sh
keytool -genkeypair -keystore viewtouch.keystore -alias viewtouch -keyalg RSA -keysize 3072 -validity 10000
base64 -w0 viewtouch.keystore      # → secret ANDROID_KEYSTORE_BASE64
```
Also add ANDROID_KEYSTORE_ALIAS (`viewtouch`) and ANDROID_KEYSTORE_PASSWORD. Keep the keystore somewhere safe: an app signed with a lost key can't be updated, only reinstalled. Without these secrets, each build gets a throwaway key.

**Building elsewhere:** the workflow uses Qt 6.11.2 for Android (installed with `aqtinstall`), Android NDK r27c, JDK 17 and KDAB's OpenSSL for Android. Android builds need an x86_64 Linux, Windows or macOS machine: the NDK is not made for ARM64 Linux.

## Phones and small screens

Every page is designed on a 1920 × 1080 landscape canvas and scaled to the screen. On a phone that makes buttons too small, so phones get **phone pages** instead:

- **Phone versions.** A page can have a phone version: a separate page on a portrait canvas (1080 × 2280) marked *Phone version of* that page (page inspector → *Phones*). Phones show it instead; buttons and roles keep pointing at the original page. The starter pages include phone versions of Login, Tables (a scrolling grid of every table), Guest Count, the order template, Settle, Log Out, Customer and Open Checks.
- **Laid out for you.** A page without a phone version, such as the menu, item and modifier pages, is shown inside the phone version of its template, the phone order screen. That screen has the check on top, qualifiers and Void / Send / Pay at the bottom. The page's own buttons go in the middle in reading order, two or three across, with titles and panels across the full width. So new menu pages work on phones without any extra work.
- **Which screens are phones.** On Android, a screen whose shorter side is under 600dp (Android's own phone/tablet line), so a Fold switches as it opens and closes. Manager → Terminals → *Screen layout* can force phone or standard pages for a terminal, and `--screen phone|standard|auto` does the same from the command line. Try phone pages on a desktop with `vtmodern --screen phone --size 540x1140`.
- **Editing.** The editor always shows pages as designed. Open a phone version from the page list to edit it, or make one with *Copy*, then set *Phone version of*, *For screens: Phones* and a portrait canvas.

## Factory reset and demo data

- `vtmodern --factory-reset` puts the store back to a fresh install: it backs the database up first (`backups/viewtouch-<time>-before-reset.db`), then deletes it, so the next start begins with the starter pages, menu, staff and settings. Backups and saved report exports stay. Stop ViewTouch first (`sudo systemctl stop vtmodern vtmodern-kiosk` when installed; add `--data-dir /var/lib/viewtouch`).
- Or from the screen: **Manager → Factory Reset…**, type RESET. ViewTouch backs up, closes, deletes the database and starts again with the starter set (the installed services are restarted by systemd; on a desktop it starts itself again). Terminals reconnect on their own.
- `vtmodern --demo-data` fills a store that has no sales yet with demo history. It plays two months of service, and the same two months last year, through the real POS: staff clock in and take breaks, orders with their choices, the kitchen bumping tickets, cards with tips, cash, gift cards and house accounts, banks checked out, End of Day. It also adds 12 customers, 8 gift cards, two weeks of shifts and tonight's waitlist. It takes a few seconds.
- Together: `vtmodern --factory-reset --demo-data`.

## Backups

- **Automatic:** the machine that keeps the data backs up when it starts, every 24 hours and after every End of Day, keeping the newest 30. Backups go to `backups/` next to the database (`/var/lib/viewtouch/backups` when installed). A USB stick or network share is safer: set `backup-dir` in the .conf file. `backup-every` and `backup-keep` change the timing and count.
- **A second copy:** Manager → Store Settings → *Also copy backups to* (for example `/media/usb/viewtouch`). Every backup is also copied there and checked, keeping as many. If the drive is missing, the backup still runs and the Manager is told.
- **Status:** Manager → End of Day shows when the last backup ran, whether the second copy worked, and a **Back Up Now** button.
- **By hand:** `vtmodern --backup` (add `--data-dir /var/lib/viewtouch` for an installed store). It is safe while ViewTouch runs.
- **Restore:**
  ```sh
  sudo systemctl stop vtmodern vtmodern-kiosk
  sudo vtmodern --data-dir /var/lib/viewtouch --restore /var/lib/viewtouch/backups/viewtouch-20260929-230000.db
  sudo systemctl start vtmodern      # or vtmodern-kiosk
  ```
  The backup is checked first. The database it replaces is kept beside it as `viewtouch.db.before-restore-<time>`.
- **Encrypted backups:** Manager → Store Settings → *Encrypt backups* and a *Backup password* (8 characters or more). Backups and the second copy are then `viewtouch-<time>.vtbak` files that only that password opens (AES-256-GCM, with the key made from the password by PBKDF2). Each one is opened again and checked as it is made. Restoring one on the same computer needs nothing more. On another computer `--restore` asks for the password (or reads `VTM_BACKUP_PASSWORD`). **Write the password down somewhere safe:** without it, an encrypted backup can't be opened by anyone. Changing it leaves older backups on the old password.

**Encrypting the data itself.** The live database is an ordinary SQLite file, readable by anyone who has the disk. For a store server, turn on disk encryption when installing the operating system (Fedora and Ubuntu installers: *Encrypt my data*). Everything is then encrypted at rest with no change to ViewTouch, and the server asks for the disk password when it boots. PINs are never stored, only salted hashes.

**Power cuts and damage.** Each sale is synced to disk as soon as it is saved, on a background thread, so a power cut loses nothing that was already on the screen. At start ViewTouch checks the database. If it is damaged, ViewTouch won't run on it: it names the newest good backup and the command to restore it. The tests cover a power cut in the middle of a check, and a busy service of 1,000 checks on four terminals (every step under a millisecond, nothing lost).

Each backup is a complete SQLite copy. Checks, pages, menu, staff, settings and past days are all in it.

## Finding any check

Manager → **Find a Check…** (or *Find an Older Check…* on the Reopen page) searches every check from the last year, open or closed: a check number (`#123`), an amount (`17.62`, with or without the tip), or part of a name, phone number (digits), table, server, item or gift card number. Earlier days are read in the background, so the screen never waits. Touch a result to see its items and payments; **Reprint Receipt** prints a copy. Staff who can take payments can search.

## Customers, gift cards and house accounts

- **Customers** (Check… → Customer…, or Manager → Customers…): find someone by any part of their phone number or by name, see their visits, what they've spent and their last visit, edit their details and notes, and put them on the check. Takeout and delivery customers go on file by themselves when their check closes, and the customer form suggests regulars as you type.
- **Gift cards** (Settle → Gift Card…, Check… → Sell / Check a Gift Card…): type or swipe the number to see the balance and history. *Sell* or *Reload* puts the card on the check; it works once that check is paid (no number: one is made up). *Pay the Check from This Card* takes up to what it holds. Undoing that payment, or reopening the check that sold a card, puts the money back where it was. No tax on gift card sales, and nothing goes to the kitchen.
- **House accounts:** a manager turns one on for a customer (with an optional limit). Put the customer on the check, then **House Account** on Settle charges it. Payments on the account (cash goes in the drawer, or card) are on the Customers page.
- **Report:** Reports → Gift Cards: cards sold and spent, what is still on cards (owed by the store), and house account charges, payments and balances.

## Waitlist and reservations

**Waitlist** on the Tables page opens the host stand.

- **Waitlist:** add a party (name, phone, size, a note). The quote is the parties ahead plus one, times *minutes per party ahead* (Store Settings, 10 by default), which the host can change by 5 minutes. The line shows how long each party has waited against what they were told, in red once they are past it.
- **Table ready:** *Notify* texts the guest when a texting service is set up (Store Settings → *Texting service URL*: a JSON POST of `{"to", "message"}` to your SMS provider or a relay). Otherwise it reminds the host to tell them.
- **Seating:** *Seat Them…* lists the free tables, the smallest that fits first. Choose the server; the table's check opens for them with the party size and the guest's name.
- **Reservations:** book for today or the next six days at a time. *They're Here* puts them at the front of the line. Reservations more than 15 minutes late are marked; *No-Show* or *Cancel Booking* takes them off.
- The host stand shows how many parties were seated today, the average wait, and no-shows.

## Reports over a range

Reports have a row of periods: *Day* (one business day, as before), *This Week*, *Last Week*, *This Month*, *Last Month*, *This Year*, or *Dates…* (any range up to a year). *vs Last Year* adds the same days a year before and the change, row by row. Ranges are read from the saved checks away from the screen, so a busy terminal never waits. Sales, Items, Categories, By Hour, Servers, Kitchen, Audit and Food Cost work over a range; the rest are one day at a time. CSV and PDF save what is shown.

## Schedule and tip pooling

- **Manager → Schedule:** a week at a time (‹ ›), each day's shifts; touch a day, pick the person and the times, *Add Shift* (an end before the start runs past midnight). Touch a shift to remove it, or *Clock Them In*. Hours for the week are listed underneath, in red over the weekly overtime line.
- Staff see their **next shift** on the logout screen.
- **Clock in on schedule** (Store Settings): staff can only clock in from *…minutes early* before a scheduled shift until it ends. Managers always can, and can clock anyone in from the schedule.
- **Roles:** server, bartender, cashier, host (the waitlist), busser (clocks in, shares tips), manager, admin. The demo staff gained Riley (busser, PIN 3333) and Jo (bartender, PIN 4444).
- **Pay and labor cost:** Manager → Employees → *Pay rate* (an hour, before tips) and *Other jobs*, one per line with its pay (`bartender 9.00`). Someone with more than one job is asked *Which job today?* when they clock in. Each shift keeps the job and its pay, so a raise later doesn't change past labor. Reports → Labor adds **Labor cost today**: each person's pay, overtime at time and a half, the total, and **labor % of net sales**. Tip pools go by the job worked today.
- **Tip-outs** (Store Settings): one per line, e.g. `busser 15 tips` or `bartender 2 sales`. That share of each person's tips (or sales) goes into a pool for everyone of that role who worked today, split by hours. With no one of that role on today, servers keep it. *Cash Out My Tips* and Reports → Tips use the result.

## Inventory and food cost

- **Manager → Inventory:** each ingredient's unit, what's on hand, the low-stock mark and its cost per unit. Count stock or add a delivery by editing *On hand*.
- **Recipes** (Manager → Menu → *Recipe*): one ingredient per line with the amount, e.g. `bun 1` or `lettuce 0.5`. Modifiers that are menu items (sides) have recipes too; *No* on a modifier uses none, *Extra* twice as much, *Lite* half.
- Stock goes down when an order is sent to the kitchen (or fired, or closed), and comes back when a sent item is voided.
- An item whose ingredients run short is **sold out by itself**, and comes back when restocked. An item a person 86'd stays sold out until they bring it back. Crossing the low mark shows *Running low: …*.
- **Reports → Food Cost:** each item sold today with its sales, recipe cost and cost %, then every ingredient's stock (low and out marked).
- **Vendors and deliveries:** Manager → Inventory → **Vendors…** lists who the store buys from (phone, account number, delivery days), and each ingredient can name its usual vendor. **Receive a Delivery…**: touch the vendor, type how much came of each item (and its cost if it changed) and the invoice number, then **Receive Delivery**. Stock goes up, costs are updated, sold-out items come back, and the delivery is kept (60 days loaded). Reports → **Purchases** shows today's deliveries by vendor. The demo has three vendors.
- New stores start with demo stock and recipes for the burgers, salads, breakfast, coffee and sides. An existing store starts with an empty inventory (or `--reset-menu` for the demo set).

## Kitchen display

- Tickets turn yellow after *Kitchen: ticket turns yellow after* minutes and red when late (Store Settings; 8 and 15 to start).
- **Rush** and **VIP** (order screen → Check…): a rush ticket goes to the front with a red frame, VIP gets a gold one, and both are printed on kitchen tickets.
- **All Day** on the kitchen screen counts everything still to make there ("3 Cobb, 2 Caesar"), for batching.
- **How things look in the kitchen** (Manager → Menu): *Kitchen name* (what the kitchen screen and kitchen tickets show instead, e.g. "BCN BGR"), *Kitchen highlight* (a color on the kitchen screen), *Don't show in the kitchen* (water, "No side"). Modifier group options take a kitchen name after `|` (`Ranch | RNCH`), or `| -` to leave it off. Checks and receipts keep the real names.
- **Rush:** on the order screen, Check… → **Rush** (again to turn it off).
- **Expo Display** (Manager → Expo Display, or a screen started on the `expo` page): every ticket across the stations, each line ticked when its station bumps it, "Waiting on: bar", and blue READY when all of it is made. Touch a ready ticket when it goes out (a ticket not ready yet needs a second touch); Recall brings the last one back.
- **Reports → Kitchen:** tickets per station from sent to bumped, with the average, the longest, how many were late, and the five slowest.

## Manager approval and practice mode

- **Manager approval:** when someone may not do something on their own (void a sent item, a discount, a manager action), a PIN pad appears on the same screen. A manager types their PIN and it goes through once; the check's history says who approved it. *Cancel* puts it away. No logging out and back in.
- **Practice mode** (training): mark a person *In training* (Manager → Employees), or a manager touches **Practice Mode** on the Log Out page to switch their own screen. A yellow *PRACTICE* banner shows. Practice checks are labelled "(practice)" and leave real tables free; they never reach the kitchen screens or printers, stock, drawers, banks, reports, points, gift cards or house accounts. Closing one says "not a sale"; any left open are cleared at End of Day.

## Screen saver

Store Settings → *Dim the screen after (minutes)* (10 to start; 0 = never). A screen nobody touches dims to the store's name and the time. The first touch only wakes it: it never presses the button underneath. Kitchen, bar and expo screens stay on, and the self-order kiosk shows its own pictures instead.

## Idle log-out, table timers and messages

- **Idle log-out** (Store Settings, 3 minutes to start, 0 = never): a screen nobody has touched logs out and goes back to the login page. Kitchen, bar and expo screens need no login.
- **Table timers:** an open table shows how long it has been seated ("Sam · $42.10 · 38m"), with a red frame after *Mark tables seated longer than* (90 minutes to start). **Reports → Turns:** dine-in checks from seated to paid, by party size and by table, with the average check and per guest (also over a range of days).
- **Messages:** Log Out → **Send a Message…**, or **Message…** on the kitchen screens: to everyone, the kitchen screens, the floor, or one person on the clock, picked from ready-made ones ("Need a runner", "86: …") or typed. It shows across the top of the screens it's for until someone there touches OK; the last hour's are listed on the message page.

## Customer display

A second monitor facing the guest:

- **Between guests:** the logo (or store name) in the store's color, then its messages and pictures in turn (Store Settings → *Customer display: between guests*, one per line; `image:/path/photo.jpg` for a picture), plus any promotion running now.
- **While ordering:** the table or check number, each item with its choices (add-on prices shown as +$1.00), voids struck through, discounts and promotions, the total. With loyalty on, *Earn rewards*: the guest types their phone number on the screen's keypad and is found or signed up. It shows their first name (never the number), their points and what this visit earns.
- **Tips:** on Settle, **Ask Guest for Tip** offers the tip choices, a custom amount on the keypad, or no tip; the choice goes on their card payment.
- **After paying:** "Thank you", their change and the points they earned, and *Print receipt*, *Text me* (when texting is set up) or *No receipt*.

Start with `--customer-display auto` (or `customer-display = auto` in the kiosk's .conf): on a desktop it opens a full-screen window on the other monitor; on a kiosk, whose one window spans both monitors, the POS stays on the left monitor and the guest's side is on the right. With one monitor, `--customer-display split` puts it in the right third, to try it out.

## Loyalty and promotions

- **Loyalty** (Store Settings): customers on a check earn points on what they spend after discounts (1 per dollar to start); reopening a check takes them back. *Rewards*, one per line like `50 = 5.00`, take money off: Customers → the customer on the check → the reward button. Undoing the reward gives the points back. Guests can sign up or be found by typing their phone number on the customer display.
- **Promotions** (Manager → Promotions): percent off chosen categories or items, or *buy N, get M* (the cheapest at the percent off), on chosen days and times. They apply by themselves while they run, show on the check and receipt by name, and come off again when the time is up or the items go. The demo set has *Happy Hour* (weekdays 3-6 PM, half-price draft beer and wine) and *Burger Tuesday* (the second burger half off).

## Self-order kiosk

A screen where guests order on their own, then pay at the counter.

- **Turning it on:** Manager → *Self-Order Kiosk* turns this screen into one on the spot. For a screen that should always be one, set Manager → Terminals → *Screen layout* to *Self-order kiosk*, or start it with `vtmodern --self-order`.
- **For the guest:** pictures and *Touch to Order* while nobody is ordering → *For Here* / *To Go* → the menu by category, with photos and descriptions → their choices (temperature, sides…) → a name to call the order by → *Your order number is 42*.
- **Tall kiosks:** on a portrait screen (e.g. a 21.5" floor stand such as the Chipsee KIOSK-CM4-215) the dishes are big photo cards, and the categories, the order bar and every button sit in the lower part, within reach. **Easy Reach** brings the whole menu down to the lower 60% of the screen for guests in a wheelchair, and children. Sold-out items show as sold out. If a guest walks away mid-order, *Are you still there?* appears, and the order is cleared (default after 90 s, set in Store Settings).
- **At the counter:** the order waits in Open Checks as *Kiosk 42* with the guest's name. The cashier opens it, takes the payment, and closing it sends it to the kitchen. With Store Settings → *Self-order kiosk: send orders to the kitchen at once*, it goes to the kitchen as soon as the guest finishes.
- **What guests see:** every menu item except modifiers (they appear as choices), anything marked *Not on the self-order kiosk*, and alcohol, which never shows there. Each item can have a *Description* and a *Photo* (Manager → Menu Items): an image file on the store's computer; kiosks connected over the network get it from there. The attract screen shows the dishes' photos and the customer display's slides (Store Settings).
- **Staff:** nothing on the kiosk leads to staff screens. A manager holds the top-left corner for 3 seconds and types their PIN to end kiosk mode.
- Card payments at the kiosk will come with card readers (M8).

## Languages

The screens come in English and Spanish (Español).

- **Each person's own.** Manager → Employees → *Language*. The screen switches when that person logs in and back when they log out. On a store with several terminals each screen speaks its own user's language, messages included. The demo store has Rosa (PIN 5555), a server whose screens are in Spanish.
- **The store's.** Manager → Store Settings → *Language*: for screens nobody is logged in to, people without their own, and everything guests see: the customer display, receipts and kitchen tickets.
- **Your own words.** Menu item names stay as you typed them, since the check, the kitchen and receipts use them. To translate buttons you added (or change any phrase), put a file `translations/es.json` in the data folder: `{"Burger of the Day": "Hamburguesa del día"}`. It wins over the built-in phrases.
- **Developers.** Phrases live in `i18n/es.json` (English → Spanish). `python3 tools/i18n.py missing es` lists what a new screen still needs, and the `i18n_es` test fails until every phrase has its Spanish with the same `%1` placeholders.

## On-screen keyboard

Text fields (customer names, Manager forms…) get an on-screen keyboard, docked below the page, on touch screens: on by default with `--kiosk`, or `--touch-keyboard yes|no`. Number fields get a number pad.

## Meal periods

Manager → Meal Periods sets when breakfast, lunch and dinner start (the starter set is 04:00, 11:00 and 16:00). You can add others, such as Late Night. Each period runs until the next one starts, and past midnight until the first one. The Menu button opens the index page whose *Meal period* (page inspector) matches the time. With no page for the current period, it opens an *All day* index page, else the first one.

## Several terminals

One machine keeps the data; the others connect to it:

```sh
# The server. It is also a terminal unless --headless.
./modern/build/vtmodern --serve                 # port 7719; --port to change
./modern/build/vtmodern --serve --headless      # a back-office box with no screen

# Every other terminal (no database of its own): finds the store and pairs on screen
./modern/build/vtmodern --connect auto --terminal "Bar"
./modern/build/vtmodern --connect 192.168.1.10 --terminal "Line" --page kitchen   # a given server
./modern/build/vtmodern --connect 192.168.1.10 --pair K7QM4-XHP2W --terminal "Bar"   # pair from a script
```

**Pairing terminals.** Only paired devices can connect.
1. On any store screen: Manager → Terminals → **Pair a Device** shows a code like `K7QM4-XHP2W`. It works once, for 10 minutes. With a server that has no screen, run `sudo vtmodern-setup pair` (or `vtmodern --pairing-code`) on it instead.
2. The new terminal shows **Join a ViewTouch store**. It lists the stores it finds on the network (or type the server's address), then asks for the code and this terminal's name. It has its own keyboard for touch-only screens.
3. It gets its own key, saved in `terminal.json` in its data folder, and connects with it from then on. It shows up in Manager → Terminals as a *paired device*, where its printer and drawer are set. Removing it there disconnects it at once and keeps it out. It then shows the Join screen again.

**Finding the server.** Terminals ask on the network (UDP port 7719) and servers answer with the store's name. A paired terminal whose server changed address (DHCP) finds it again by its id and saves the new address.

- **Shared data.** Checks, tables, the menu, staff, the drawer and reports are the same on every terminal. Check numbers run in one sequence, and End of Day sees every terminal.
- **Check locks.** A check that is open on one terminal can't be opened on another (the table shows "on Bar"). Closing it, putting it away, logging out or losing the connection releases it.
- **Page edits reach every terminal.** Pages edited on any terminal are saved on the server and pushed to the others. Saving needs a manager on that terminal.
- **Lost connection.** A terminal that loses the server shows *Reconnecting…*, keeps trying, and returns to the login page when the server is back.
- **Responsiveness.** Terminals never wait on the network. Button actions continue when the server answers, and touches are ignored until then.
- **Printing.** All printing happens at the server's printers. Each terminal has its own drawer and prints receipts on the printer set for it in Manager → Terminals (default: the "receipt" printer).
- **Security.** Connections are encrypted (TLS 1.2, ECDHE-PSK with ChaCha20-Poly1305). Each paired device proves itself with its own 256-bit key, and the server with the same key, so no certificates are needed and a stranger's device can't connect or listen in. Pairing codes are stretched (PBKDF2, 600,000 rounds), so they can't be guessed from a recorded pairing. Open port 7719 for TCP and UDP in the server's firewall.

## Keep running if the server fails

A second store computer can be the **standby**: it keeps a live copy of everything and takes over if the main server stops.

```sh
# On the second computer, once (code from Manager -> Terminals -> Pair a Device):
./modern/build/vtmodern --standby auto --pair K7QM4-XHP2W     # or --standby 192.168.1.10
# From then on (as a service):
./modern/build/vtmodern --standby auto
```

- **Live copy.** The standby gets the whole database when it connects, then every change as the main server saves it (checks, payments, punches, settings, pages). It connects with the store's own server key, which only a standby pairing receives.
- **Taking over.** If the standby hears nothing from the main server for 20 seconds, it becomes the main server: same store, same paired screens, same check numbers. The screens show *The standby computer takes over in a few seconds*, find it on the network by the store's id and reconnect by themselves. A manager can also touch **Take Over Now** on a screen that lost the server and type their PIN.
- **The old main comes back as the standby.** A server that starts and finds the store already served on the network becomes the standby. A main server that was only cut off (a broken cable) sees the newer main on the network and steps down the same way. Either way it first keeps its own data as `backups/…-before-standby.db`, so nothing it took while cut off is lost for good.
- **Manager → Network** shows whether the standby is in sync, which screens are connected and who is on them, and whether each printer printed or failed last.

Run the main server and the standby as headless services on two computers, both on port 7719.

## Kitchen display and takeout / delivery

- **Kitchen and bar displays.** Manager → Kitchen Display / Bar Display, or start a terminal with `--page kitchen` or `--page bar-display`. These screens need no login.
  - Each Send is one ticket, oldest first, turning amber after 5 minutes and red after 10.
  - Touch a ticket when it's ready. Recall brings the last one back.
  - Each station bumps only its own lines, so the bar clearing drinks leaves the kitchen's food on its screen.
  - Paid-first counter orders stay on screen until they are bumped.
- **Takeout / Delivery** (floor plan) asks for the customer: name, phone, address and a note. The details save as you type. They print on the receipt and kitchen tickets and show on the order, the kitchen card and the check list. A takeout or delivery that is put away with nothing ordered is discarded.

## Prices by order type, staff meals

- **Takeout and delivery prices:** Manager → Menu → *Takeout price* and *Delivery price* (0 = the regular price; delivery falls back on the takeout price). An item rung on a takeout or delivery check takes that price; the kiosk's *To Go* orders too.
- **Staff meals:** a discount payment type can be a *Staff meal* (Manager → Payment Types). The demo has **Staff Meal**, 50% off: ring the meal and touch Staff Meal on the payment screen. It records who ate (the person ringing it), and the Sales report shows *of which staff meals* under discounts.
- **Extra costs extra:** Store Settings → *Extra: percent added* (50 = half again) and/or *Extra: amount added* (e.g. $0.75, even on a free choice). Touching **Extra** before an item or a modifier charges it; the line keeps that price.
- **Items that aren't discounted:** *No discounts* (discounts and comps leave it out) and *No staff discount* (staff pay full price; the demo's beer and wine).

## Cash rounding

Where pennies are gone (Canada, and elsewhere): Manager → Taxes → *Cash rounding* → nearest 5 or 10 cents. A check paid in cash is rounded (what is owed when the cash comes: $17.62 → $17.60, $17.63 → $17.65); touching **Cash** with no amount typed takes the rounded balance. The check, the receipt and the Sales report show a *Cash rounding* line. Card payments are exact; a check paid partly by card rounds only what cash pays.

## Tips and cash handling

- **Tips** go on card payments. On the settle page, touch 15%, 18% or 20%, or type an amount and touch Amount. The receipt, the Tips report and the server's "tips owed" show them.
- **Party gratuity.** Dine-in checks with at least *min guests* get the store's gratuity (Manager → Settings; 18% for 6+ by default, 0 turns it off). Anyone can add it on the settle page; lowering an automatic one needs a manager. Gratuity is part of the check total; tips are on top.
- **Server banks** (the starter setting; Manager → Settings → *Cash handling*). Each person keeps the cash they take in their own bank, on whichever terminal they use, so terminals need no drawer.
  - A bank starts with the first cash sale at $0. To start with change on hand, type it on the Drawer page and touch **Start Bank**.
  - **Check out** at the end of the shift: Log Out → **My Bank…**, count your cash, type it, **Check Out**. It shows *Cash to turn in*, then over / short. By default, open checks have to be closed or handed over first. Turn that off in Manager → Settings (*Close all checks before checking out*), or set it per person in Manager → Employees (*Checking out with open checks*).
  - Pay outs, paid ins and tip cash-outs come out of the bank of whoever does them.
- **Expenses.** A pay out is an expense: the manager touches what it was for (Food & supplies, Produce, Ice, Cleaning, Repairs, Other; Manager → Settings → *Expense categories*), types the amount and an optional reason, and touches **Pay Out**. Reports → **Expenses** adds them up by category and lists each one.
  - Managers see every other open bank on the Drawer page and can **Count** one for a server who left. End of Day waits until every bank is counted.
- **Terminals without a drawer:** Manager → Settings → *Terminals have a cash drawer* is the default; Manager → Terminals → *Cash drawer* sets it per terminal (e.g. the host stand has none). There, people on drawer handling can't take cash (they're told to use a terminal with a drawer or their own bank), No Sale is off, and nothing kicks a drawer. People with their own bank work as usual.
- **Per person:** Manager → Employees → *Cash handling* overrides the store setting for one person: *Own bank* or *Terminal's cash drawer*. For example, servers carry banks while the counter cashier rings into the counter's drawer; the drawer opens only for the cashier's cash sales.
- **Drawers per terminal** (the other *Cash handling* choice). Each terminal starts, counts and balances its own drawer ("Bar drawer"). Cash payments need this terminal's drawer open, and End of Day waits until every drawer is counted.
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

**Menu choices, prices and 86:**
- **Modifier groups** (Manager → Modifier Groups) are the choices an item asks for: *Dressing* (choose 1), *Add a Protein* (optional, priced), *Fillings* (up to 3). Give items their groups in Manager → Menu. Ordering such an item opens the **Choose** page beside the check. Required groups must be chosen before **Done**, and **Cancel Item** takes it off. Modifier pages (the burgers' Temperature → Side) still work, so use whichever suits each item. The demo menu shows both: burgers use modifier pages. These items use groups:
  - salads: dressing, protein
  - eggs and omelettes: how cooked, fillings, toast
  - pancakes and French toast: syrup
  - add-ons on breakfast items
  - the Kids Burger: side and drink
  - drinks: soda, lemonade and juice sizes, coffee extras, tea hot or iced, draft beer choice, wine color and glass or bottle
- An item still missing a required choice can't be sent, fired or closed. Its **Choose** button on the check (amber) reopens its choices.
- **Prices by meal period** (Manager → Menu → *Prices by meal period*, e.g. `dinner = 14.50`): an item costs that during the period. Make a *Happy Hour* meal period for happy-hour prices. Items already on a check keep their price.
- **Sold out (86):** Check Options or Manager → **Sold Out (86)…**. Touch an item to 86 it or bring it back. Its menu buttons say **SOLD OUT** and can't be ordered, on every terminal. Anyone taking orders can do this.

**Managing checks** (the order screen's **Check…** tab):
- **Transfer** a check to another server, **move** it to another table, or **merge** another open check into it (items, payments and guests come along). Servers can do this to their own checks, managers to anyone's.
- **Reopen** a check closed today (managers). Its cash leaves the drawer or bank until it is closed again. A refund is a reopen: void the item or take off the payment, then close.
- Every check keeps a **history**: who transferred, moved, merged, reopened, voided or discounted it, and when. It's on the Check Options page.

**Seats and courses** (the row at the top of the check):
- **Seat − / +** sets the seat for the next items. Touch an item first to move it to another seat. Kitchen tickets and the kitchen screen show "S2".
- **Course 1 2 3** sets the course for the next items (or a touched one). **Send** sends course 1 and anything already fired. Later courses wait, marked **HOLD**, until **Fire Course 2** sends them with a "COURSE 2" ticket. Closing a check sends anything still held.
- A counter that doesn't need seats or courses hides the row with the order list's `props.controls: false`.

**Manager** (PIN 1234 → Manager):
- **Menu, Employees, Payments (Tenders), Printers, Taxes, Settings, Terminals, Meal Periods, Modifier Groups:** edit and Save.
- **Breaks and overtime:** Log Out → **Start / End Break** (clocking out ends a break too). The Labor report shows each punch's breaks and worked hours, then each person's hours today and this pay week, regular and overtime. Manager → Settings: *Breaks are paid* (off by default), *Overtime after hours in a day* (off; 8 in California), *Overtime after hours in a week* (40, the US federal rule) and *Pay week starts on*. When both rules apply, whichever gives more overtime counts.
- **Permissions per person** (Manager → Employees): the role (server, cashier, manager, admin) sets the defaults. Each permission can then be set to *As the role*, *Yes* or *No* for one person: take orders, take payments, give discounts and comps, void sent items, manager screens, edit pages. For example, a lead server who may void, or a manager who doesn't edit pages. Staff are deactivated rather than deleted. PINs must be unique, and you can't lock yourself out.
- **Reports:** Sales, Items, Categories (with each one's share), By Hour, Servers, Tips, Labor, Drawer and **Audit** (every void, discount, reopened, moved, transferred and merged check, with who and when), for today (live) or any closed day (◀ ▶). **Print** sends a report to the receipt printer. **CSV** and **PDF** save it to `exports/` in the data folder, or the folder given with `--export-dir` (e.g. a USB stick).
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
| table | One table: its text is the table's name, `props.seats` its seats. Shows free / yours / someone else's |
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

**Tables** are zones like buttons. On the Tables page press F1 (or Ctrl+E), then *+ Panel ▾ → table* to add one, or select a table and *Duplicate* (T7 becomes T8, Bar 3 becomes Bar 4). Drag and resize them, pick a shape (circle, octagon…) and colors, and set *Table name* and *Seats* in the inspector. Every table needs its own name: the name is what checks and kitchen tickets show. Floor plans saved before tables became zones (a single *tableMap* panel) turn into separate table zones when loaded, each in the same place.

Press **F1** or **Ctrl+E** (no Fn needed), or touch **Manager → Edit Pages**. Changes go into a draft. The running app keeps the saved pages until you press **Save**, which writes to the database in one transaction. **Done** asks whether to save or discard.

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
./modern/build/vtmodern --windowed        # in a window (it starts full screen; F11 or Ctrl+Shift+F switches)
./modern/build/vtmodern --kiosk           # full screen, no mouse pointer, F11 off
./modern/build/vtmodern --login 1234      # start logged in (testing)
```

Tests run headless (offscreen). They include UI tests that drag, resize and type in the real editor.

If Qt6 is not found, only the core library and tests are built. To build it from the legacy root project instead, pass `-DVT_BUILD_MODERN=ON`.
