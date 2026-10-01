#!/usr/bin/env python3
"""Generate modern/seed (theme, starter pages, POS data). One-off helper; the JSON is the source of truth."""
import json, os, re, sys

OUT = sys.argv[1]
SCHEMA = 1
os.makedirs(os.path.join(OUT, "pages"), exist_ok=True)
os.makedirs(os.path.join(OUT, "pos"), exist_ok=True)

def write(path, obj, versioned=True):
    if versioned and isinstance(obj, dict):
        obj = {"schemaVersion": SCHEMA, **obj}
    with open(os.path.join(OUT, path), "w") as f:
        json.dump(obj, f, indent=2)
        f.write("\n")

def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")

def rect(x, y, w, h):
    return {"x": x, "y": y, "w": w, "h": h}

def jump(page=None, role=None, mode="push"):
    a = {"type": "jump", "mode": mode}
    if page: a["page"] = page
    if role: a["role"] = role
    return a

def command(name, **args):
    a = {"type": "command", "name": name}
    if args: a["args"] = args
    return a

def fill(color, **more):
    return {"normal": {"fill": color, **more}}

def zone(id, x, y, w, h, label="", kind="button", actions=(), **kw):
    z = {"id": id, "kind": kind, "rect": rect(x, y, w, h), "shape": kw.pop("shape", "rect"),
         "behavior": kw.pop("behavior", "blink" if kind in ("button", "image") else "none")}
    if label: z["label"] = label
    if actions: z["actions"] = list(actions)
    z.update(kw)
    return z

def label(id, x, y, w, h, text, **kw):
    return zone(id, x, y, w, h, text, kind="label", **kw)

def page(id, name, kind, zones, **kw):
    p = {"id": id, "name": name, "kind": kind, "canvas": {"w": 1920, "h": 1080}, "grid": 8}
    p.update(kw)
    p["zones"] = zones
    write(f"pages/{id}.json", p)

# ---------------------------------------------------------------- POS data
MENU = []
def menu(name, price, family, tax="food", modifier=False, printer="kitchen"):
    item = {"id": slug(name), "name": name, "price": price, "family": family, "taxClass": tax,
            "printer": printer}
    if modifier:
        item["modifier"] = True
    MENU.append(item)
    return item["id"]

BURGERS = [("Classic Burger", 11.50), ("Cheeseburger", 12.25), ("Bacon Burger", 13.50),
           ("Mushroom Swiss", 13.25), ("Veggie Burger", 12.00), ("Kids Burger", 7.50),
           ("Burger of the Day", 14.00)]
SALADS = [("House Salad", 8.50), ("Caesar", 9.75), ("Cobb", 12.50), ("Greek", 10.25)]
DRINKS = [("Coffee", 2.75, "food"), ("Tea", 2.50, "food"), ("Soda", 2.95, "food"), ("Juice", 3.50, "food"),
          ("Water", 0.00, "food"), ("Lemonade", 3.25, "food"), ("Draft Beer", 6.00, "alcohol"),
          ("House Wine", 8.00, "alcohol")]
BREAKFAST = [("Two Eggs", 8.95), ("Pancakes", 9.50), ("French Toast", 9.75), ("Omelette", 11.25)]
TEMPS = ["Rare", "Medium Rare", "Medium", "Medium Well", "Well Done"]
SIDES = [("Fries", 0.00), ("Side Salad", 0.00), ("Onion Rings", 1.00), ("Sweet Potato Fries", 1.50),
         ("No Side", 0.00)]

for n, p in BURGERS: menu(n, p, "burgers")
for n, p in SALADS: menu(n, p, "salads")
for n, p, t in DRINKS: menu(n, p, "drinks", tax=t, printer="bar")
for n, p in BREAKFAST: menu(n, p, "breakfast")

# Choices asked for when an item is ordered (the Choose page). Burgers keep
# their Temperature -> Side pages: both ways work.
def item(id):
    return next(m for m in MENU if m["id"] == id)
for sid in ("house-salad", "greek"):
    item(sid)["modifierGroups"] = ["dressing", "salad-protein"]
for sid in ("caesar", "cobb"):
    item(sid)["modifierGroups"] = ["salad-protein"]
item("two-eggs")["modifierGroups"] = ["eggs", "toast", "breakfast-add-ons"]
item("omelette")["modifierGroups"] = ["omelette-fillings", "toast", "breakfast-add-ons"]
for bid in ("pancakes", "french-toast"):
    item(bid)["modifierGroups"] = ["syrup", "breakfast-add-ons"]
item("kids-burger")["modifierGroups"] = ["kids-side", "kids-drink"]
for did in ("soda", "lemonade", "juice"):
    item(did)["modifierGroups"] = ["drink-size"]
item("coffee")["modifierGroups"] = ["coffee-extras"]
item("tea")["modifierGroups"] = ["hot-or-iced"]
item("draft-beer")["modifierGroups"] = ["draft"]
item("house-wine")["modifierGroups"] = ["wine", "wine-pour"]
# Dinner portions and the evening wine price.
for bid, price in (("classic-burger", 12.50), ("bacon-burger", 14.50), ("house-wine", 9.00)):
    item(bid)["periodPrices"] = {"dinner": price}
for n in TEMPS: menu(n, 0.00, "temperature", modifier=True)
for n, p in SIDES: menu(n, p, "sides", modifier=True)
write("pos/menu.json", MENU, versioned=False)

write("pos/employees.json", [
    {"id": "manager", "name": "Morgan (Manager)", "role": "manager", "pin": "1234"},
    {"id": "sam", "name": "Sam", "role": "server", "pin": "1111"},
    {"id": "casey", "name": "Casey", "role": "cashier", "pin": "2222"},
], versioned=False)

write("pos/settings.json", {
    "storeName": "ViewTouch Café",
    "currencySymbol": "$",
    "tax": {"food": 8.25, "alcohol": 10.0, "merchandise": 8.25, "room": 0, "taxTakeoutFood": True},
    "tenders": [
        {"id": "cash", "name": "Cash", "kind": "cash"},
        {"id": "credit", "name": "Credit Card", "kind": "card"},
        {"id": "gift", "name": "Gift Card", "kind": "giftcard"},
        {"id": "house", "name": "House Account", "kind": "house"},
        {"id": "discount", "name": "10% Discount", "kind": "discount", "percent": 10},
        {"id": "comp", "name": "Comp", "kind": "discount", "percent": 100},
    ],
    # "file" printers write text under <app data>/printouts so tickets can be
    # seen without hardware. Switch them to network/CUPS in Manager -> Printers.
    "printers": [
        {"id": "receipt", "name": "Receipt", "type": "file", "path": "receipt.txt", "width": 42,
         "cutter": True, "drawerKick": True},
        {"id": "kitchen", "name": "Kitchen", "type": "file", "path": "kitchen.txt", "width": 42,
         "cutter": True, "drawerKick": False},
        {"id": "bar", "name": "Bar", "type": "file", "path": "bar.txt", "width": 42,
         "cutter": True, "drawerKick": False},
    ],
    "receiptHeader": "123 Main Street\nOpen daily 7am - 10pm",
    "gratuity": {"percent": 18, "minGuests": 6},
    "terminals": [],
    # Servers carry their own bank; terminals need no drawer of their own.
    "cashMode": "serverBank",
    "modifierGroups": [
        {"id": "dressing", "name": "Dressing", "min": 1, "max": 1,
         "options": [{"name": n, "price": 0} for n in ("Ranch", "Blue Cheese", "Balsamic", "Caesar", "Oil & Vinegar")]},
        {"id": "salad-protein", "name": "Add a Protein", "min": 0, "max": 1,
         "options": [{"name": "Grilled Chicken", "price": 4.00}, {"name": "Shrimp", "price": 5.00},
                     {"name": "Salmon", "price": 6.00}]},
        {"id": "eggs", "name": "Eggs", "min": 1, "max": 1,
         "options": [{"name": n, "price": 0} for n in ("Scrambled", "Over Easy", "Over Medium", "Sunny Side Up", "Poached")]},
        {"id": "toast", "name": "Toast", "min": 1, "max": 1,
         "options": [{"name": n, "price": 0} for n in ("White", "Wheat", "Sourdough", "Rye", "English Muffin")]},
        {"id": "syrup", "name": "Syrup", "min": 1, "max": 1,
         "options": [{"name": "Maple", "price": 0}, {"name": "Blueberry", "price": 0},
                     {"name": "Strawberry", "price": 0}, {"name": "Real Maple", "price": 1.50}]},
        {"id": "breakfast-add-ons", "name": "Add-ons", "min": 0, "max": 0,
         "options": [{"name": "Bacon", "price": 3.00}, {"name": "Sausage", "price": 3.00},
                     {"name": "Fruit Cup", "price": 2.50}, {"name": "Hash Browns", "price": 2.75},
                     {"name": "Whipped Cream", "price": 0.75}]},
        {"id": "kids-side", "name": "Kids Side", "min": 1, "max": 1,
         "options": [{"name": n, "price": 0} for n in ("Fries", "Apple Slices", "Fruit Cup", "Carrot Sticks")]},
        {"id": "kids-drink", "name": "Kids Drink", "min": 0, "max": 1,
         "options": [{"name": "Milk", "price": 0}, {"name": "Apple Juice", "price": 0},
                     {"name": "Chocolate Milk", "price": 0.50}]},
        {"id": "drink-size", "name": "Size", "min": 1, "max": 1,
         "options": [{"name": "Small", "price": 0}, {"name": "Medium", "price": 0.50},
                     {"name": "Large", "price": 1.00}]},
        {"id": "coffee-extras", "name": "Coffee", "min": 0, "max": 0,
         "options": [{"name": "Cream", "price": 0}, {"name": "Oat Milk", "price": 0.75},
                     {"name": "Sugar", "price": 0}, {"name": "Sweetener", "price": 0},
                     {"name": "Extra Shot", "price": 1.25}, {"name": "Decaf", "price": 0}]},
        {"id": "hot-or-iced", "name": "Hot or Iced", "min": 1, "max": 1,
         "options": [{"name": "Hot", "price": 0}, {"name": "Iced", "price": 0}]},
        {"id": "draft", "name": "Draft", "min": 1, "max": 1,
         "options": [{"name": "IPA", "price": 0}, {"name": "Lager", "price": 0}, {"name": "Stout", "price": 0},
                     {"name": "Seasonal", "price": 1.00}]},
        {"id": "wine", "name": "Wine", "min": 1, "max": 1,
         "options": [{"name": n, "price": 0} for n in ("Red", "White", "Rosé")]},
        {"id": "wine-pour", "name": "Pour", "min": 1, "max": 1,
         "options": [{"name": "Glass", "price": 0}, {"name": "Bottle", "price": 22.00}]},
        {"id": "omelette-fillings", "name": "Fillings", "min": 0, "max": 3,
         "options": [{"name": "Cheese", "price": 0}, {"name": "Ham", "price": 1.00}, {"name": "Mushrooms", "price": 0},
                     {"name": "Peppers", "price": 0}, {"name": "Onions", "price": 0}, {"name": "Spinach", "price": 0},
                     {"name": "Bacon", "price": 1.50}]},
    ],
    "mealPeriods": [{"id": "breakfast", "name": "Breakfast", "start": "04:00"},
                    {"id": "lunch", "name": "Lunch", "start": "11:00"},
                    {"id": "dinner", "name": "Dinner", "start": "16:00"}],
    "receiptFooter": "Thank you for visiting!\nPowered by ViewTouch",
})

# ---------------------------------------------------------------- theme
WIDGETS = ["orderList", "loginPad", "guestCount", "numPad", "paymentPanel",
           "logoutPanel", "clock", "checkList", "keyboard", "statusBar",
           "adminPanel", "reportView", "drawerPanel", "endOfDay", "splitCheck", "customerInfo",
           "customerLookup", "giftCard", "waitlist"]
widget_style = {"normal": {"fill": "#232933", "frame": "flat", "shadow": 0, "radius": 12,
                           "textColor": "#e6e9ef", "fontSize": 28, "bold": False}}
write("theme.json", {
    "name": "ViewTouch Dark",
    "background": {"fill": "#171a1f"},
    "style": {
        "normal": {"fill": "#2d3440", "textColor": "#f2f4f7", "font": "DejaVu Sans",
                   "fontSize": 30, "bold": True, "frame": "raised", "frameWidth": 3,
                   "radius": 14, "shadow": 5, "textStyle": "none"},
        "selected": {"fill": "#2f6fd6", "frame": "inset", "shadow": 2},
        "disabled": {"opacity": 0.35},
    },
    "kinds": {
        "label": {"normal": {"fill": "transparent", "frame": "none", "shadow": 0,
                             "fontSize": 44, "textColor": "#e6e9ef"},
                  "selected": {"fill": "transparent", "frame": "none"}},
        "comment": {"normal": {"fill": "#fff3b0", "textColor": "#3d3200", "frame": "flat",
                               "shadow": 0, "fontSize": 20, "bold": False}},
        "image": {"normal": {"fill": "#f5efe0", "textColor": "#2b2b2b"}},
        **{w: widget_style for w in WIDGETS},
    },
})

GREEN, RED, BLUE, AMBER, TEAL, PURPLE = "#1f8a4c", "#b83232", "#2b62b0", "#a86a12", "#1f6f73", "#6b46c1"

# ---------------------------------------------------------------- order template
flow = [
    ("flow-tables", "Tables", [command("releaseCheck"), jump(role="tables", mode="replace")], {}),
    ("flow-no", "No", [{"type": "qualifier", "qualifier": "no"}], {}),
    ("flow-extra", "Extra", [{"type": "qualifier", "qualifier": "extra"}], {}),
    ("flow-lite", "Lite", [{"type": "qualifier", "qualifier": "lite"}], {}),
    ("flow-side", "Side", [{"type": "qualifier", "qualifier": "side"}], {}),
    ("flow-void", "Void", [command("voidItem")], {"behavior": "double", "style": fill(RED)}),
    ("flow-send", "Send", [command("sendOrder")], {"style": fill(GREEN), "hotkey": "s"}),
    ("flow-pay", "Pay", [jump(role="settle")], {"style": fill(BLUE), "hotkey": "p"}),
]
tmpl = [zone("order-list", 16, 16, 560, 948, kind="orderList")]
for i, (zid, text, acts, kw) in enumerate(flow):
    tmpl.append(zone(zid, 16 + i * 237, 980, 229, 84, text, actions=acts, **kw))
for i, (zid, text, target) in enumerate([("tab-breakfast", "Breakfast", "index-breakfast"),
                                          ("tab-lunch", "Lunch", "index-lunch"),
                                          ("tab-dinner", "Dinner", "index-dinner")]):
    tmpl.append(zone(zid, 592 + i * 258, 16, 250, 72, text, actions=[jump(page=target, mode="replace")],
                     style={"normal": {"fontSize": 26}}))
tmpl.append(zone("tab-categories", 1374, 16, 170, 72, "‹ Menu", actions=[jump(mode="index")],
                 style={"normal": {"fontSize": 26}}))
tmpl.append(zone("tab-note", 1560, 16, 160, 72, "Note", actions=[jump(page="note")],
                 style={"normal": {"fontSize": 26}}))
# Transfer, move, merge, reopen, and the check's history.
tmpl.append(zone("tab-check", 1736, 16, 168, 72, "Check…", actions=[jump(page="check-options")],
                 style={"normal": {"fontSize": 26}}))
page("order-template", "Order Template", "template", tmpl)

# ---------------------------------------------------------------- index pages
def index_page(id, name, period, cats):
    zs = [label("title", 592, 104, 1312, 72, name)]
    for i, (text, target, color) in enumerate(cats):
        col, row = i % 3, i // 3
        zs.append(zone(f"cat-{target}", 592 + col * 444, 192 + row * 260, 424, 240, text,
                       actions=[jump(page=target, mode="replace")], style=fill(color)))
    page(id, name, "index", zs, templateId="order-template", mealPeriod=period)

index_page("index-breakfast", "Breakfast", "breakfast",
           [("Plates", "items-breakfast", AMBER), ("Drinks", "items-drinks", TEAL)])
index_page("index-lunch", "Lunch", "lunch",
           [("Burgers", "items-burgers", AMBER), ("Salads", "items-salads", GREEN),
            ("Drinks", "items-drinks", TEAL)])
index_page("index-dinner", "Dinner", "dinner",
           [("Burgers", "items-burgers", AMBER), ("Salads", "items-salads", GREEN),
            ("Drinks", "items-drinks", TEAL)])

# ---------------------------------------------------------------- item pages
def add(name, seq=None):
    a = {"type": "addItem", "item": slug(name)}
    if seq: a["modifierSequence"] = seq
    return a

def item_page(id, name, items, color, shape="rounded", cols=4, cell=(316, 180), extra=()):
    zs = [label("title", 592, 104, 1312, 72, name)]
    w, h = cell
    gap_x = (1312 - cols * w) // max(cols - 1, 1)
    for i, (text, seq) in enumerate(items):
        col, row = i % cols, i // cols
        zs.append(zone(f"item-{i + 1}", 592 + col * (w + gap_x), 192 + row * (h + 16), w, h, text,
                       actions=[add(text, seq)], shape=shape, style=fill(color)))
    zs.extend(extra)
    page(id, name, "items", zs, templateId="order-template")

BURGER_MODS = ["mod-temperature", "mod-side"]
item_page("items-burgers", "Burgers",
          [(n, BURGER_MODS if n not in ("Veggie Burger", "Kids Burger", "Burger of the Day")
            else (["mod-side"] if n == "Veggie Burger" else None))
           for n, _ in BURGERS[:6]],
          AMBER,
          extra=[zone("burger-photo", 592, 596, 316, 260, "Burger of the Day", kind="image",
                      imagePath="qrc:/images/burger.png",
                      actions=[add("Burger of the Day", BURGER_MODS)]),
                 zone("note", 1240, 596, 664, 120,
                      "Burgers run Temperature, then Side, then return here.", kind="comment")])
item_page("items-salads", "Salads", [(n, None) for n, _ in SALADS], GREEN, shape="hexagon")
item_page("items-drinks", "Drinks", [(n, None) for n, _, _ in DRINKS], TEAL, shape="circle", cols=5,
          cell=(200, 200))
item_page("items-breakfast", "Breakfast Plates", [(n, None) for n, _ in BREAKFAST], AMBER, shape="octagon",
          cell=(316, 220))

# ---------------------------------------------------------------- modifier pages
def modifier_page(id, name, question, options):
    zs = [label("title", 592, 104, 1312, 72, question)]
    for i, text in enumerate(options):
        col, row = i % 3, i // 3
        zs.append(zone(f"opt-{i + 1}", 592 + col * 444, 192 + row * 196, 424, 180, text,
                       actions=[add(text), jump(mode="sequence")]))
    zs.append(zone("skip", 1480, 800, 424, 120, "Skip ›", actions=[jump(mode="sequence")],
                   style={"normal": {"fill": "#3a4250", "fontSize": 28}}))
    page(id, name, "modifier", zs, templateId="order-template")

modifier_page("mod-temperature", "Temperature", "How should it be cooked?", TEMPS)
modifier_page("mod-side", "Side", "Choose a side", [n for n, _ in SIDES])

# Free-text note for the kitchen
page("note", "Note", "custom", [
    label("title", 592, 104, 1312, 72, "Note for the kitchen"),
    zone("keyboard", 592, 192, 1312, 560, kind="keyboard"),
    zone("cancel", 592, 800, 420, 120, "Cancel", actions=[jump(mode="back")]),
    zone("add-note", 1484, 800, 420, 120, "Add Note", actions=[command("addComment"), jump(mode="back")],
         style=fill(GREEN)),
], templateId="order-template")

# ---------------------------------------------------------------- system pages
page("login", "Login", "login", [
    label("title", 460, 40, 1000, 110, "ViewTouch", style={"normal": {"fontSize": 72}}),
    zone("clock", 660, 160, 600, 80, kind="clock"),
    zone("login-pad", 660, 260, 600, 600, kind="loginPad"),
    zone("clock-in", 300, 880, 344, 120, "Clock In", actions=[command("clockIn")]),
    zone("start", 660, 880, 600, 120, "Log In", actions=[command("login")], style=fill(GREEN)),
    zone("clock-out", 1276, 880, 344, 120, "Clock Out", actions=[command("clockOut")]),
    zone("hint", 1300, 260, 560, 200,
         "Demo PINs: 1234 manager, 1111 server, 2222 cashier. Remove this note in the editor.",
         kind="comment"),
], role="login")

TABLES = [
    {"label": "T1", "x": 80, "y": 80, "w": 200, "h": 200, "shape": "circle", "seats": 2},
    {"label": "T2", "x": 360, "y": 80, "w": 200, "h": 200, "shape": "circle", "seats": 2},
    {"label": "T3", "x": 640, "y": 80, "w": 320, "h": 200, "shape": "rect", "seats": 4},
    {"label": "T4", "x": 1040, "y": 80, "w": 320, "h": 200, "shape": "rect", "seats": 4},
    {"label": "T5", "x": 80, "y": 400, "w": 480, "h": 220, "shape": "rect", "seats": 6},
    {"label": "T6", "x": 640, "y": 400, "w": 240, "h": 240, "shape": "octagon", "seats": 4},
    {"label": "T7", "x": 960, "y": 400, "w": 240, "h": 240, "shape": "octagon", "seats": 4},
    {"label": "Bar 1", "x": 80, "y": 760, "w": 240, "h": 160, "shape": "rounded", "seats": 1},
    {"label": "Bar 2", "x": 360, "y": 760, "w": 240, "h": 160, "shape": "rounded", "seats": 1},
    {"label": "Bar 3", "x": 640, "y": 760, "w": 240, "h": 160, "shape": "rounded", "seats": 1},
]
# Each table is its own zone: move, resize, reshape or restyle it in the
# editor, or add more with + Panel -> table.
def table_zone(t):
    tid = "table-" + t["label"].lower().replace(" ", "-")
    return zone(tid, 16 + t["x"], 16 + t["y"], t["w"], t["h"], t["label"], kind="table",
                shape=t["shape"], props={"seats": t["seats"]})

page("tables", "Tables", "tables", [
    *[table_zone(t) for t in TABLES],
    zone("quick", 1472, 16, 432, 150, "Quick Order", actions=[command("startQuick"), jump(mode="index")],
         style=fill(GREEN)),
    zone("takeout", 1472, 182, 208, 150, "Takeout", actions=[command("startTakeout"), jump(page="customer")]),
    zone("delivery", 1696, 182, 208, 150, "Delivery", actions=[command("startDelivery"), jump(page="customer")]),
    zone("checks", 1472, 348, 432, 150, "Open Checks", actions=[jump(role="checkList")]),
    zone("host", 1056, 914, 400, 150, "Waitlist", actions=[jump(page="host")], style=fill(TEAL)),
    zone("status", 1472, 514, 432, 218, kind="logoutPanel"),
    zone("manager", 1472, 748, 432, 150, "Manager", actions=[jump(role="manager")]),
    zone("logout", 1472, 914, 432, 150, "Log Out", actions=[jump(role="logout")], style=fill(RED)),
], role="tables", background={"texture": "woodfloor", "fill": "#3b2a1a"})

page("customer", "Customer", "custom", [
    label("title", 16, 16, 1888, 80, "Who is the order for?"),
    zone("customer", 16, 112, 1888, 800, kind="customerInfo"),
    zone("cancel", 16, 944, 432, 120, "Cancel", actions=[command("releaseCheck"), jump(mode="back")]),
    zone("menu", 1472, 944, 432, 120, "Continue to Menu ›", actions=[jump(mode="index")], style=fill(GREEN)),
])

# Choices for the item just ordered (its modifier groups). Inside the order
# screen, like item pages, so phones frame it too.
page("modifiers", "Choose", "modifier", [
    zone("choices", 592, 104, 1312, 860, kind="modifierPicker"),
], templateId="order-template", role="modifiers")

# Sold out (86): from Check Options or the Manager page.
page("sold-out", "Sold Out (86)", "custom", [
    label("title", 16, 16, 1888, 80, "Touch an item to mark it sold out, or back on"),
    zone("list", 16, 112, 1888, 816, kind="soldOutList"),
    zone("back", 16, 944, 432, 120, "‹ Back", actions=[jump(mode="back")]),
], permission="order")

# --- the host stand ---
page("host", "Host Stand", "custom", [
    label("title", 16, 16, 1888, 80, "Waitlist & reservations"),
    zone("waitlist", 16, 112, 1888, 816, kind="waitlist"),
    zone("back", 16, 944, 432, 120, "‹ Tables", actions=[jump(role="tables")]),
    zone("customers", 1472, 944, 432, 120, "Customers…", actions=[jump(page="customers")]),
], permission="order")

# --- customers and gift cards ---
page("customers", "Customers", "custom", [
    label("title", 16, 16, 1888, 80, "Customers"),
    zone("customers", 16, 112, 1888, 816, kind="customerLookup"),
    zone("back", 16, 944, 432, 120, "‹ Back", actions=[jump(mode="back")]),
    zone("gift-card", 1472, 944, 432, 120, "Gift Cards…", actions=[jump(page="gift-card")]),
], permission="order")
page("gift-card", "Gift Card", "custom", [
    label("title", 16, 16, 1888, 80, "Gift cards"),
    zone("card", 16, 112, 1888, 816, kind="giftCard"),
    zone("back", 16, 944, 432, 120, "‹ Back", actions=[jump(mode="back")]),
    zone("pay", 1472, 944, 432, 120, "Pay ›", actions=[jump(role="settle")], style=fill(BLUE)),
], permission="order")

# --- managing a check (from the order screen's Check… tab) ---
page("check-options", "Check Options", "custom", [
    label("title", 16, 16, 1888, 80, "This check"),
    zone("history", 16, 112, 900, 952, kind="checkHistory"),
    zone("customer", 932, 112, 972, 104, "Customer…", actions=[jump(page="customers")]),
    zone("gift-card", 932, 230, 972, 104, "Sell / Check a Gift Card…", actions=[jump(page="gift-card")]),
    zone("transfer", 932, 348, 972, 104, "Transfer to Another Server…", actions=[jump(page="transfer")]),
    zone("move", 932, 466, 972, 104, "Move to Another Table…", actions=[jump(page="move-table")]),
    zone("merge", 932, 584, 972, 104, "Merge Another Check Into This One…", actions=[jump(page="merge")]),
    zone("reopen", 932, 702, 972, 104, "Reopen a Closed Check… (manager)", actions=[jump(page="closed-checks")]),
    zone("sold-out", 932, 820, 972, 104, "Sold Out (86)…", actions=[jump(page="sold-out")]),
    zone("back", 932, 944, 972, 120, "‹ Back to the Order", actions=[jump(mode="back")]),
], permission="order")
page("transfer", "Transfer Check", "custom", [
    label("title", 16, 16, 1888, 80, "Give this check to…"),
    zone("staff", 16, 112, 1888, 816, kind="staffPicker"),
    zone("back", 16, 944, 432, 120, "‹ Back", actions=[jump(mode="back")]),
], permission="order")
page("move-table", "Move Check", "custom", [
    label("title", 16, 16, 1888, 80, "Move this check to table…"),
    zone("tables", 16, 112, 1888, 816, kind="tableGrid", props={"action": "move", "columns": 6}),
    zone("back", 16, 944, 432, 120, "‹ Back", actions=[jump(mode="back")]),
], permission="order")
page("merge", "Merge Checks", "custom", [
    label("title", 16, 16, 1888, 80, "Merge which check into this one?"),
    zone("list", 16, 112, 1888, 816, kind="checkList", props={"mode": "merge"}),
    zone("back", 16, 944, 432, 120, "‹ Back", actions=[jump(mode="back")]),
], permission="order")
page("closed-checks", "Closed Checks", "custom", [
    label("title", 16, 16, 1888, 80, "Checks closed today: touch one to reopen it"),
    zone("list", 16, 112, 1888, 816, kind="checkList", props={"mode": "closed"}),
    zone("back", 16, 944, 432, 120, "‹ Back", actions=[jump(mode="back")]),
], permission="manager")

page("check-list", "Open Checks", "custom", [
    label("title", 16, 16, 1440, 80, "Open checks"),
    zone("list", 16, 112, 1440, 952, kind="checkList"),
    zone("back", 1472, 914, 432, 150, "‹ Back", actions=[jump(mode="back")]),
], role="checkList")

page("guest-count", "Guest Count", "guestCount", [
    label("title", 560, 32, 800, 80, "How many guests?"),
    zone("guests", 660, 128, 600, 160, kind="guestCount"),
    zone("pad", 660, 304, 600, 560, kind="numPad"),
    zone("cancel", 660, 888, 290, 120, "Cancel", actions=[command("releaseCheck"), jump(mode="back")]),
    zone("start", 970, 888, 290, 120, "Start Order", actions=[command("startCheck"), jump(mode="index")],
         style=fill(GREEN)),
], role="guestCount")

# Gift Card opens its page (the card number first); the rest pay right away.
tenders = [("Cash", "cash", GREEN), ("Credit Card", "credit", BLUE), ("Gift Card…", "gift", TEAL),
           ("House Account", "house", TEAL), ("10% Off", "discount", AMBER), ("Comp", "comp", PURPLE)]


def tender_action(tid):
    return [jump(page="gift-card")] if tid == "gift" else [{"type": "tender", "tender": tid}]
settle = [zone("payment", 16, 16, 900, 1048, kind="paymentPanel"),
          zone("pad", 932, 16, 520, 620, kind="numPad", props={"mode": "amount"})]
for i, (text, tid, color) in enumerate(tenders):
    settle.append(zone(f"tender-{tid}", 1468, 16 + i * 104, 436, 92, text,
                       actions=tender_action(tid), style=fill(color)))
settle += [
    zone("receipt", 932, 652, 520, 120, "Print Receipt", actions=[command("printReceipt")]),
    zone("close", 932, 788, 520, 120, "Close Check", actions=[command("closeCheck")], style=fill(GREEN)),
    zone("remove-payment", 932, 924, 520, 120, "Undo Payment", actions=[command("removePayment")]),
    zone("split", 1468, 640, 212, 100, "Split", actions=[jump(page="split")]),
    zone("drawer", 1692, 640, 212, 100, "Drawer…", actions=[jump(page="drawer")]),
    # On the customer display: the guest picks a tip, which goes on their card.
    zone("ask-tip", 1468, 756, 436, 100, "Ask Guest for Tip", actions=[command("askForTip")], style=fill(TEAL)),
    zone("done", 1468, 944, 436, 120, "‹ Back to Order", actions=[jump(mode="back")]),
]
page("settle", "Settle", "settle", settle, role="settle", permission="check.settle")

page("split", "Split Check", "custom", [
    zone("split-check", 16, 16, 1888, 932, kind="splitCheck"),
    zone("back", 16, 964, 432, 100, "‹ Back", actions=[jump(mode="back")]),
], permission="order")

page("logout", "Log Out", "logout", [
    label("title", 560, 40, 800, 90, "End of shift"),
    zone("panel", 560, 150, 800, 400, kind="logoutPanel"),
    zone("clock-out", 560, 580, 390, 140, "Clock Out", actions=[command("clockOut")]),
    zone("tips", 560, 900, 390, 120, "Cash Out My Tips", actions=[command("cashOutTips")], style=fill(GREEN)),
    # Server banks: check out (count your cash); with drawers: this terminal's drawer.
    zone("bank", 970, 900, 390, 120, "My Bank…", actions=[jump(page="drawer")]),
    zone("break", 970, 580, 390, 140, "Start / End Break", actions=[command("toggleBreak")]),
    zone("logout", 560, 740, 390, 140, "Log Out", actions=[command("logout")], style=fill(RED)),
    zone("cancel", 970, 740, 390, 140, "Cancel", actions=[jump(mode="back")]),
], role="logout")

admin = [("Menu", "menu"), ("Employees", "employees"), ("Settings", "settings"), ("Taxes", "taxes"),
         ("Tenders", "tenders"), ("Printers", "printers"), ("Reports", "reports"), ("Banks & Drawers", "drawers"),
         ("End of Day", "endOfDay"), ("Terminals", "terminals"), ("Meal Periods", "mealPeriods"),
         ("Modifier Groups", "modifierGroups")]
mgr = [label("title", 160, 40, 1600, 100, "Manager")]
for i, (text, panel) in enumerate(admin):
    col, row = i % 4, i // 4
    mgr.append(zone(f"admin-{panel}", 160 + col * 408, 160 + row * 180, 384, 160, text,
                    actions=[command("openAdmin", panel=panel)]))
mgr += [
    # grid slots 12 and 13 (row 3), then 14 and 15
    zone("kitchen-display", 160, 700, 384, 160, "Kitchen Display", actions=[jump(page="kitchen")]),
    zone("bar-display", 568, 700, 384, 160, "Bar Display", actions=[jump(page="bar-display")]),
    zone("sold-out", 1384, 880, 384, 160, "Sold Out (86)…", actions=[jump(page="sold-out")]),
    zone("customers", 568, 880, 384, 160, "Customers…", actions=[jump(page="customers")]),
    zone("gift-cards", 976, 880, 384, 160, "Gift Cards…", actions=[jump(page="gift-card")]),
    zone("edit-pages", 1384, 700, 384, 160, "Edit Pages", actions=[command("editMode")], style=fill(BLUE)),
    # Touch twice. On a kiosk screen it stays closed until the next boot.
    zone("close-app", 976, 700, 384, 160, "Close ViewTouch", actions=[command("closeApp")], behavior="double",
         style=fill(RED)),
    zone("back", 160, 900, 384, 140, "‹ Back", actions=[jump(mode="back")]),
]
page("manager", "Manager", "manager", mgr, role="manager", permission="manager")

# Manager screens (reached through openAdmin from the Manager page)
for pid, name, panel in [("admin-menu", "Menu Items", "menu"), ("admin-employees", "Employees", "employees"),
                         ("admin-tenders", "Payment Types", "tenders"), ("admin-printers", "Printers", "printers"),
                         ("admin-taxes", "Taxes", "taxes"), ("admin-store", "Store Settings", "store"),
                         ("admin-terminals", "Terminals", "terminals"),
                         ("admin-meal-periods", "Meal Periods", "mealPeriods"),
                         ("admin-modifier-groups", "Modifier Groups", "modifierGroups")]:
    page(pid, name, "manager", [
        label("title", 16, 16, 1888, 80, name),
        zone("editor", 16, 112, 1888, 816, kind="adminPanel", props={"panel": panel}),
        zone("back", 16, 944, 432, 120, "‹ Manager", actions=[jump(mode="back")]),
    ], permission="manager")

page("reports", "Reports", "manager", [
    zone("report", 16, 16, 1440, 1048, kind="reportView"),
    zone("back", 1472, 944, 432, 120, "‹ Manager", actions=[jump(mode="back")]),
], permission="manager")

page("drawer", "Drawer", "manager", [
    zone("drawer", 16, 16, 900, 1048, kind="drawerPanel"),
    zone("pad", 932, 16, 520, 620, kind="numPad", props={"mode": "amount"}),
    zone("reason", 932, 652, 520, 120, "Reason…", actions=[jump(page="drawer-reason")]),
    zone("back", 1472, 944, 432, 120, "‹ Back", actions=[jump(mode="back")]),
], permission="check.settle")

# Why cash left or entered the drawer; Pay Out / Paid In use the typed text.
page("drawer-reason", "Drawer Reason", "manager", [
    label("title", 16, 16, 1888, 80, "Reason for the pay out or paid in"),
    zone("keyboard", 16, 112, 1888, 760, kind="keyboard", props={"placeholder": "Vendor, ice, change…"}),
    zone("cancel", 16, 944, 432, 120, "Cancel", actions=[command("clearText"), jump(mode="back")]),
    zone("done", 1472, 944, 432, 120, "Done", actions=[jump(mode="back")], style=fill(GREEN)),
], permission="manager")

# Kitchen and bar displays. "public": they run without anyone logged in;
# start a kitchen screen with --connect <server> --page kitchen.
KDS = {"normal": {"fill": "#101317", "frame": "flat", "shadow": 0, "radius": 0}}
page("kitchen", "Kitchen Display", "kitchen", [
    zone("tickets", 0, 0, 1920, 1080, kind="kitchenDisplay", props={"station": "kitchen"}, style=KDS),
], role="kitchen", permission="public", background={"fill": "#101317"})
page("bar-display", "Bar Display", "kitchen", [
    zone("tickets", 0, 0, 1920, 1080, kind="kitchenDisplay", props={"station": "bar"}, style=KDS),
], role="bar", permission="public", background={"fill": "#101317"})

page("end-of-day", "End of Day", "manager", [
    zone("eod", 460, 40, 1000, 880, kind="endOfDay"),
    zone("drawer", 460, 944, 480, 120, "Drawer…", actions=[jump(page="drawer")]),
    zone("back", 980, 944, 480, 120, "‹ Manager", actions=[jump(mode="back")]),
], permission="manager")

page("library", "Button Library", "library", [
    label("title", 40, 24, 1840, 80, "Button Library — copy these onto any page"),
    zone("lib-send", 40, 140, 300, 120, "Send", actions=[command("sendOrder")], style=fill(GREEN)),
    zone("lib-void", 360, 140, 300, 120, "Void", actions=[command("voidItem")], behavior="double",
         style=fill(RED)),
    zone("lib-pay", 680, 140, 300, 120, "Pay", actions=[jump(role="settle")], style=fill(BLUE)),
    zone("lib-close", 1320, 140, 300, 120, "Close ViewTouch", actions=[command("closeApp")], behavior="double",
         style=fill(RED)),
    zone("lib-toggle", 1000, 140, 300, 120, "Toggle", behavior="toggle"),
    zone("lib-diamond", 40, 300, 240, 240, "Diamond", shape="diamond"),
    zone("lib-hexagon", 320, 300, 300, 240, "Hexagon", shape="hexagon"),
    zone("lib-octagon", 660, 300, 240, 240, "Octagon", shape="octagon"),
    zone("lib-circle", 940, 300, 240, 240, "Circle", shape="circle"),
    zone("lib-sand", 40, 580, 300, 140, "Sand", style={"normal": {"texture": "sand", "fill": "#ae9877",
         "textColor": "#1b1b1b"}, "selected": {"texture": "litsand", "fill": "#d9c49a"}}),
    zone("lib-marble", 360, 580, 300, 140, "Marble", style={"normal": {"texture": "greenmarble",
         "fill": "#3a5a40"}}),
    zone("lib-wood", 680, 580, 300, 140, "Wood", shape="rounded", style={"normal": {"texture": "darkwood",
         "fill": "#5a3a22"}}),
    zone("lib-parchment", 1000, 580, 300, 140, "Parchment", style={"normal": {"texture": "parchment",
         "fill": "#d8c8a0", "textColor": "#2b2b2b", "textStyle": "embossed"}}),
    zone("lib-disabled", 1320, 580, 300, 140, "Disabled", enabled=False),
    zone("lib-status", 40, 780, 1840, 80, kind="statusBar"),
], background={"texture": "graymarble", "fill": "#555a60"})

# ---------------------------------------------------------------- phone pages
# Phone versions of the pages a server uses. Phones show them in place of the
# page they are a version of (variantOf). Pages without one are shown inside
# the phone order template, their buttons laid out in its content area
# (index, item and modifier pages, the note page).
PW, PH = 1080, 2280

def phone_page(base, name, kind, zones, **kw):
    page(f"{base}-phone", f"{name} (phone)", kind, zones, variantOf=base, formFactor="phone",
         canvas={"w": PW, "h": PH}, **kw)

def grid_buttons(buttons, y, cols, h, x=24, width=PW - 48, gap=16):
    """(id, text, actions, kw) in rows of `cols` from `y`; returns zones and the y below them."""
    out = []
    w = (width - gap * (cols - 1)) // cols
    for i, (zid, text, acts, kw) in enumerate(buttons):
        col, row = i % cols, i // cols
        out.append(zone(zid, x + col * (w + gap), y + row * (h + gap), w, h, text, actions=acts, **kw))
    rows = (len(buttons) + cols - 1) // cols
    return out, y + rows * (h + gap)

phone_page("login", "Login", "login", [
    label("title", 40, 60, 1000, 150, "ViewTouch", style={"normal": {"fontSize": 96}}),
    zone("clock", 140, 230, 800, 100, kind="clock"),
    zone("login-pad", 60, 360, 960, 1400, kind="loginPad"),
    zone("clock-in", 40, 1800, 320, 200, "Clock In", actions=[command("clockIn")]),
    zone("start", 380, 1800, 320, 200, "Log In", actions=[command("login")], style=fill(GREEN)),
    zone("clock-out", 720, 1800, 320, 200, "Clock Out", actions=[command("clockOut")]),
])

tables_buttons, _ = grid_buttons([
    ("quick", "Quick Order", [command("startQuick"), jump(mode="index")], {"style": fill(GREEN)}),
    ("checks", "Open Checks", [jump(role="checkList")], {}),
    ("takeout", "Takeout", [command("startTakeout"), jump(page="customer")], {}),
    ("delivery", "Delivery", [command("startDelivery"), jump(page="customer")], {}),
    ("manager", "Manager", [jump(role="manager")], {}),
    ("logout", "Log Out", [jump(role="logout")], {"style": fill(RED)}),
], 1640, 2, 190)
phone_page("tables", "Tables", "tables", [
    label("title", 24, 24, 1032, 100, "Tables"),
    zone("tables", 24, 140, 1032, 1480, kind="tableGrid"),
    *tables_buttons,
], background={"fill": "#2a2118"})

phone_page("guest-count", "Guest Count", "guestCount", [
    label("title", 24, 40, 1032, 120, "How many guests?"),
    zone("guests", 140, 180, 800, 220, kind="guestCount"),
    zone("pad", 140, 420, 800, 1300, kind="numPad"),
    zone("cancel", 40, 1780, 480, 220, "Cancel", actions=[command("releaseCheck"), jump(mode="back")]),
    zone("start", 560, 1780, 480, 220, "Start Order", actions=[command("startCheck"), jump(mode="index")],
         style=fill(GREEN)),
])

# The order screen: tabs, the check, the page's buttons, qualifiers, actions.
TAB = {"normal": {"fontSize": 34}}
qualifiers, y = grid_buttons([(f"flow-{q}", q.capitalize(), [{"type": "qualifier", "qualifier": q}], {})
                              for q in ("no", "extra", "lite", "side")], 1926, 4, 150, x=16, width=PW - 32)
actions, _ = grid_buttons([
    ("flow-void", "Void", [command("voidItem")], {"behavior": "double", "style": fill(RED)}),
    ("flow-send", "Send", [command("sendOrder")], {"style": fill(GREEN)}),
    ("flow-pay", "Pay", [jump(role="settle")], {"style": fill(BLUE)}),
], y, 3, 172, x=16, width=PW - 32)
top_tabs, _ = grid_buttons([
    ("tab-categories", "‹ Menu", [jump(mode="index")], {"style": TAB}),
    ("tab-note", "Note", [jump(page="note")], {"style": TAB}),
    ("tab-check", "Check…", [jump(page="check-options")], {"style": TAB}),
    ("flow-tables", "Tables", [command("releaseCheck"), jump(role="tables", mode="replace")], {"style": TAB}),
], 16, 4, 110, x=16, width=PW - 32)
phone_page("order-template", "Order Template", "template", [
    *top_tabs,
    zone("order-list", 16, 142, 1048, 720, kind="orderList"),
    *qualifiers,
    *actions,
], contentArea={"x": 16, "y": 878, "w": 1048, "h": 1032})

tender_zones = []
th = (700 - 16 * (len(tenders) - 1)) // len(tenders)
for i, (text, tid, color) in enumerate(tenders):
    tender_zones.append(zone(f"tender-{tid}", 552, 932 + i * (th + 16), 512, th, text,
                             actions=tender_action(tid), style=fill(color)))
phone_page("settle", "Settle", "settle", [
    zone("payment", 16, 16, 1048, 900, kind="paymentPanel"),
    zone("pad", 16, 932, 520, 700, kind="numPad", props={"mode": "amount"}),
    *tender_zones,
    zone("close", 16, 1648, 520, 180, "Close Check", actions=[command("closeCheck")], style=fill(GREEN)),
    zone("receipt", 552, 1648, 512, 180, "Print Receipt", actions=[command("printReceipt")]),
    zone("remove-payment", 16, 1844, 336, 180, "Undo Payment", actions=[command("removePayment")]),
    zone("split", 368, 1844, 336, 180, "Split Check", actions=[jump(page="split")]),
    zone("drawer", 720, 1844, 344, 180, "Drawer…", actions=[jump(page="drawer")]),
    zone("done", 16, 2040, 1048, 220, "‹ Back to Order", actions=[jump(mode="back")]),
])

logout_buttons, _ = grid_buttons([
    ("clock-out", "Clock Out", [command("clockOut")], {}),
    ("break", "Start / End Break", [command("toggleBreak")], {}),
    ("logout", "Log Out", [command("logout")], {"style": fill(RED)}),
    ("cancel", "Cancel", [jump(mode="back")], {}),
    ("tips", "Cash Out My Tips", [command("cashOutTips")], {"style": fill(GREEN)}),
    ("bank", "My Bank…", [jump(page="drawer")], {}),
], 720, 2, 200)
phone_page("logout", "Log Out", "logout", [
    label("title", 24, 40, 1032, 110, "End of shift"),
    zone("panel", 24, 170, 1032, 520, kind="logoutPanel"),
    *logout_buttons,
])

phone_page("customer", "Customer", "custom", [
    label("title", 24, 24, 1032, 100, "Who is the order for?"),
    zone("customer", 24, 140, 1032, 1860, kind="customerInfo"),
    zone("cancel", 24, 2030, 508, 220, "Cancel", actions=[command("releaseCheck"), jump(mode="back")]),
    zone("menu", 548, 2030, 508, 220, "Menu ›", actions=[jump(mode="index")], style=fill(GREEN)),
])

phone_page("check-list", "Open Checks", "custom", [
    label("title", 24, 24, 1032, 100, "Open checks"),
    zone("list", 24, 140, 1032, 1860, kind="checkList"),
    zone("back", 24, 2030, 1032, 220, "‹ Back", actions=[jump(mode="back")]),
])

print("seed written to", OUT)
