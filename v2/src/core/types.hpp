/*
 * ViewTouch V2 - Core Type Definitions
 * Faithful reimplementation of ViewTouch types
 */

#pragma once

#include <cstdint>
#include <string>

namespace vt {

/*************************************************************
 * Zone Types - matches original ViewTouch pos_zone.hh
 *************************************************************/
enum class ZoneType : uint8_t {
    Undefined        = 0,   // type not defined
    Standard         = 1,   // button with message & jump
    Item             = 2,   // order menu item
    Conditional      = 3,   // work if conditions are met
    Tender           = 4,   // tender payment type button
    Table            = 5,   // table status/selection
    Comment          = 6,   // only seen by superuser
    Qualifier        = 7,   // qualifier: no, extra, lite
    Toggle           = 8,   // button with toggling text/message
    Simple           = 9,   // button with only jump
    Switch           = 10,  // settings selection button
    
    Login            = 20,  // takes user id for login
    Command          = 21,  // system command/status
    GuestCount       = 23,  // enter the number of guests
    Logout           = 24,  // user logout stuff
    
    OrderEntry       = 30,  // show current menu order
    CheckList        = 31,  // show open checks
    PaymentEntry     = 32,  // show/allow payments for check
    UserEdit         = 33,  // show/edit users
    Settings         = 34,  // edit general system variables
    TaxSettings      = 35,  // tax and royalty settings
    Developer        = 36,  // developer application settings
    TenderSet        = 37,  // tender selection & settings
    TaxSet           = 38,  // tax specifications
    MoneySet         = 39,  // currency specifications
    CCSettings       = 40,  // credit/charge card settings
    CCMsgSettings    = 41,  // credit/charge card messages
    
    Report           = 50,  // super report zone
    OrderPage        = 51,  // page change on order entry window
    Schedule         = 52,  // employee scheduling
    PrintTarget      = 53,  // family printer destinations
    SplitCheck       = 54,  // check splitting zone
    DrawerManage     = 55,  // drawer pulling/balancing
    Hardware         = 56,  // terminal & printer setup
    TimeSettings     = 57,  // store hours/shifts
    TableAssign      = 58,  // transfer tables/checks
    CheckDisplay     = 59,  // display multiple checks
    
    KillSystem       = 61,  // system termination
    Payout           = 62,  // cash payout system
    DrawerAssign     = 63,  // drawer assignment
    OrderFlow        = 64,  // order start/index/continue
    Search           = 66,  // search for word though records
    SplitKitchen     = 67,  // split kitchen terminal assignment
    EndDay           = 68,  // end of day management
    Read             = 69,  // reading & displaying text files
    JobSecurity      = 70,  // job security settings
    Inventory        = 71,  // raw product inventory
    Recipe           = 72,  // recipes using raw products
    Vendor           = 73,  // raw product suppliers
    Labor            = 74,  // labor management
    ItemList         = 75,  // list all sales items
    Invoice          = 76,  // invoice entry/listing
    Phrase           = 77,  // phrase translation/replacement
    ItemTarget       = 78,  // item printer target
    ReceiptSet       = 79,  // printed receipt settings
    Merchant         = 80,  // merchant info for credit authorize
    License          = 81,  // viewtouch pos license setup
    Account          = 82,  // chart of accounts list/edit
    OrderAdd         = 83,  // increase order button
    OrderDelete      = 84,  // delete/rebuild button
    OrderDisplay     = 85,  // kitchen work order display
    Chart            = 86,  // spreadsheet like data display
    VideoTarget      = 87,  // kitchen video food types
    Expense          = 88,  // paying expense from revenue
    StatusButton     = 89,  // for error messages
    CDU              = 90,  // CDU string entry
    Receipts         = 91,  // receipt headers/footers
    CustomerInfo     = 92,  // editing customer info
    CheckEdit        = 93,  // editing check info
    CreditCardList   = 94,  // managing exceptions/refunds/voids
    ExpireMsg        = 95,  // expiration message
    RevenueGroups    = 96,  // revenue group settings
    ImageButton      = 97,  // button with user-selectable image
    
    ItemNormal       = 98,  // menu item button
    ItemModifier     = 99,  // modifier button
    ItemMethod       = 100, // non-tracking modifier button
    ItemSubstitute   = 101, // menu item + substitute button
    ItemPound        = 102, // priced by weight button
    ItemAdmission    = 103, // event admission button
    OrderComment     = 104, // add comment button
    
    ClearSystem      = 107, // clear system with countdown
    IndexTab         = 108, // index tab button
    LanguageButton   = 109, // language selection button
    CalculationSettings = 110 // calculation settings
};

/*************************************************************
 * Page Types - matches original ViewTouch
 *************************************************************/
enum class PageType : uint8_t {
    System       = 0,   // Hidden, normally unmodifiable System Page
    Table        = 1,   // Table layout page
    Index        = 2,   // Top level menu page
    Item         = 3,   // Menu Item ordering page
    Scripted3    = 4,   // Yet another modifier page
    Scripted     = 5,   // Page in a modifier script
    Scripted2    = 6,   // Alternate modifier page
    Template     = 7,   // Viewable System page
    Library      = 8,   // User page for storing zones
    Item2        = 9,   // Alternate item ordering page
    Table2       = 10,  // Table page with check detail
    Checks       = 12,  // Check list system page
    KitchenVid   = 13,  // List of checks for the cooks
    KitchenVid2  = 14,  // Secondary list of checks for cooks
    Bar1         = 15,  // Bar mode page
    Bar2         = 16,  // Second bar mode page
    ModifierKB   = 17,  // Modifier page with keyboard
    IndexTabs    = 18   // Index with tabs for quick navigation
};

/*************************************************************
 * Zone Behavior - how zone responds to touch
 *************************************************************/
enum class ZoneBehavior : uint8_t {
    None    = 0,  // Zone doesn't change when selected
    Toggle  = 1,  // Zone toggles with each selection
    Blink   = 2,  // Zone depresses then resets itself
    Select  = 3,  // Once selected stay selected
    Double  = 4,  // Touch twice within time period
    Miss    = 5   // Touch misses zone & hits zones underneath
};

/*************************************************************
 * Zone Frame Appearance
 *************************************************************/
enum class ZoneFrame : uint8_t {
    Unchanged    = 0,
    Default      = 1,
    Hidden       = 2,   // frame, texture & text all hidden
    None         = 3,   // no frame
    Raised       = 10,  // raised single frame
    Raised1      = 11,  // medium raised
    Raised2      = 12,  // lit raised
    Raised3      = 13,  // dark raised
    Inset        = 20,  // inset single frame
    Inset1       = 21,
    Inset2       = 22,
    Inset3       = 23,
    Double       = 30,  // double raised frame
    Double1      = 31,
    Double2      = 32,
    Double3      = 33,
    Border       = 40,  // raised & inset frames filled with texture
    ClearBorder  = 41,
    SandBorder   = 42,
    LitSandBorder    = 43,
    InsetBorder      = 44,
    ParchmentBorder  = 45,
    DoubleBorder     = 50,
    LitDoubleBorder  = 51
};

/*************************************************************
 * Zone Shapes
 *************************************************************/
enum class ZoneShape : uint8_t {
    Rectangle = 1,
    Diamond   = 2,
    Circle    = 3,
    Hexagon   = 4,
    Octagon   = 5
};

/*************************************************************
 * Jump Types - navigation behavior
 *************************************************************/
enum class JumpType : uint8_t {
    None     = 0,  // Don't jump
    Normal   = 1,  // Jump to page, push current page onto stack
    Stealth  = 2,  // Jump to page (don't push current page)
    Return   = 3,  // Pop page off stack, jump to it
    Home     = 4,  // Jump to employee home page
    Script   = 5,  // Jump to next page in script
    Index    = 6,  // Jump to current page's index
    Password = 7   // Like Normal but password must be entered
};

/*************************************************************
 * Terminal Types
 *************************************************************/
enum class TerminalType : uint8_t {
    OrderOnly    = 0,  // can order but no settling
    Normal       = 1,  // normal operation
    Bar          = 2,  // alternate menu index, pay & settle at once
    Bar2         = 3,  // bar with all local work orders
    FastFood     = 4,  // no table view, pay & settle at once
    SelfOrder    = 5,  // customer self-service, no login required
    KitchenVideo = 6,  // display of checks for cooks
    KitchenVideo2= 7   // secondary check display
};

/*************************************************************
 * Text Alignment
 *************************************************************/
enum class TextAlign : uint8_t {
    Left   = 0,
    Center = 1,
    Right  = 2
};

/*************************************************************
 * Update Messages - what changed
 *************************************************************/
enum UpdateFlag : uint32_t {
    UpdateNone        = 0,
    UpdateMinute      = (1 << 0),
    UpdateHour        = (1 << 1),
    UpdateTimeout     = (1 << 2),
    UpdateBlink       = (1 << 3),
    UpdateMealPeriod  = (1 << 4),
    UpdateUsers       = (1 << 5),
    UpdateChecks      = (1 << 6),
    UpdateOrders      = (1 << 7),
    UpdateOrderSelect = (1 << 8),
    UpdatePayments    = (1 << 9),
    UpdateTable       = (1 << 10),
    UpdateAllTables   = (1 << 11),
    UpdateMenu        = (1 << 12),
    UpdateDrawer      = (1 << 13),
    UpdateSale        = (1 << 14),
    UpdateQualifier   = (1 << 15),
    UpdateGuests      = (1 << 16),
    UpdateDrawers     = (1 << 17),
    UpdateArchive     = (1 << 18),
    UpdateSettings    = (1 << 19),
    UpdateJobFilter   = (1 << 20),
    UpdateTerminals   = (1 << 21),
    UpdatePrinters    = (1 << 22),
    UpdateAuthorize   = (1 << 23),
    UpdateServer      = (1 << 24),
    UpdateReport      = (1 << 25)
};

/*************************************************************
 * Special Page IDs
 *************************************************************/
constexpr int PAGE_ID_MANAGER     = -10;
constexpr int PAGE_ID_ITEM_TARGET = -9;
constexpr int PAGE_ID_BAR_SETTLE  = -8;
constexpr int PAGE_ID_LOGOUT      = -7;
constexpr int PAGE_ID_GUESTCOUNT2 = -6;
constexpr int PAGE_ID_GUESTCOUNT  = -5;
constexpr int PAGE_ID_TABLE2      = -4;
constexpr int PAGE_ID_TABLE       = -3;
constexpr int PAGE_ID_LOGIN2      = -2;
constexpr int PAGE_ID_LOGIN       = -1;
constexpr int PAGE_ID_SETTLEMENT  = -20;
constexpr int PAGE_ID_TABSETTLE   = -85;

/*************************************************************
 * Stack Sizes
 *************************************************************/
constexpr int PAGE_STACK_SIZE   = 32;
constexpr int SCRIPT_STACK_SIZE = 32;

/*************************************************************
 * Render Update
 *************************************************************/
enum class RenderUpdate : uint8_t {
    Redraw  = 0,  // just redraw zone
    Refresh = 1,  // recalculate current data & redraw
    New     = 2   // initialize data view & redraw
};

/*************************************************************
 * Region - basic rectangular area
 *************************************************************/
struct Region {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    
    bool intersects(const Region& other) const {
        return !(other.x >= x + w || other.x + other.w <= x ||
                 other.y >= y + h || other.y + other.h <= y);
    }
};

} // namespace vt
