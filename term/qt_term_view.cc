/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998, 2025, 2026
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 * qt_term_view.cc
 * Qt6 terminal rendering backend — full implementation.
 *
 * Replaces term_view.cc / layer.cc. The server (vt_main) emits the same
 * CharQueue binary protocol over the Unix socket; we just render with
 * QPainter instead of Xlib/Xft.
 */

#include "qt_term_view.hh"
#include "src/utils/vt_logger.hh"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QFontMetrics>
#include <QTimer>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QGroupBox>
#include <QScrollArea>
#include <QListWidget>
#include <functional>

#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <array>

// X11 keysym values expected by vt_main's KeyboardInput()
namespace XK {
    constexpr int BackSpace    = 0xff08;
    constexpr int Tab          = 0xff09;
    constexpr int Return_      = 0xff0d;
    constexpr int Escape       = 0xff1b;
    constexpr int Delete       = 0xffff;
    constexpr int Home         = 0xff50;
    constexpr int Left         = 0xff51;
    constexpr int Up           = 0xff52;
    constexpr int Right        = 0xff53;
    constexpr int Down         = 0xff54;
    constexpr int Page_Up      = 0xff55;
    constexpr int Page_Down    = 0xff56;
    constexpr int End          = 0xff57;
    constexpr int Insert       = 0xff63;
    constexpr int KP_Enter     = 0xff8d;
    constexpr int ISO_Left_Tab = 0xfe20;
    constexpr int F1           = 0xffbe;
    constexpr int F2           = 0xffbf;
    constexpr int F3           = 0xffc0;
    constexpr int F4           = 0xffc1;
    constexpr int F5           = 0xffc2;
    constexpr int F6           = 0xffc3;
    constexpr int F7           = 0xffc4;
    constexpr int F8           = 0xffc5;
    constexpr int F9           = 0xffc6;
    constexpr int F10          = 0xffc7;
    constexpr int F11          = 0xffc8;
    constexpr int F12          = 0xffc9;
}

// X11 modifier masks (matching server expectations)
constexpr int kShiftMask   = 1;
constexpr int kControlMask = 4;
constexpr int kMod1Mask    = 8;   // Alt

static int qtKeyToXKeysym(int qt_key) noexcept
{
    switch (qt_key) {
    case Qt::Key_Backspace: return XK::BackSpace;
    case Qt::Key_Tab:       return XK::Tab;
    case Qt::Key_Return:    return XK::Return_;
    case Qt::Key_Enter:     return XK::KP_Enter;
    case Qt::Key_Escape:    return XK::Escape;
    case Qt::Key_Delete:    return XK::Delete;
    case Qt::Key_Home:      return XK::Home;
    case Qt::Key_Left:      return XK::Left;
    case Qt::Key_Up:        return XK::Up;
    case Qt::Key_Right:     return XK::Right;
    case Qt::Key_Down:      return XK::Down;
    case Qt::Key_PageUp:    return XK::Page_Up;
    case Qt::Key_PageDown:  return XK::Page_Down;
    case Qt::Key_End:       return XK::End;
    case Qt::Key_Insert:    return XK::Insert;
    case Qt::Key_F1:        return XK::F1;
    case Qt::Key_F2:        return XK::F2;
    case Qt::Key_F3:        return XK::F3;
    case Qt::Key_F4:        return XK::F4;
    case Qt::Key_F5:        return XK::F5;
    case Qt::Key_F6:        return XK::F6;
    case Qt::Key_F7:        return XK::F7;
    case Qt::Key_F8:        return XK::F8;
    case Qt::Key_F9:        return XK::F9;
    case Qt::Key_F10:       return XK::F10;
    case Qt::Key_F11:       return XK::F11;
    case Qt::Key_F12:       return XK::F12;
    default:                return qt_key;   // ASCII keys pass through
    }
}

static int qtModsToX11(Qt::KeyboardModifiers mods) noexcept
{
    int state = 0;
    if (mods & Qt::ShiftModifier)   state |= kShiftMask;
    if (mods & Qt::ControlModifier) state |= kControlMask;
    if (mods & Qt::AltModifier)     state |= kMod1Mask;
    return state;
}

// ─────────────────────────────────────────────────────────────────────────────
// Globals used by term_credit.cc (unchanged interface)
// ─────────────────────────────────────────────────────────────────────────────
int SocketNo      = 0;
int TermNo        = 0;
int IsTermLocal   = 0;
int WinWidth      = 1024;
int WinHeight     = 768;

static CharQueue s_buffer_out{QUEUE_SIZE};
static CharQueue s_buffer_in {QUEUE_SIZE};

int  SendNow() noexcept       { return s_buffer_out.Write(SocketNo); }
int  WInt8(int v)    noexcept { return s_buffer_out.Put8(v); }
int  RInt8()         noexcept { return s_buffer_in.Get8(); }
int  WInt16(int v)   noexcept { return s_buffer_out.Put16(v); }
int  RInt16()        noexcept { return s_buffer_in.Get16(); }
int  WInt32(int v)   noexcept { return s_buffer_out.Put32(v); }
int  RInt32()        noexcept { return s_buffer_in.Get32(); }
long WLong(long v)   noexcept { return s_buffer_out.PutLong(v); }
long RLong()         noexcept { return s_buffer_in.GetLong(); }
int       WFlt(Flt v)  noexcept { return s_buffer_out.Put32(static_cast<int>(v * 100.0)); }
Flt       RFlt()       noexcept { return static_cast<Flt>(s_buffer_in.Get32()) / 100.0; }
long long WLLong(long long v) noexcept { return s_buffer_out.PutLLong(v); }
long long RLLong() noexcept           { return s_buffer_in.GetLLong(); }

int WStr(const char *s, int len)
{
    return s_buffer_out.PutString(s ? std::string(s) : std::string(), len);
}

genericChar *RStr(genericChar *s)
{
    static std::array<genericChar, 1024> buf{};
    if (!s) s = buf.data();
    s[0] = '\0';
    s_buffer_in.GetString(s, 1023);
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pen / colour data  (modernised: better contrast, softer shadows)
// ─────────────────────────────────────────────────────────────────────────────
struct PenEntry { int id; int t[3], sh[3], hi[3]; };
static constexpr std::array<PenEntry, TEXT_COLORS> kPenData = {{
    {COLOR_BLACK,      {  20, 20, 20},{200,195,190},{160,155,150}},
    {COLOR_WHITE,      { 255,255,255},{ 80, 80, 80},{210,210,210}},
    {COLOR_RED,        { 210, 35, 35},{ 80,  0,  0},{255,180,180}},
    {COLOR_GREEN,      {  20,140, 40},{  0, 50,  0},{150,240,150}},
    {COLOR_BLUE,       {  30, 90,210},{  0,  0, 80},{180,200,255}},
    {COLOR_YELLOW,     { 200,170,  0},{ 80, 60,  0},{240,230,100}},
    {COLOR_BROWN,      { 130, 80, 40},{ 60, 20,  0},{220,200,180}},
    {COLOR_ORANGE,     { 230,100,  0},{ 80, 30,  0},{255,210,160}},
    {COLOR_PURPLE,     { 120, 30,200},{  0,  0, 80},{220,180,255}},
    {COLOR_TEAL,       {   0,140,160},{  0, 40, 60},{150,220,240}},
    {COLOR_GRAY,       { 100,100,100},{ 40, 40, 40},{210,210,210}},
    {COLOR_MAGENTA,    { 190, 50,140},{ 60,  0, 40},{240,180,220}},
    {COLOR_REDORANGE,  { 230, 70, 10},{ 80, 20,  0},{255,200,170}},
    {COLOR_SEAGREEN,   {  10,140,100},{  0, 50, 30},{140,230,200}},
    {COLOR_LT_BLUE,    {  90,130,190},{ 20, 40, 80},{180,210,245}},
    {COLOR_DK_RED,     { 160, 20, 20},{ 50,  0,  0},{240,170,170}},
    {COLOR_DK_GREEN,   {  10,100, 30},{  0, 35,  0},{130,220,130}},
    {COLOR_DK_BLUE,    {  10, 50,160},{  0,  0, 60},{180,195,245}},
    {COLOR_DK_TEAL,    {   0, 95,120},{  0, 25, 45},{140,205,235}},
    {COLOR_DK_MAGENTA, { 150, 30,100},{ 45,  0, 25},{225,170,210}},
    {COLOR_DK_SEAGREEN,{   0,105, 75},{  0, 35, 20},{130,225,195}},
}};

// ─────────────────────────────────────────────────────────────────────────────
// Modern flat texture colours — one per IMAGE_* enum value.
// Used instead of tiling XPM bitmaps.
// ─────────────────────────────────────────────────────────────────────────────
// Each entry is {base_rgb, highlight_rgb} — fillTexture draws a subtle
// vertical gradient from highlight (top) to base (bottom).
struct TextureGrad { uint32_t base, top; };
static constexpr std::array<TextureGrad, IMAGE_COUNT> kModernTextures = {{
    {0xEFEBE4, 0xF8F5F0},  //  0 IMAGE_SAND          warm off-white
    {0xFAF8F4, 0xFFFFFF},  //  1 IMAGE_LIT_SAND      bright cream
    {0xDDDAD3, 0xE8E5DE},  //  2 IMAGE_DARK_SAND      medium warm grey
    {0xC8AC7C, 0xDEC898},  //  3 IMAGE_LITE_WOOD      pale oak
    {0x987040, 0xB08858},  //  4 IMAGE_WOOD           mid oak
    {0x684828, 0x7A5830},  //  5 IMAGE_DARK_WOOD      dark walnut
    {0xE0E0E0, 0xEEEEEE},  //  6 IMAGE_GRAY_PARCHMENT cool light grey
    {0xCCCCD0, 0xDADADE},  //  7 IMAGE_GRAY_MARBLE    blue-grey
    {0xC0D0BC, 0xD0E0CC},  //  8 IMAGE_GREEN_MARBLE   sage green
    {0xECE4D0, 0xF5EFE0},  //  9 IMAGE_PARCHMENT      warm parchment
    {0xF2F0EE, 0xFAF9F8},  // 10 IMAGE_PEARL          pearl white
    {0xE4DFD4, 0xEFEBE2},  // 11 IMAGE_CANVAS         canvas
    {0xD8C898, 0xE8D8A8},  // 12 IMAGE_TAN_PARCHMENT  tan
    {0xC0C0C8, 0xCECED6},  // 13 IMAGE_SMOKE          cool smoke grey
    {0x504038, 0x605048},  // 14 IMAGE_LEATHER         dark leather
    {0xCCDCEE, 0xDAEAFC},  // 15 IMAGE_BLUE_PARCHMENT  soft sky blue
    {0xCED4E0, 0xDCE2EE},  // 16 IMAGE_GRADIENT        cool grey-blue
    {0xC09870, 0xD0A880},  // 17 IMAGE_GRADIENTBROWN   warm tan
    {0x1A1A1A, 0x1A1A1A},  // 18 IMAGE_BLACK           (handled separately)
    {0xD4D4D4, 0xE2E2E2},  // 19 IMAGE_GREYSAND        neutral grey
    {0xF4F4F4, 0xFCFCFC},  // 20 IMAGE_WHITEMESH       near white
    {0x2C2C2C, 0x383838},  // 21 IMAGE_CARBON_FIBER    near black
    {0xF2F2F2, 0xFAFAFA},  // 22 IMAGE_WHITE_TEXTURE   white
    {0xD05820, 0xE06830},  // 23 IMAGE_DARK_ORANGE      orange
    {0xE8C800, 0xF0D810},  // 24 IMAGE_YELLOW           yellow
    {0x44A860, 0x54B870},  // 25 IMAGE_GREEN            medium green
    {0xE88428, 0xF09438},  // 26 IMAGE_ORANGE           warm orange
    {0x2E7CC8, 0x3E8CD8},  // 27 IMAGE_BLUE             medium blue
    {0x246828, 0x347838},  // 28 IMAGE_POOL_TABLE       felt green
    {0x989898, 0xA8A8A8},  // 29 IMAGE_TEST             grey
    {0x402818, 0x503828},  // 30 IMAGE_DIAMOND_LEATHER  very dark brown
    {0xC89C40, 0xD8AC50},  // 31 IMAGE_BREAD            golden
    {0x800C0C, 0x940E0E},  // 32 IMAGE_LAVA             deep red
    {0x585860, 0x686870},  // 33 IMAGE_DARK_MARBLE      slate grey
}};

// ─────────────────────────────────────────────────────────────────────────────
// Font data — Noto Sans replaces DejaVu Serif for a clean modern look.
// Courier slots keep Liberation Mono for numeric readability (prices, codes).
// ─────────────────────────────────────────────────────────────────────────────
struct FontEntry { int id; const char *family; int pts; bool bold; };
static constexpr std::array<FontEntry, 17> kFontData = {{
    {FONT_TIMES_20,   "Noto Sans", 12, false},
    {FONT_TIMES_24,   "Noto Sans", 14, false},
    {FONT_TIMES_34,   "Noto Sans", 18, false},
    {FONT_TIMES_48,   "Noto Sans", 28, false},
    {FONT_TIMES_20B,  "Noto Sans", 12, true },
    {FONT_TIMES_24B,  "Noto Sans", 14, true },
    {FONT_TIMES_34B,  "Noto Sans", 18, true },
    {FONT_TIMES_48B,  "Noto Sans", 28, true },
    {FONT_TIMES_14,   "Noto Sans", 10, false},
    {FONT_TIMES_14B,  "Noto Sans", 10, true },
    {FONT_TIMES_18,   "Noto Sans", 11, false},
    {FONT_TIMES_18B,  "Noto Sans", 11, true },
    {FONT_COURIER_18, "Noto Sans Mono", 11, false},
    {FONT_COURIER_18B,"Noto Sans Mono", 11, true },
    {FONT_COURIER_20, "Noto Sans Mono", 12, false},
    {FONT_COURIER_20B,"Noto Sans Mono", 12, true },
    {FONT_DEFAULT,    "Noto Sans", 14, false},
}};

// Page size table
static constexpr std::array<std::pair<int,int>, 18> kPageSizes = {{
    {0,   0},    {640,480},  {768,1024}, {800,480},
    {800,600},   {1024,600}, {1024,768}, {1280,800},
    {1280,1024}, {1366,768}, {1440,900}, {1600,900},
    {1600,1200}, {1680,1050},{1920,1080},{1920,1200},
    {2560,1440}, {2560,1600},
}};

// Window IDs matching terminal.cc
static constexpr int WIN_MAIN    = 1;
static constexpr int WIN_TOOLBAR = 2;

// Mouse codes matching terminal.hh
static constexpr int MOUSE_LEFT    = 1;
static constexpr int MOUSE_MIDDLE  = 2;
static constexpr int MOUSE_RIGHT   = 4;
static constexpr int MOUSE_PRESS   = 8;
static constexpr int MOUSE_DRAG    = 16;
static constexpr int MOUSE_RELEASE = 32;
static constexpr int MOUSE_SHIFT   = 64;

// ─────────────────────────────────────────────────────────────────────────────
// TermWidget – construction / destruction
// ─────────────────────────────────────────────────────────────────────────────
TermWidget::TermWidget(int socket_fd, int is_local, QWidget *parent)
    : QWidget(parent)
    , socket_fd_(socket_fd)
{
    SocketNo    = socket_fd;
    IsTermLocal = is_local;

    setWindowTitle("ViewTouch POS");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::StrongFocus);

    QScreen *screen = QApplication::primaryScreen();
    QRect sg = screen ? screen->geometry() : QRect(0, 0, 1024, 768);
    WinWidth  = sg.width();
    WinHeight = sg.height();
    resize(WinWidth, WinHeight);

    page_w_ = WinWidth;
    page_h_ = WinHeight;

    back_buffer_ = QPixmap(WinWidth, WinHeight);
    back_buffer_.fill(Qt::black);

    initColors();
    initFonts();
    initTextures();
    initEdgeColors();

    screensaver_timer_ = new QTimer(this);
    connect(screensaver_timer_, &QTimer::timeout, this, &TermWidget::onScreensaverTick);
    screensaver_timer_->start(1000);

    if (socket_fd_ > 0) {
        socket_notifier_ = new QSocketNotifier(socket_fd_, QSocketNotifier::Read, this);
        connect(socket_notifier_, &QSocketNotifier::activated, this, &TermWidget::onSocketReady);
    }
}

TermWidget::~TermWidget() = default;

// ─────────────────────────────────────────────────────────────────────────────
// Initialisation
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::initColors()
{
    for (const auto &e : kPenData) {
        text_colors_  [e.id] = QColor(e.t[0],  e.t[1],  e.t[2]);
        shadow_colors_[e.id] = QColor(e.sh[0], e.sh[1], e.sh[2]);
        hilight_colors_[e.id]= QColor(e.hi[0], e.hi[1], e.hi[2]);
    }
}

void TermWidget::initFonts()
{
    // Prefer Noto Sans; fall back through Liberation Sans → DejaVu Sans
    QFont::insertSubstitutions("Noto Sans",
        QStringList{"Liberation Sans", "DejaVu Sans", "sans-serif"});
    QFont::insertSubstitutions("Noto Sans Mono",
        QStringList{"Liberation Mono", "DejaVu Sans Mono", "monospace"});

    QFont fallback("Noto Sans", 14);
    fallback.setHintingPreference(QFont::PreferFullHinting);
    fallback.setStyleStrategy(
        static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
    fonts_.fill(fallback);

    for (const auto &e : kFontData) {
        QFont f(e.family, e.pts);
        f.setBold(e.bold);
        f.setHintingPreference(QFont::PreferFullHinting);
        f.setStyleStrategy(
            static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferQuality));
        if (e.id < static_cast<int>(fonts_.size()))
            fonts_[e.id] = f;
    }
}

void TermWidget::initTextures()
{
    for (int i = 0; i < IMAGE_COUNT; ++i) {
        if (ImageData[i]) {
            textures_[i] = QPixmap(ImageData[i]);
            if (textures_[i].isNull())
                vt::Logger::warn("qt_term: failed to load texture {}", i);
        }
    }
}

void TermWidget::initEdgeColors()
{
    // Modern neutral palette — subtle 1-stop bevel, no warm-beige 3D look
    c_te_ = QColor(230,230,230); c_be_ = QColor(120,120,120);   // normal
    c_le_ = QColor(218,218,218); c_re_ = QColor(140,140,140);
    c_lte_= QColor(255,255,255); c_lbe_= QColor(150,150,150);   // lit
    c_lle_= QColor(245,245,245); c_lre_= QColor(160,160,160);
    c_dte_= QColor(170,170,170); c_dbe_= QColor( 80, 80, 80);   // dark
    c_dle_= QColor(155,155,155); c_dre_= QColor( 95, 95, 95);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource accessors
// ─────────────────────────────────────────────────────────────────────────────
QColor TermWidget::textColor(int id) const noexcept
{
    if (id >= 0 && id < TEXT_COLORS) return text_colors_[id];
    return Qt::white;
}

QColor TermWidget::shadowColor(int id) const noexcept
{
    if (id >= 0 && id < TEXT_COLORS) return shadow_colors_[id];
    return Qt::black;
}

QColor TermWidget::hilightColor(int id) const noexcept
{
    if (id >= 0 && id < TEXT_COLORS) return hilight_colors_[id];
    return Qt::white;
}

QFont TermWidget::getFont(int id) const noexcept
{
    if (id >= 0 && id < static_cast<int>(fonts_.size())) return fonts_[id];
    return fonts_[FONT_DEFAULT];
}

QBrush TermWidget::textureBrush(int id) const noexcept
{
    if (id >= 0 && id < IMAGE_COUNT && !textures_[id].isNull())
        return QBrush(textures_[id]);
    return QBrush(QColor(210, 185, 150));
}

QPixmap &TermWidget::targetPixmap(int layer_id) noexcept
{
    if (layer_id != 0) {
        auto it = windows_.find(layer_id);
        if (it != windows_.end()) return it->pix;
    }
    return back_buffer_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Qt event overrides
// ─────────────────────────────────────────────────────────────────────────────
// Convert a page-space point to screen-space, accounting for viewport scaling.
static QPoint pageToScreen(int px, int py, float sx, float sy, int vx, int vy,
                            int page_x, int page_y)
{
    return { vx + (int)((px - page_x) * sx),
             vy + (int)((py - page_y) * sy) };
}

void TermWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (scale_x_ != 1.0f || view_x_ != 0 || view_y_ != 0) {
        // Scale page content to fill the physical screen
        p.fillRect(rect(), Qt::black);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QRect src(page_x_, page_y_, page_w_, page_h_);
        QRect dst(view_x_, view_y_, view_w_, view_h_);
        p.drawPixmap(dst, back_buffer_, src);
        for (auto &w : windows_) {
            if (!w.visible) continue;
            int wx = view_x_ + (int)((w.rect.x() - page_x_) * scale_x_);
            int wy = view_y_ + (int)((w.rect.y() - page_y_) * scale_y_);
            int ww = (int)(w.rect.width()  * scale_x_);
            int wh = (int)(w.rect.height() * scale_y_);
            p.drawPixmap(QRect(wx, wy, ww, wh), w.pix, w.pix.rect());
        }
    } else {
        p.drawPixmap(0, 0, back_buffer_);
        for (auto &w : windows_) {
            if (w.visible) p.drawPixmap(w.rect.topLeft(), w.pix);
        }
    }

    // ── Rubber-band selection overlay ────────────────────────────────────────
    if (select_anchor_set_) {
        auto a = pageToScreen(select_ax_, select_ay_, scale_x_, scale_y_,
                              view_x_, view_y_, page_x_, page_y_);
        auto b = pageToScreen(select_ex_, select_ey_, scale_x_, scale_y_,
                              view_x_, view_y_, page_x_, page_y_);
        QRect band = QRect(a, b).normalized();

        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.fillRect(band, QColor(80, 140, 255, 50));
        // Double-line dashed border: white line over dark line for visibility on any bg
        QPen darkPen(QColor(0, 0, 0, 180), 1, Qt::DashLine);
        QPen litPen (QColor(255, 255, 255, 220), 1, Qt::DashLine);
        darkPen.setDashOffset(0); litPen.setDashOffset(4);
        p.setPen(darkPen); p.drawRect(band);
        p.setPen(litPen);  p.drawRect(band);
    }

    // ── Edit-cursor resize handle overlay ────────────────────────────────────
    if (edit_cursor_visible_) {
        auto tl = pageToScreen(edit_cursor_rect_.left(),  edit_cursor_rect_.top(),
                               scale_x_, scale_y_, view_x_, view_y_, page_x_, page_y_);
        auto br = pageToScreen(edit_cursor_rect_.right(), edit_cursor_rect_.bottom(),
                               scale_x_, scale_y_, view_x_, view_y_, page_x_, page_y_);
        QRect cr(tl, br);
        int mx = cr.left() + cr.width()  / 2;
        int my = cr.top()  + cr.height() / 2;

        // Draw zone outline
        p.setPen(QPen(QColor(255, 200, 0, 220), 1, Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(cr);

        // Draw 8 resize handles: corners + edge midpoints
        const int HS = 8;  // handle size
        QPoint handles[] = {
            cr.topLeft(),     {mx, cr.top()},    cr.topRight(),
            {cr.left(), my},                     {cr.right(), my},
            cr.bottomLeft(),  {mx, cr.bottom()}, cr.bottomRight(),
        };
        for (QPoint h : handles) {
            QRect hRect(h.x() - HS/2, h.y() - HS/2, HS, HS);
            p.fillRect(hRect, QColor(255, 200, 0, 220));
            p.setPen(QPen(Qt::black, 1));
            p.drawRect(hRect);
        }
    }
}

void TermWidget::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    int nw = ev->size().width();
    int nh = ev->size().height();
    if (nw == back_buffer_.width() && nh == back_buffer_.height()) return;

    QPixmap new_buf(nw, nh);
    new_buf.fill(Qt::black);
    { QPainter p(&new_buf); p.drawPixmap(0, 0, back_buffer_); }
    back_buffer_ = std::move(new_buf);
    WinWidth  = nw;
    WinHeight = nh;
}

void TermWidget::keyPressEvent(QKeyEvent *ev)
{
    resetScreensaver();
    QString txt = ev->text();
    genericChar ch = txt.isEmpty() ? 0 : static_cast<genericChar>(txt.at(0).toLatin1());
    int xkeysym = qtKeyToXKeysym(ev->key());
    int state   = qtModsToX11(ev->modifiers());

    if (ev->key() == Qt::Key_Backtab ||
        (ev->key() == Qt::Key_Tab && (ev->modifiers() & Qt::ShiftModifier)))
        xkeysym = XK::ISO_Left_Tab;

    sendKey(ch, xkeysym, state);
}

// Convert a screen position to page-relative coordinates accounting for scaling.
static QPoint screenToPage(QPointF screen, float scale_x, float scale_y,
                            int view_x, int view_y)
{
    return { (int)((screen.x() - view_x) / scale_x),
             (int)((screen.y() - view_y) / scale_y) };
}

void TermWidget::mousePressEvent(QMouseEvent *ev)
{
    resetScreensaver();
    QPoint pg = screenToPage(ev->position(), scale_x_, scale_y_, view_x_, view_y_);
    int px = pg.x(), py = pg.y();

    // Check if the click lands inside a visible overlay window
    press_toolbar_ = false;
    for (auto it = windows_.begin(); it != windows_.end(); ++it) {
        if (!it->visible) continue;
        // Window rect in back_buffer_ space; subtract page offset for page coords
        QRect wr(it->rect.x() - page_x_, it->rect.y() - page_y_,
                 it->rect.width(), it->rect.height());
        if (!wr.contains(px, py)) continue;
        press_toolbar_ = true;
        int bx = px - wr.x(), by = py - wr.y();
        for (const auto &btn : it->buttons) {
            if (btn.rect.contains(bx, by)) {
                sendButtonPress(it->id, btn.id);
                moves_count_ = 0;
                return;
            }
        }
        moves_count_ = 0;
        return;  // click in window border/title — eat it
    }

    // Send SrvMouse (not SrvTouch) so that server calls MouseInput() which
    // handles edit mode correctly (zone select / drag / right-click edit).
    int btn = MOUSE_PRESS | MOUSE_LEFT;
    if (ev->button() == Qt::RightButton)  btn = MOUSE_PRESS | MOUSE_RIGHT;
    if (ev->button() == Qt::MiddleButton) btn = MOUSE_PRESS | MOUSE_MIDDLE;
    if (ev->modifiers() & Qt::ShiftModifier) btn |= MOUSE_SHIFT;
    moves_count_ = 0;
    sendMouse(btn, px, py);
}

void TermWidget::mouseReleaseEvent(QMouseEvent *ev)
{
    if (press_toolbar_) { press_toolbar_ = false; return; }
    QPoint pg = screenToPage(ev->position(), scale_x_, scale_y_, view_x_, view_y_);
    int btn = 1;
    if (ev->button() == Qt::RightButton)  btn = 4;
    if (ev->button() == Qt::MiddleButton) btn = 2;
    sendMouse(btn | MOUSE_RELEASE, pg.x(), pg.y());
}

void TermWidget::mouseMoveEvent(QMouseEvent *ev)
{
    ++moves_count_;
    if (!press_toolbar_ && (ev->buttons() & Qt::LeftButton)) {
        QPoint pg = screenToPage(ev->position(), scale_x_, scale_y_, view_x_, view_y_);
        sendMouse(1 | MOUSE_DRAG, pg.x(), pg.y());
    }
}

bool TermWidget::event(QEvent *ev)
{
    if (ev->type() == QEvent::TouchBegin || ev->type() == QEvent::TouchUpdate ||
        ev->type() == QEvent::TouchEnd)
    {
        auto *te = static_cast<QTouchEvent *>(ev);
        const auto &points = te->points();
        if (!points.isEmpty()) {
            const auto &pt = points.first();
            QPoint pg = screenToPage(pt.position(), scale_x_, scale_y_, view_x_, view_y_);
            if (ev->type() == QEvent::TouchBegin)
                sendMouse(MOUSE_PRESS | MOUSE_LEFT, pg.x(), pg.y());
            else if (ev->type() == QEvent::TouchEnd)
                sendMouse(MOUSE_LEFT | MOUSE_RELEASE, pg.x(), pg.y());
        }
        ev->accept();
        return true;
    }
    return QWidget::event(ev);
}

// ─────────────────────────────────────────────────────────────────────────────
// Server communication  (protocol exactly matching terminal.cc)
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::sendToServer()
{
    s_buffer_out.Write(socket_fd_);
}

void TermWidget::sendTermInfo()
{
    int sz = pageSizeEnum(WinWidth, WinHeight);
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvTermInfo));
    s_buffer_out.Put8(sz);
    s_buffer_out.Put16(WinWidth);
    s_buffer_out.Put16(WinHeight);
    s_buffer_out.Put16(32);   // colour depth — Qt6 always 32-bit
    sendToServer();
    vt::Logger::info("qt_term: SrvTermInfo {}x{} size={}", WinWidth, WinHeight, sz);
}

void TermWidget::sendTouch(int x, int y)
{
    // SrvTouch: Put8(cmd), Put16(WIN_MAIN), Put16(x), Put16(y)
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvTouch));
    s_buffer_out.Put16(WIN_MAIN);
    s_buffer_out.Put16(x);
    s_buffer_out.Put16(y);
    sendToServer();
}

void TermWidget::sendKey(genericChar ch, int xkeysym, int state)
{
    // SrvKey: Put8(cmd), Put16(win_id ignored), Put16(char), Put32(keysym), Put32(state)
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvKey));
    s_buffer_out.Put16(WIN_MAIN);
    s_buffer_out.Put16(static_cast<int>(static_cast<unsigned char>(ch)));
    s_buffer_out.Put32(xkeysym);
    s_buffer_out.Put32(state);
    sendToServer();
}

void TermWidget::sendMouse(int button_code, int x, int y)
{
    // SrvMouse: Put8(cmd), Put16(win_id), Put8(code), Put16(x), Put16(y)
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvMouse));
    s_buffer_out.Put16(WIN_MAIN);
    s_buffer_out.Put8(button_code);   // server reads this with RInt8()
    s_buffer_out.Put16(x);
    s_buffer_out.Put16(y);
    sendToServer();
}

void TermWidget::sendButtonPress(int layer_id, int button_id)
{
    // SrvButtonPress: Put8(cmd), Put16(layer_id), Put16(button_id)
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvButtonPress));
    s_buffer_out.Put16(layer_id);
    s_buffer_out.Put16(button_id);
    sendToServer();
}

void TermWidget::sendZoneData(const ZoneData &d)
{
    // Mirror of ZoneDialog::Send() in term_dialog.cc
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvZoneData));
    s_buffer_out.Put8(d.ztype);
    s_buffer_out.PutString(d.name, 0);
    s_buffer_out.Put32(d.page_id);
    s_buffer_out.Put8(d.group_id);
    s_buffer_out.Put8(d.behave);
    s_buffer_out.Put8(d.confirm);
    s_buffer_out.PutString(d.confirm_msg, 0);
    s_buffer_out.Put8(d.font);
    for (int i = 0; i < 3; ++i) {
        s_buffer_out.Put8(d.frame[i]);
        s_buffer_out.Put8(d.texture[i]);
        s_buffer_out.Put8(d.color[i]);
        s_buffer_out.Put8(d.image[i]);
    }
    s_buffer_out.Put8(d.shape);
    s_buffer_out.Put16(d.shadow);
    s_buffer_out.Put16(d.key);
    s_buffer_out.PutString(d.expression, 0);
    s_buffer_out.PutString(d.message, 0);
    s_buffer_out.PutString(d.filename, 0);
    s_buffer_out.PutString(d.image_filename, 0);
    s_buffer_out.Put8(d.tender_type);
    s_buffer_out.PutString(d.tender_amount, 0);
    s_buffer_out.Put8(d.report_type);
    s_buffer_out.Put8(d.check_disp_num);
    s_buffer_out.Put8(d.video_target);
    s_buffer_out.Put8(d.report_print);
    s_buffer_out.PutString(d.script, 0);
    s_buffer_out.Put32(d.spacing);
    s_buffer_out.Put32(d.qualifier);
    s_buffer_out.Put32(d.amount);
    s_buffer_out.Put8(d.switch_type);
    s_buffer_out.Put8(d.jtype);
    s_buffer_out.Put32(d.jump_id);
    s_buffer_out.Put16(d.customer_type);
    s_buffer_out.Put8(d.drawer_zone_type);
    s_buffer_out.PutString(d.item_name, 0);
    s_buffer_out.PutString(d.item_print_name, 0);
    s_buffer_out.PutString(d.item_zone_name, 0);
    s_buffer_out.Put8(d.itype);
    s_buffer_out.PutString(d.item_location, 0);
    s_buffer_out.PutString(d.item_event_time, 0);
    s_buffer_out.PutString(d.item_total_tickets, 0);
    s_buffer_out.PutString(d.item_available_tickets, 0);
    s_buffer_out.PutString(d.item_price_label, 0);
    s_buffer_out.PutString(d.item_price, 0);
    s_buffer_out.PutString(d.item_subprice, 0);
    s_buffer_out.PutString(d.item_employee_price, 0);
    s_buffer_out.Put8(d.item_family);
    s_buffer_out.Put8(d.item_sales);
    s_buffer_out.Put8(d.item_printer);
    s_buffer_out.Put8(d.item_order);
    sendToServer();
}

// ── helper lambdas for zone dialog combo population ──────────────────────────
static void addFrameItems(QComboBox *cb)
{
    // Each entry: [display-name, frame-code-as-data]
    struct { const char *name; int code; } kFrames[] = {
        {"Default",         1},  {"None",            3},
        {"Raised",         10},  {"Raised (Lit)",   12},  {"Raised (Dark)", 13},
        {"Inset",          20},  {"Inset (Lit)",    22},  {"Inset (Dark)",  23},
        {"Double",         30},  {"Double (Lit)",   32},  {"Double (Dark)", 33},
        {"Border",         40},  {"Sand Border",    42},  {"Inset Border",  44},
        {"Parchment",      45},  {"Double Border",  50},
    };
    for (auto &f : kFrames) cb->addItem(f.name, f.code);
}
static void addTextureItems(QComboBox *cb)
{
    struct { const char *name; int code; } kTex[] = {
        {"Default",    0}, {"Sand",      1}, {"Lit Sand",    2}, {"Dark Sand",   3},
        {"Parchment",  4}, {"Wood",      5}, {"Clear",       6}, {"Marble",      7},
        {"Brick",      8}, {"Leather",   9}, {"Blue",       10}, {"Dark Blue",  11},
        {"Gold",      12}, {"Black",    13}, {"Gray",       14}, {"Green",      15},
    };
    for (auto &t : kTex) cb->addItem(t.name, t.code);
}
static void addColorItems(QComboBox *cb)
{
    struct { const char *name; int code; } kCol[] = {
        {"Default",  255}, {"Black",    0}, {"White",     1}, {"Red",       2},
        {"Green",    3},   {"Blue",     4}, {"Yellow",    5}, {"Brown",     6},
        {"Orange",   7},   {"Purple",   8}, {"Teal",      9}, {"Gray",     10},
        {"Magenta", 11},   {"Lt Blue", 14}, {"Dk Red",   15}, {"Dk Green", 16},
        {"Dk Blue", 17},
    };
    for (auto &c : kCol) cb->addItem(c.name, c.code);
}
static void addFontItems(QComboBox *cb)
{
    struct { const char *name; int code; } kFont[] = {
        {"Default",       0}, {"Times 48",   1}, {"Times 48 Bold",  2},
        {"Times 20",      4}, {"Times 24",   5}, {"Times 34",       6},
        {"Times 20 Bold", 7}, {"Times 24 Bold", 8}, {"Times 34 Bold", 9},
        {"Times 14",     10}, {"Times 14 Bold", 11}, {"Times 18",  12},
        {"Times 18 Bold",13}, {"Courier 18", 14}, {"Courier 18 Bold", 15},
        {"Courier 20",   16}, {"Courier 20 Bold", 17},
    };
    for (auto &f : kFont) cb->addItem(f.name, f.code);
}
static int comboForValue(QComboBox *cb, int value)
{
    // Find the combo index whose user-data matches value; fall back to 0
    for (int i = 0; i < cb->count(); ++i)
        if (cb->itemData(i).toInt() == value) return i;
    return 0;
}
static QGroupBox *makeAppearGroup(const QString &title,
                                  QComboBox **frame, QComboBox **tex, QComboBox **col,
                                  int f, int t, int c)
{
    auto *grp = new QGroupBox(title);
    auto *fl  = new QFormLayout(grp);
    *frame = new QComboBox; addFrameItems(*frame);
    (*frame)->setCurrentIndex(comboForValue(*frame, f));
    *tex   = new QComboBox; addTextureItems(*tex);
    (*tex)->setCurrentIndex(comboForValue(*tex, t));
    *col   = new QComboBox; addColorItems(*col);
    (*col)->setCurrentIndex(comboForValue(*col, c));
    fl->addRow("Frame:",   *frame);
    fl->addRow("Texture:", *tex);
    fl->addRow("Color:",   *col);
    return grp;
}

// Zone type table: {code, name, user-accessible (editor), system-only (super)}
struct ZoneTypeEntry { int code; const char *name; bool user_edit; };
static const ZoneTypeEntry kZoneTypes[] = {
    {0,   "Undefined",          false},
    {1,   "Standard",           true },
    {2,   "Item",               true },
    {3,   "Conditional",        true },
    {4,   "Tender",             true },
    {5,   "Table",              true },
    {6,   "Comment",            false},
    {7,   "Qualifier",          true },
    {8,   "Toggle",             true },
    {9,   "Simple",             true },
    {10,  "Switch",             false},
    {20,  "Login",              false},
    {21,  "Command",            false},
    {23,  "Guest Count",        false},
    {24,  "Logout",             false},
    {30,  "Order Entry",        false},
    {31,  "Check List",         false},
    {32,  "Payment Entry",      false},
    {33,  "User Edit",          false},
    {34,  "Settings",           false},
    {35,  "Tax Settings",       false},
    {36,  "Developer",          false},
    {37,  "Tender Set",         false},
    {38,  "Tax Set",            false},
    {39,  "Money Set",          false},
    {40,  "CC Settings",        false},
    {41,  "CC Msg Settings",    false},
    {50,  "Report",             false},
    {52,  "Schedule",           false},
    {53,  "Print Target",       false},
    {54,  "Split Check",        false},
    {55,  "Drawer Manage",      false},
    {56,  "Hardware",           false},
    {57,  "Time Settings",      false},
    {58,  "Table Assign",       false},
    {59,  "Check Display",      false},
    {61,  "Kill System",        false},
    {62,  "Payout",             false},
    {63,  "Drawer Assign",      false},
    {64,  "Order Flow",         false},
    {66,  "Search",             false},
    {67,  "Split Kitchen",      false},
    {68,  "End Day",            false},
    {69,  "Read",               false},
    {70,  "Job Security",       false},
    {71,  "Inventory",          false},
    {72,  "Recipe",             false},
    {73,  "Vendor",             false},
    {74,  "Labor",              false},
    {75,  "Item List",          false},
    {76,  "Invoice",            false},
    {77,  "Phrase",             false},
    {78,  "Item Target",        false},
    {79,  "Receipt Settings",   false},
    {80,  "Merchant",           false},
    {81,  "License",            false},
    {82,  "Account",            false},
    {83,  "Order Add",          false},
    {84,  "Order Delete",       false},
    {85,  "Order Display",      false},
    {86,  "Chart",              false},
    {87,  "Video Target",       false},
    {88,  "Expense",            false},
    {89,  "Status Button",      false},
    {90,  "CDU",                false},
    {91,  "Receipts",           false},
    {92,  "Customer Info",      false},
    {93,  "Check Edit",         false},
    {94,  "Credit Card List",   false},
    {95,  "Expire Message",     false},
    {96,  "Revenue Groups",     false},
    {97,  "Image Button",       true },
    {98,  "Item Normal",        true },
    {99,  "Item Modifier",      true },
    {100, "Item Method",        true },
    {101, "Item Substitute",    true },
    {102, "Item Pound",         true },
    {103, "Item Admission",     true },
    {104, "Order Comment",      false},
    {107, "Clear System",       false},
    {108, "Index Tab",          true },
    {109, "Language Button",    true },
    {110, "Calculation Settings",false},
};
static const int kZoneTypeCount = (int)(sizeof(kZoneTypes)/sizeof(kZoneTypes[0]));

static bool isItemZoneType(int code) {
    return code == 2 || (code >= 98 && code <= 103);
}

void TermWidget::showZoneDialog()
{
    static const char *kBehave[] = {
        "Normal","Toggle","Blink","Select","Double-Click","Miss"
    };
    static const char *kJumpType[] = {
        "None","Normal","Stealth","Return","Home","Script","Index","Password"
    };

    ZoneData d = pending_zone_;

    QDialog dlg(this, Qt::Dialog);
    dlg.setWindowTitle("Zone Properties");
    dlg.setMinimumWidth(520);
    dlg.setModal(true);

    auto *tabs    = new QTabWidget;
    auto *mainLay = new QVBoxLayout(&dlg);
    mainLay->addWidget(tabs);

    // ── Tab 1: Basic ─────────────────────────────────────────────────────────
    auto *tab1 = new QWidget;
    auto *fl1  = new QFormLayout(tab1);

    auto *nameEdit = new QLineEdit(QString::fromUtf8(d.name));
    fl1->addRow("Name:", nameEdit);

    auto *typeCb = new QComboBox;
    int typeCbSel = 0;
    for (int i = 0; i < kZoneTypeCount; ++i) {
        const auto &e = kZoneTypes[i];
        if (!d.full_edit && !e.user_edit) continue;
        typeCb->addItem(QString("%1 (%2)").arg(e.name).arg(e.code), e.code);
        if (e.code == d.ztype) typeCbSel = typeCb->count() - 1;
    }
    typeCb->setCurrentIndex(typeCbSel);
    fl1->addRow("Type:", typeCb);

    auto *behaveCb = new QComboBox;
    for (auto *s : kBehave) behaveCb->addItem(s);
    behaveCb->setCurrentIndex(qBound(0, d.behave, 5));
    fl1->addRow("Behavior:", behaveCb);

    auto *fontCb = new QComboBox;
    addFontItems(fontCb);
    fontCb->setCurrentIndex(comboForValue(fontCb, d.font));
    fl1->addRow("Font:", fontCb);

    auto *shadowSpin = new QSpinBox;
    shadowSpin->setRange(0, 20);
    shadowSpin->setValue(d.shadow);
    fl1->addRow("Shadow depth:", shadowSpin);

    auto *confirmChk = new QCheckBox("Require confirm");
    confirmChk->setChecked(d.confirm != 0);
    fl1->addRow("", confirmChk);

    auto *confirmMsgEdit = new QLineEdit(QString::fromUtf8(d.confirm_msg));
    fl1->addRow("Confirm msg:", confirmMsgEdit);

    // Super-user-only fields
    QSpinBox *pageIdSpin = nullptr;
    QSpinBox *groupIdSpin = nullptr;
    QLineEdit *filenameEdit = nullptr;
    QLineEdit *imageFilenameEdit = nullptr;
    if (d.full_edit) {
        pageIdSpin = new QSpinBox;
        pageIdSpin->setRange(-99999, 99999);
        pageIdSpin->setValue(d.page_id);
        fl1->addRow("Page ID:", pageIdSpin);

        groupIdSpin = new QSpinBox;
        groupIdSpin->setRange(0, 255);
        groupIdSpin->setValue(d.group_id);
        fl1->addRow("Group ID:", groupIdSpin);

        filenameEdit = new QLineEdit(QString::fromUtf8(d.filename));
        fl1->addRow("Script file:", filenameEdit);

        imageFilenameEdit = new QLineEdit(QString::fromUtf8(d.image_filename));
        fl1->addRow("Image file:", imageFilenameEdit);
    }

    tabs->addTab(tab1, "Basic");

    // ── Tab 2: Appearance ────────────────────────────────────────────────────
    auto *tab2 = new QWidget;
    auto *fl2  = new QVBoxLayout(tab2);

    QComboBox *frame1Cb, *tex1Cb, *col1Cb;
    QComboBox *frame2Cb, *tex2Cb, *col2Cb;
    QComboBox *frame3Cb, *tex3Cb, *col3Cb;

    auto *grp1 = makeAppearGroup("State 1 (normal)",
                                 &frame1Cb, &tex1Cb, &col1Cb,
                                 d.frame[0], d.texture[0], d.color[0]);
    auto *grp2 = makeAppearGroup("State 2 (selected)",
                                 &frame2Cb, &tex2Cb, &col2Cb,
                                 d.frame[1], d.texture[1], d.color[1]);
    auto *grp3 = makeAppearGroup("State 3 (active)",
                                 &frame3Cb, &tex3Cb, &col3Cb,
                                 d.frame[2], d.texture[2], d.color[2]);
    grp2->setEnabled(d.states >= 2);
    grp3->setEnabled(d.states >= 3);

    fl2->addWidget(grp1);
    fl2->addWidget(grp2);
    fl2->addWidget(grp3);
    fl2->addStretch();
    tabs->addTab(tab2, "Appearance");

    // ── Tab 3: Action ────────────────────────────────────────────────────────
    auto *tab3 = new QWidget;
    auto *fl3  = new QFormLayout(tab3);

    auto *msgEdit = new QLineEdit(QString::fromUtf8(d.message));
    fl3->addRow("Message:", msgEdit);

    auto *exprEdit = new QLineEdit(QString::fromUtf8(d.expression));
    fl3->addRow("Expression:", exprEdit);

    auto *jumpCb = new QComboBox;
    for (auto *s : kJumpType) jumpCb->addItem(s);
    jumpCb->setCurrentIndex(qBound(0, d.jtype, 7));
    fl3->addRow("Jump type:", jumpCb);

    auto *jumpIdSpin = new QSpinBox;
    jumpIdSpin->setRange(-9999, 99999);
    jumpIdSpin->setValue(d.jump_id);
    fl3->addRow("Jump page ID:", jumpIdSpin);

    auto *keySpin = new QSpinBox;
    keySpin->setRange(0, 65535);
    keySpin->setValue(d.key);
    fl3->addRow("Key binding:", keySpin);

    QLineEdit *tenderAmtEdit = nullptr;
    if (d.full_edit) {
        auto *scriptEdit = new QLineEdit(QString::fromUtf8(d.script));
        fl3->addRow("Script:", scriptEdit);

        tenderAmtEdit = new QLineEdit(QString::fromUtf8(d.tender_amount));
        fl3->addRow("Tender amount:", tenderAmtEdit);
    }

    tabs->addTab(tab3, "Action");

    // ── Tab 4: Item (shown for item zone types) ───────────────────────────────
    QLineEdit *itemNameEdit = nullptr, *itemPrintEdit = nullptr, *itemZoneEdit = nullptr;
    QLineEdit *itemPriceEdit = nullptr, *itemSubpriceEdit = nullptr;
    QLineEdit *itemEmpPriceEdit = nullptr;
    QSpinBox  *itemFamilySpin = nullptr, *itemSalesSpin = nullptr;
    QSpinBox  *itemPrinterSpin = nullptr, *itemOrderSpin = nullptr;
    QWidget   *tab4 = nullptr;
    if (isItemZoneType(d.ztype)) {
        tab4 = new QWidget;
        auto *fl4 = new QFormLayout(tab4);

        itemNameEdit = new QLineEdit(QString::fromUtf8(d.item_name));
        fl4->addRow("Item name:", itemNameEdit);

        itemPrintEdit = new QLineEdit(QString::fromUtf8(d.item_print_name));
        fl4->addRow("Print name:", itemPrintEdit);

        itemZoneEdit = new QLineEdit(QString::fromUtf8(d.item_zone_name));
        fl4->addRow("Zone name:", itemZoneEdit);

        itemPriceEdit = new QLineEdit(QString::fromUtf8(d.item_price));
        fl4->addRow("Price:", itemPriceEdit);

        itemSubpriceEdit = new QLineEdit(QString::fromUtf8(d.item_subprice));
        fl4->addRow("Subprice:", itemSubpriceEdit);

        itemEmpPriceEdit = new QLineEdit(QString::fromUtf8(d.item_employee_price));
        fl4->addRow("Employee price:", itemEmpPriceEdit);

        itemFamilySpin = new QSpinBox;
        itemFamilySpin->setRange(0, 255);
        itemFamilySpin->setValue(d.item_family);
        fl4->addRow("Family:", itemFamilySpin);

        itemSalesSpin = new QSpinBox;
        itemSalesSpin->setRange(0, 255);
        itemSalesSpin->setValue(d.item_sales);
        fl4->addRow("Sales:", itemSalesSpin);

        itemPrinterSpin = new QSpinBox;
        itemPrinterSpin->setRange(0, 255);
        itemPrinterSpin->setValue(d.item_printer);
        fl4->addRow("Printer:", itemPrinterSpin);

        itemOrderSpin = new QSpinBox;
        itemOrderSpin->setRange(0, 255);
        itemOrderSpin->setValue(d.item_order);
        fl4->addRow("Order:", itemOrderSpin);

        tabs->addTab(tab4, "Item");
    }

    // ── OK / Cancel ──────────────────────────────────────────────────────────
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLay->addWidget(btnBox);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    auto cpy = [](char *dst, size_t n, const QString &src) {
        std::strncpy(dst, src.toUtf8().constData(), n - 1);
        dst[n - 1] = '\0';
    };

    cpy(d.name,        sizeof(d.name),        nameEdit->text());
    cpy(d.confirm_msg, sizeof(d.confirm_msg),  confirmMsgEdit->text());
    cpy(d.message,     sizeof(d.message),      msgEdit->text());
    cpy(d.expression,  sizeof(d.expression),   exprEdit->text());

    d.ztype   = typeCb->currentData().toInt();
    d.behave  = behaveCb->currentIndex();
    d.font    = fontCb->currentData().toInt();
    d.shadow  = shadowSpin->value();
    d.confirm = confirmChk->isChecked() ? 1 : 0;
    d.jtype   = jumpCb->currentIndex();
    d.jump_id = jumpIdSpin->value();
    d.key     = keySpin->value();

    if (d.full_edit) {
        if (pageIdSpin)     d.page_id  = pageIdSpin->value();
        if (groupIdSpin)    d.group_id = groupIdSpin->value();
        if (filenameEdit)   cpy(d.filename,       sizeof(d.filename),       filenameEdit->text());
        if (imageFilenameEdit) cpy(d.image_filename, sizeof(d.image_filename), imageFilenameEdit->text());
        if (tenderAmtEdit)  cpy(d.tender_amount,  sizeof(d.tender_amount),  tenderAmtEdit->text());
    }

    d.frame[0]   = frame1Cb->currentData().toInt();
    d.texture[0] = tex1Cb->currentData().toInt();
    d.color[0]   = col1Cb->currentData().toInt();
    d.frame[1]   = frame2Cb->currentData().toInt();
    d.texture[1] = tex2Cb->currentData().toInt();
    d.color[1]   = col2Cb->currentData().toInt();
    d.frame[2]   = frame3Cb->currentData().toInt();
    d.texture[2] = tex3Cb->currentData().toInt();
    d.color[2]   = col3Cb->currentData().toInt();

    if (tab4) {
        if (itemNameEdit)    cpy(d.item_name,          sizeof(d.item_name),          itemNameEdit->text());
        if (itemPrintEdit)   cpy(d.item_print_name,    sizeof(d.item_print_name),    itemPrintEdit->text());
        if (itemZoneEdit)    cpy(d.item_zone_name,     sizeof(d.item_zone_name),     itemZoneEdit->text());
        if (itemPriceEdit)   cpy(d.item_price,         sizeof(d.item_price),         itemPriceEdit->text());
        if (itemSubpriceEdit)cpy(d.item_subprice,      sizeof(d.item_subprice),      itemSubpriceEdit->text());
        if (itemEmpPriceEdit)cpy(d.item_employee_price,sizeof(d.item_employee_price),itemEmpPriceEdit->text());
        if (itemFamilySpin)  d.item_family  = itemFamilySpin->value();
        if (itemSalesSpin)   d.item_sales   = itemSalesSpin->value();
        if (itemPrinterSpin) d.item_printer = itemPrinterSpin->value();
        if (itemOrderSpin)   d.item_order   = itemOrderSpin->value();
    }

    sendZoneData(d);
}

// ─────────────────────────────────────────────────────────────────────────────
// Page Properties Dialog (EDITPAGE → showPageDialog → sendPageData)
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::showPageDialog()
{
    static const char *kPageSize[] = {
        "640×480","768×1024","800×480","800×600","1024×600","1024×768",
        "1280×800","1280×1024","1366×768","1440×900","1600×900","1600×1200",
        "1680×1050","1920×1080","1920×1200","2560×1440","2560×1600"
    };
    static const char *kPageType[] = {
        "System","Table","Index","Item","","","","","","Item 2","Table 2"
    };
    static const char *kIndex[] = {
        "General","Bar","Dining"
    };

    PageData d = pending_page_;

    QDialog dlg(this, Qt::Dialog);
    dlg.setWindowTitle(d.id == 0 ? "New Page" : QString("Page Properties – %1")
                       .arg(QString::fromUtf8(d.name)));
    dlg.setMinimumWidth(500);
    dlg.setModal(true);

    auto *tabs    = new QTabWidget;
    auto *mainLay = new QVBoxLayout(&dlg);
    mainLay->addWidget(tabs);

    // ── Tab 1: General ────────────────────────────────────────────────────────
    auto *tab1 = new QWidget;
    auto *fl1  = new QFormLayout(tab1);

    auto *nameEdit = new QLineEdit(QString::fromUtf8(d.name));
    fl1->addRow("Name:", nameEdit);

    auto *sizeCb = new QComboBox;
    for (auto *s : kPageSize) sizeCb->addItem(s);
    sizeCb->setCurrentIndex(qBound(0, d.size - 1, (int)(sizeof(kPageSize)/sizeof(kPageSize[0])) - 1));
    fl1->addRow("Size:", sizeCb);

    auto *typeCb = new QComboBox;
    for (int i = 0; i < (int)(sizeof(kPageType)/sizeof(kPageType[0])); ++i)
        if (kPageType[i][0]) typeCb->addItem(kPageType[i], i);
    typeCb->setCurrentIndex(qMax(0, comboForValue(typeCb, d.page_type)));
    if (!d.full_edit) typeCb->setEnabled(false);
    fl1->addRow("Type:", typeCb);

    QSpinBox *idSpin = nullptr;
    if (d.full_edit) {
        idSpin = new QSpinBox;
        idSpin->setRange(-99999, 99999);
        idSpin->setValue(d.id);
        fl1->addRow("Page ID:", idSpin);
    }

    auto *titleColorCb = new QComboBox;
    addColorItems(titleColorCb);
    titleColorCb->setCurrentIndex(comboForValue(titleColorCb, d.title_color));
    fl1->addRow("Title color:", titleColorCb);

    auto *imageCb = new QComboBox;
    addTextureItems(imageCb);
    imageCb->setCurrentIndex(comboForValue(imageCb, d.image));
    fl1->addRow("Background:", imageCb);

    auto *indexCb = new QComboBox;
    for (int i = 0; i < (int)(sizeof(kIndex)/sizeof(kIndex[0])); ++i)
        indexCb->addItem(kIndex[i], i);
    indexCb->setCurrentIndex(qBound(0, d.index, 2));
    fl1->addRow("Index:", indexCb);

    QSpinBox *parentSpin = nullptr;
    if (d.full_edit) {
        parentSpin = new QSpinBox;
        parentSpin->setRange(-99999, 99999);
        parentSpin->setValue(d.parent_id);
        fl1->addRow("Parent page:", parentSpin);
    }

    tabs->addTab(tab1, "General");

    // ── Tab 2: Defaults ───────────────────────────────────────────────────────
    auto *tab2 = new QWidget;
    auto *fl2  = new QFormLayout(tab2);

    auto *fontCb = new QComboBox;
    addFontItems(fontCb);
    fontCb->setCurrentIndex(comboForValue(fontCb, d.default_font));
    fl2->addRow("Default font:", fontCb);

    auto *spacingSpin = new QSpinBox;
    spacingSpin->setRange(0, 10);
    spacingSpin->setValue(d.default_spacing);
    fl2->addRow("Spacing:", spacingSpin);

    auto *shadowSpin = new QSpinBox;
    shadowSpin->setRange(0, 100);
    shadowSpin->setValue(d.default_shadow);
    fl2->addRow("Shadow:", shadowSpin);

    tabs->addTab(tab2, "Defaults");

    // ── Tab 3: Appearance ─────────────────────────────────────────────────────
    auto *tab3 = new QWidget;
    auto *fl3  = new QVBoxLayout(tab3);

    QComboBox *frame1Cb, *tex1Cb, *col1Cb;
    QComboBox *frame2Cb, *tex2Cb, *col2Cb;
    QComboBox *frame3Cb, *tex3Cb, *col3Cb;

    auto *grp1 = makeAppearGroup("State 1 (normal)",
                                 &frame1Cb, &tex1Cb, &col1Cb,
                                 d.frame[0], d.texture[0], d.color[0]);
    auto *grp2 = makeAppearGroup("State 2 (selected)",
                                 &frame2Cb, &tex2Cb, &col2Cb,
                                 d.frame[1], d.texture[1], d.color[1]);
    auto *grp3 = makeAppearGroup("State 3 (active)",
                                 &frame3Cb, &tex3Cb, &col3Cb,
                                 d.frame[2], d.texture[2], d.color[2]);
    fl3->addWidget(grp1);
    fl3->addWidget(grp2);
    fl3->addWidget(grp3);
    fl3->addStretch();
    tabs->addTab(tab3, "Appearance");

    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLay->addWidget(btnBox);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    auto cpy = [](char *dst, size_t n, const QString &src) {
        std::strncpy(dst, src.toUtf8().constData(), n - 1);
        dst[n - 1] = '\0';
    };
    cpy(d.name, sizeof(d.name), nameEdit->text());

    d.size           = sizeCb->currentIndex() + 1;
    d.page_type      = typeCb->currentData().toInt();
    d.title_color    = titleColorCb->currentData().toInt();
    d.image          = imageCb->currentData().toInt();
    d.index          = indexCb->currentData().toInt();
    d.default_font   = fontCb->currentData().toInt();
    d.default_spacing= spacingSpin->value();
    d.default_shadow = shadowSpin->value();
    if (idSpin)     d.id        = idSpin->value();
    if (parentSpin) d.parent_id = parentSpin->value();

    d.frame[0]   = frame1Cb->currentData().toInt();
    d.texture[0] = tex1Cb->currentData().toInt();
    d.color[0]   = col1Cb->currentData().toInt();
    d.frame[1]   = frame2Cb->currentData().toInt();
    d.texture[1] = tex2Cb->currentData().toInt();
    d.color[1]   = col2Cb->currentData().toInt();
    d.frame[2]   = frame3Cb->currentData().toInt();
    d.texture[2] = tex3Cb->currentData().toInt();
    d.color[2]   = col3Cb->currentData().toInt();

    sendPageData(d);
}

void TermWidget::sendPageData(const PageData &d)
{
    // Mirror of PageDialog::Send() / ReadPage() wire format
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvPageData));
    s_buffer_out.Put8(d.size);
    s_buffer_out.Put8(d.page_type);
    s_buffer_out.PutString(d.name, 0);
    s_buffer_out.Put32(d.id);
    s_buffer_out.Put8(d.title_color);
    s_buffer_out.Put8(d.image);
    s_buffer_out.Put8(d.default_font);
    for (int i = 0; i < 3; ++i) {
        s_buffer_out.Put8(d.frame[i]);
        s_buffer_out.Put8(d.texture[i]);
        s_buffer_out.Put8(d.color[i]);
    }
    s_buffer_out.Put8(d.default_spacing);
    s_buffer_out.Put16(d.default_shadow);
    s_buffer_out.Put32(d.parent_id);
    s_buffer_out.Put8(d.index);
    sendToServer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Global Defaults Dialog (DEFPAGE → showDefaultsDialog → sendDefaultsData)
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::showDefaultsDialog()
{
    static const char *kPageSize[] = {
        "640×480","768×1024","800×480","800×600","1024×600","1024×768",
        "1280×800","1280×1024","1366×768","1440×900","1600×900","1600×1200",
        "1680×1050","1920×1080","1920×1200","2560×1440","2560×1600"
    };

    DefaultsData d = pending_defaults_;

    QDialog dlg(this, Qt::Dialog);
    dlg.setWindowTitle("Zone Defaults");
    dlg.setMinimumWidth(500);
    dlg.setModal(true);

    auto *tabs    = new QTabWidget;
    auto *mainLay = new QVBoxLayout(&dlg);
    mainLay->addWidget(tabs);

    // ── Tab 1: Typography ────────────────────────────────────────────────────
    auto *tab1 = new QWidget;
    auto *fl1  = new QFormLayout(tab1);

    auto *fontCb = new QComboBox;
    addFontItems(fontCb);
    fontCb->setCurrentIndex(comboForValue(fontCb, d.default_font));
    fl1->addRow("Default font:", fontCb);

    auto *spacingSpin = new QSpinBox;
    spacingSpin->setRange(0, 10);
    spacingSpin->setValue(d.default_spacing);
    fl1->addRow("Spacing:", spacingSpin);

    auto *shadowSpin = new QSpinBox;
    shadowSpin->setRange(0, 100);
    shadowSpin->setValue(d.default_shadow);
    fl1->addRow("Shadow:", shadowSpin);

    auto *titleColorCb = new QComboBox;
    addColorItems(titleColorCb);
    titleColorCb->setCurrentIndex(comboForValue(titleColorCb, d.default_title_color));
    fl1->addRow("Title color:", titleColorCb);

    auto *bgTexCb = new QComboBox;
    addTextureItems(bgTexCb);
    bgTexCb->setCurrentIndex(comboForValue(bgTexCb, d.default_image));
    fl1->addRow("Background:", bgTexCb);

    auto *sizeCb = new QComboBox;
    for (auto *s : kPageSize) sizeCb->addItem(s);
    sizeCb->setCurrentIndex(qBound(0, d.default_size - 1, (int)(sizeof(kPageSize)/sizeof(kPageSize[0])) - 1));
    fl1->addRow("Default page size:", sizeCb);

    tabs->addTab(tab1, "General");

    // ── Tab 2: Appearance ────────────────────────────────────────────────────
    auto *tab2 = new QWidget;
    auto *fl2  = new QVBoxLayout(tab2);

    QComboBox *frame1Cb, *tex1Cb, *col1Cb;
    QComboBox *frame2Cb, *tex2Cb, *col2Cb;
    QComboBox *frame3Cb, *tex3Cb, *col3Cb;

    auto *grp1 = makeAppearGroup("State 1 (normal)",
                                 &frame1Cb, &tex1Cb, &col1Cb,
                                 d.frame[0], d.texture[0], d.color[0]);
    auto *grp2 = makeAppearGroup("State 2 (selected)",
                                 &frame2Cb, &tex2Cb, &col2Cb,
                                 d.frame[1], d.texture[1], d.color[1]);
    auto *grp3 = makeAppearGroup("State 3 (active)",
                                 &frame3Cb, &tex3Cb, &col3Cb,
                                 d.frame[2], d.texture[2], d.color[2]);
    fl2->addWidget(grp1);
    fl2->addWidget(grp2);
    fl2->addWidget(grp3);
    fl2->addStretch();
    tabs->addTab(tab2, "Appearance");

    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLay->addWidget(btnBox);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    d.default_font         = fontCb->currentData().toInt();
    d.default_spacing      = spacingSpin->value();
    d.default_shadow       = shadowSpin->value();
    d.default_title_color  = titleColorCb->currentData().toInt();
    d.default_image        = bgTexCb->currentData().toInt();
    d.default_size         = sizeCb->currentIndex() + 1;
    d.frame[0]   = frame1Cb->currentData().toInt();
    d.texture[0] = tex1Cb->currentData().toInt();
    d.color[0]   = col1Cb->currentData().toInt();
    d.frame[1]   = frame2Cb->currentData().toInt();
    d.texture[1] = tex2Cb->currentData().toInt();
    d.color[1]   = col2Cb->currentData().toInt();
    d.frame[2]   = frame3Cb->currentData().toInt();
    d.texture[2] = tex3Cb->currentData().toInt();
    d.color[2]   = col3Cb->currentData().toInt();

    sendDefaultsData(d);
}

void TermWidget::sendDefaultsData(const DefaultsData &d)
{
    // Mirror of ReadDefaults() wire format (SrvDefPage)
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvDefPage));
    s_buffer_out.Put8(d.default_font);
    s_buffer_out.Put16(d.default_shadow);
    s_buffer_out.Put8(d.default_spacing);
    for (int i = 0; i < 3; ++i) {
        s_buffer_out.Put8(d.frame[i]);
        s_buffer_out.Put8(d.texture[i]);
        s_buffer_out.Put8(d.color[i]);
    }
    s_buffer_out.Put8(d.default_image);
    s_buffer_out.Put8(d.default_title_color);
    s_buffer_out.Put8(d.default_size);
    sendToServer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Zone Dialog (EDITMULTIZONE → showMultiZoneDialog → sendMultiZoneData)
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::showMultiZoneDialog()
{
    MultiZoneData d = pending_multizone_;

    QDialog dlg(this, Qt::Dialog);
    dlg.setWindowTitle("Edit Multiple Zones");
    dlg.setMinimumWidth(480);
    dlg.setModal(true);

    auto *mainLay = new QVBoxLayout(&dlg);
    auto *fl      = new QFormLayout;
    mainLay->addLayout(fl);

    auto *lbl = new QLabel("Fields left blank or set to '(mixed)' will not be changed.");
    lbl->setWordWrap(true);
    mainLay->insertWidget(0, lbl);

    // Helper: combo with a "(mixed)" sentinel at index 0, data = -1
    auto makeMixedCombo = [](std::function<void(QComboBox*)> populate, int cur) -> QComboBox * {
        auto *cb = new QComboBox;
        cb->addItem("(mixed)", -1);
        populate(cb);
        if (cur >= 0) {
            int idx = 1;
            for (int i = 1; i < cb->count(); ++i) {
                if (cb->itemData(i).toInt() == cur) { idx = i; break; }
            }
            cb->setCurrentIndex(idx);
        }
        return cb;
    };

    static const char *kBehave[] = {
        "Normal","Toggle","Blink","Select","Double-Click","Miss"
    };
    auto *behaveCb = new QComboBox;
    behaveCb->addItem("(mixed)", -1);
    for (int i = 0; i < 6; ++i) behaveCb->addItem(kBehave[i], i);
    behaveCb->setCurrentIndex(d.behave >= 0 ? d.behave + 1 : 0);
    fl->addRow("Behavior:", behaveCb);

    auto *fontCb = makeMixedCombo([](QComboBox *cb){ addFontItems(cb); }, d.font);
    fl->addRow("Font:", fontCb);

    auto *shadowSpin = new QSpinBox;
    shadowSpin->setSpecialValueText("(mixed)");
    shadowSpin->setRange(-1, 100);
    shadowSpin->setValue(d.shadow);
    fl->addRow("Shadow:", shadowSpin);

    fl->addRow(new QLabel("─── State 1 (normal) ───"));

    auto *frame1Cb = makeMixedCombo([](QComboBox *cb){ addFrameItems(cb); }, d.frame1);
    fl->addRow("Frame 1:", frame1Cb);

    auto *tex1Cb = makeMixedCombo([](QComboBox *cb){ addTextureItems(cb); }, d.tex1);
    fl->addRow("Texture 1:", tex1Cb);

    auto *col1Cb = makeMixedCombo([](QComboBox *cb){ addColorItems(cb); }, d.color1);
    fl->addRow("Color 1:", col1Cb);

    fl->addRow(new QLabel("─── State 2 (selected) ───"));

    auto *frame2Cb = makeMixedCombo([](QComboBox *cb){ addFrameItems(cb); }, d.frame2);
    fl->addRow("Frame 2:", frame2Cb);

    auto *tex2Cb = makeMixedCombo([](QComboBox *cb){ addTextureItems(cb); }, d.tex2);
    fl->addRow("Texture 2:", tex2Cb);

    auto *col2Cb = makeMixedCombo([](QComboBox *cb){ addColorItems(cb); }, d.color2);
    fl->addRow("Color 2:", col2Cb);

    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLay->addWidget(btnBox);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    d.behave = behaveCb->currentData().toInt();
    d.font   = fontCb->currentData().toInt();
    d.shadow = shadowSpin->value();
    d.frame1 = frame1Cb->currentData().toInt();
    d.tex1   = tex1Cb->currentData().toInt();
    d.color1 = col1Cb->currentData().toInt();
    d.frame2 = frame2Cb->currentData().toInt();
    d.tex2   = tex2Cb->currentData().toInt();
    d.color2 = col2Cb->currentData().toInt();

    sendMultiZoneData(d);
}

void TermWidget::sendMultiZoneData(const MultiZoneData &d)
{
    // Mirror of ReadMultiZone(): 10 × WInt16 (no command byte prefix needed —
    // SrvZoneChanges triggers ReadMultiZone on the server)
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvZoneChanges));
    s_buffer_out.Put16(d.behave);
    s_buffer_out.Put16(d.font);
    s_buffer_out.Put16(d.frame1);
    s_buffer_out.Put16(d.tex1);
    s_buffer_out.Put16(d.color1);
    s_buffer_out.Put16(d.frame2);
    s_buffer_out.Put16(d.tex2);
    s_buffer_out.Put16(d.color2);
    s_buffer_out.Put16(d.shape);
    s_buffer_out.Put16(d.shadow);
    sendToServer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Page List Dialog (LISTSTART..LISTEND → showListDialog → SrvListSelect)
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::showListDialog()
{
    if (list_items_.isEmpty()) return;

    QDialog dlg(this, Qt::Dialog);
    dlg.setWindowTitle("Page List");
    dlg.setMinimumWidth(320);
    dlg.setMinimumHeight(400);
    dlg.setModal(true);

    auto *mainLay = new QVBoxLayout(&dlg);
    auto *lw      = new QListWidget;
    for (const QString &s : list_items_) lw->addItem(s);
    lw->setCurrentRow(0);
    mainLay->addWidget(lw);

    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLay->addWidget(btnBox);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(lw, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted) return;

    int selected = lw->currentRow() + 1;   // 1-based, matching Motif ListDialog
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvListSelect));
    s_buffer_out.Put32(selected);
    sendToServer();
}

void TermWidget::sendError(const char *msg)
{
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvError));
    s_buffer_out.PutString(msg ? std::string(msg) : std::string(), 0);
    sendToServer();
}

void TermWidget::sendShutdown()
{
    s_buffer_out.Put8(ToInt(ServerProtocol::SrvShutdown));
    sendToServer();
}

void TermWidget::start()
{
    {
        QPainter p(&back_buffer_);
        p.fillRect(back_buffer_.rect(), Qt::black);
        p.setPen(Qt::white);
        p.setFont(getFont(FONT_TIMES_34));
        p.drawText(back_buffer_.rect(), Qt::AlignCenter, "Please Wait");
    }
    update();
    sendTermInfo();
}

// ─────────────────────────────────────────────────────────────────────────────
// Socket slot
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::onSocketReady()
{
    int val = s_buffer_in.Read(socket_fd_);
    if (val <= 0) {
        handleDisconnect();
        return;
    }
    processBuffer();
}

void TermWidget::handleDisconnect()
{
    vt::Logger::warn("qt_term: server connection lost");
    if (socket_notifier_) socket_notifier_->setEnabled(false);
    QPainter p(&back_buffer_);
    p.fillRect(back_buffer_.rect(), Qt::black);
    p.setPen(Qt::white);
    p.setFont(getFont(FONT_TIMES_24));
    p.drawText(back_buffer_.rect(), Qt::AlignCenter, "Server disconnected");
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
// Command dispatch
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::processBuffer()
{
    while (s_buffer_in.CurrSize() > 0) {
        int code = s_buffer_in.Get8();
        if (code < 0) break;
        dispatchCommand(code);
    }
}

void TermWidget::dispatchCommand(int code)
{
    namespace P = TerminalProtocol;
    switch (code) {
    case P::FLUSH:           update();             break;
    case P::UPDATEALL:       cmdUpdateAll();       break;
    case P::UPDATEAREA:      cmdUpdateArea();      break;
    case P::SETCLIP:         cmdSetClip();         break;
    case P::BLANKPAGE:       cmdBlankPage();       break;
    case P::BACKGROUND:      cmdBackground();      break;
    case P::TITLEBAR:        cmdTitleBar();        break;
    case P::ZONE:            cmdZone();            break;
    case P::TEXTL:           cmdText(ALIGN_LEFT);     break;
    case P::TEXTC:           cmdText(ALIGN_CENTER);   break;
    case P::TEXTR:           cmdText(ALIGN_RIGHT);    break;
    case P::ZONETEXTL:       cmdZoneText(ALIGN_LEFT);    break;
    case P::ZONETEXTC:       cmdZoneText(ALIGN_CENTER);  break;
    case P::ZONETEXTR:       cmdZoneText(ALIGN_RIGHT);   break;
    case P::SHADOW:          cmdShadow();          break;
    case P::RECTANGLE:       cmdRectangle();       break;
    case P::SOLID_RECTANGLE: cmdSolidRectangle();  break;
    case P::PIXMAP:          cmdPixmap();          break;
    case P::HLINE:           cmdHLine();           break;
    case P::VLINE:           cmdVLine();           break;
    case P::FRAME:           cmdFrame();           break;
    case P::FILLEDFRAME:     cmdFilledFrame();     break;
    case P::STATUSBAR:       cmdStatusBar();       break;
    case P::EDITCURSOR:      cmdEditCursor();      break;
    case P::CURSOR:          cmdCursor();          break;
    case P::NEWWINDOW:       cmdNewWindow();       break;
    case P::SHOWWINDOW:      cmdShowWindow();      break;
    case P::KILLWINDOW:      cmdKillWindow();      break;
    case P::TARGETWINDOW:    cmdTargetWindow();    break;

    case P::SET_EMBOSSED:
        use_embossed_     = (s_buffer_in.Get8() != 0); break;
    case P::SET_ANTIALIAS:
        use_antialiasing_ = (s_buffer_in.Get8() != 0); break;
    case P::SET_DROP_SHADOW:
        use_drop_shadows_ = (s_buffer_in.Get8() != 0); break;
    case P::SET_SHADOW_OFFSET:
        shadow_offset_x_  = s_buffer_in.Get16();
        shadow_offset_y_  = s_buffer_in.Get16();
        break;
    case P::SET_SHADOW_BLUR:
        shadow_blur_      = s_buffer_in.Get8(); break;
    case P::SET_ICONIFY:
        setWindowState(windowState() | Qt::WindowMinimized); break;

    case P::BLANKSCREEN:
        screen_blanked_ = true; back_buffer_.fill(Qt::black); update(); break;
    case P::BLANKTIME:
        blank_time_ = s_buffer_in.Get16(); resetScreensaver(); break;
    case P::USERINPUT:
        resetScreensaver(); break;

    case P::SETMESSAGE:   { RStr(); } break;
    case P::CLEARMESSAGE: break;
    case P::STORENAME:    { RStr(); } break;

    case P::FLUSH_TS:     break;
    case P::CALIBRATE_TS: break;
    case P::CONNTIMEOUT:  { s_buffer_in.Get16(); } break;

    case P::TRANSLATE:
    case P::TRANSLATIONS: {
        static std::array<char, 1024> key{}, val{};
        int count = s_buffer_in.Get8();
        for (int i = 0; i < count; ++i) {
            RStr(key.data()); RStr(val.data());
        }
        break;
    }

    case P::SELECTOFF:
        select_anchor_set_   = false;
        edit_cursor_visible_ = false;
        update();
        break;
    case P::SELECTUPDATE: {
        int sx = s_buffer_in.Get16();
        int sy = s_buffer_in.Get16();
        if (!select_anchor_set_) {
            select_ax_ = sx; select_ay_ = sy;
            select_ex_ = sx; select_ey_ = sy;
            select_anchor_set_ = true;
        } else {
            select_ex_ = sx; select_ey_ = sy;
        }
        update();
        break;
    }

    // ── edit-mode dialog data: skip without desync ───────────────────────────
    case P::EDITZONE: {
        // Read all fields into pending_zone_, then show the Qt dialog
        pending_zone_ = ZoneData{};
        pending_zone_.full_edit = s_buffer_in.Get8();
        pending_zone_.ztype     = s_buffer_in.Get8();
        RStr(pending_zone_.name);
        pending_zone_.page_id   = s_buffer_in.Get32();
        pending_zone_.group_id  = s_buffer_in.Get8();
        pending_zone_.behave    = s_buffer_in.Get8();
        pending_zone_.confirm   = s_buffer_in.Get8();
        RStr(pending_zone_.confirm_msg);
        pending_zone_.font      = s_buffer_in.Get8();
        pending_zone_.states    = s_buffer_in.Get8();
        for (int i = 0; i < 3; ++i) {
            pending_zone_.frame[i]   = s_buffer_in.Get8();
            pending_zone_.texture[i] = s_buffer_in.Get8();
            pending_zone_.color[i]   = s_buffer_in.Get8();
            pending_zone_.image[i]   = s_buffer_in.Get8();
        }
        pending_zone_.shape   = s_buffer_in.Get8();
        pending_zone_.shadow  = s_buffer_in.Get16();
        pending_zone_.key     = s_buffer_in.Get16();
        RStr(pending_zone_.expression);
        RStr(pending_zone_.message);
        RStr(pending_zone_.filename);
        RStr(pending_zone_.image_filename);
        pending_zone_.tender_type    = s_buffer_in.Get8();
        RStr(pending_zone_.tender_amount);
        pending_zone_.report_type    = s_buffer_in.Get8();
        pending_zone_.check_disp_num = s_buffer_in.Get8();
        pending_zone_.video_target   = s_buffer_in.Get8();
        pending_zone_.report_print   = s_buffer_in.Get8();
        RStr(pending_zone_.script);
        pending_zone_.spacing     = s_buffer_in.Get32();
        pending_zone_.qualifier   = s_buffer_in.Get32();
        pending_zone_.amount      = s_buffer_in.Get32();
        pending_zone_.switch_type = s_buffer_in.Get8();
        pending_zone_.jtype       = s_buffer_in.Get8();
        pending_zone_.jump_id     = s_buffer_in.Get32();
        pending_zone_.customer_type    = s_buffer_in.Get16();
        pending_zone_.drawer_zone_type = s_buffer_in.Get8();
        RStr(pending_zone_.item_name);
        RStr(pending_zone_.item_print_name);
        RStr(pending_zone_.item_zone_name);
        pending_zone_.itype = s_buffer_in.Get8();
        RStr(pending_zone_.item_location);
        RStr(pending_zone_.item_event_time);
        RStr(pending_zone_.item_total_tickets);
        RStr(pending_zone_.item_available_tickets);
        RStr(pending_zone_.item_price_label);
        RStr(pending_zone_.item_price);
        RStr(pending_zone_.item_subprice);
        RStr(pending_zone_.item_employee_price);
        pending_zone_.item_family  = s_buffer_in.Get8();
        pending_zone_.item_sales   = s_buffer_in.Get8();
        pending_zone_.item_printer = s_buffer_in.Get8();
        pending_zone_.item_order   = s_buffer_in.Get8();
        QTimer::singleShot(0, this, [this]{ showZoneDialog(); });
        break;
    }
    case P::EDITPAGE: {
        pending_page_ = PageData{};
        pending_page_.full_edit     = s_buffer_in.Get8();
        pending_page_.size          = s_buffer_in.Get8();
        pending_page_.page_type     = s_buffer_in.Get8();
        RStr(pending_page_.name);
        pending_page_.id            = s_buffer_in.Get32();
        pending_page_.title_color   = s_buffer_in.Get8();
        pending_page_.image         = s_buffer_in.Get8();
        pending_page_.default_font  = s_buffer_in.Get8();
        for (int i = 0; i < 3; ++i) {
            pending_page_.frame[i]   = s_buffer_in.Get8();
            pending_page_.texture[i] = s_buffer_in.Get8();
            pending_page_.color[i]   = s_buffer_in.Get8();
        }
        pending_page_.default_spacing = s_buffer_in.Get8();
        pending_page_.default_shadow  = s_buffer_in.Get16();
        pending_page_.parent_id       = s_buffer_in.Get32();
        pending_page_.index           = s_buffer_in.Get8();
        QTimer::singleShot(0, this, [this]{ showPageDialog(); });
        break;
    }
    case P::EDITMULTIZONE: {
        pending_multizone_ = MultiZoneData{};
        pending_multizone_.full_edit = s_buffer_in.Get8();
        pending_multizone_.behave  = s_buffer_in.Get16();
        pending_multizone_.font    = s_buffer_in.Get16();
        pending_multizone_.frame1  = s_buffer_in.Get16();
        pending_multizone_.tex1    = s_buffer_in.Get16();
        pending_multizone_.color1  = s_buffer_in.Get16();
        pending_multizone_.frame2  = s_buffer_in.Get16();
        pending_multizone_.tex2    = s_buffer_in.Get16();
        pending_multizone_.color2  = s_buffer_in.Get16();
        pending_multizone_.shape   = s_buffer_in.Get16();
        pending_multizone_.shadow  = s_buffer_in.Get16();
        QTimer::singleShot(0, this, [this]{ showMultiZoneDialog(); });
        break;
    }
    case P::DEFPAGE: {
        pending_defaults_ = DefaultsData{};
        pending_defaults_.default_font    = s_buffer_in.Get8();
        pending_defaults_.default_shadow  = s_buffer_in.Get16();
        pending_defaults_.default_spacing = s_buffer_in.Get8();
        for (int i = 0; i < 3; ++i) {
            pending_defaults_.frame[i]   = s_buffer_in.Get8();
            pending_defaults_.texture[i] = s_buffer_in.Get8();
            pending_defaults_.color[i]   = s_buffer_in.Get8();
        }
        pending_defaults_.default_image       = s_buffer_in.Get8();
        pending_defaults_.default_title_color = s_buffer_in.Get8();
        pending_defaults_.default_size        = s_buffer_in.Get8();
        QTimer::singleShot(0, this, [this]{ showDefaultsDialog(); });
        break;
    }
    case P::LISTSTART:
        list_items_.clear();
        break;
    case P::LISTITEM: {
        static std::array<char,512> _s{};
        RStr(_s.data());
        list_items_.append(QString::fromUtf8(_s.data()));
        break;
    }
    case P::LISTEND:
        QTimer::singleShot(0, this, [this]{ showListDialog(); });
        break;

    case P::PUSHBUTTON: cmdPushButton(); break;

    // Same wire format as PUSHBUTTON: <id, x, y, w, h, label, font, c1, c2>
    case P::ITEMLIST:
    case P::ITEMMENU:
    case P::TEXTENTRY: {
        static std::array<char,256> _s{};
        s_buffer_in.Get16(); s_buffer_in.Get16(); s_buffer_in.Get16(); // id, x, y
        s_buffer_in.Get16(); s_buffer_in.Get16();                      // w, h
        RStr(_s.data());                                                // label
        s_buffer_in.Get8(); s_buffer_in.Get8(); s_buffer_in.Get8();    // font, c1, c2
        break;
    }
    case P::CONSOLE: {
        // <id, x, y, w, h, c1, c2>
        s_buffer_in.Get16(); s_buffer_in.Get16(); s_buffer_in.Get16(); // id, x, y
        s_buffer_in.Get16(); s_buffer_in.Get16();                      // w, h
        s_buffer_in.Get8(); s_buffer_in.Get8();                        // c1, c2
        break;
    }
    case P::PAGEINDEX: {
        // <id, x, y, w, h>
        s_buffer_in.Get16(); s_buffer_in.Get16(); s_buffer_in.Get16(); // id, x, y
        s_buffer_in.Get16(); s_buffer_in.Get16();                      // w, h
        break;
    }

    case P::SOUND:
        { s_buffer_in.Get16(); } break;

    case P::BELL:
        { s_buffer_in.Get16(); QApplication::beep(); } break;

    case P::ICONIFY:
        setWindowState(windowState() | Qt::WindowMinimized); break;

    case P::DIE:
        vt::Logger::info("qt_term: received DIE");
        QApplication::quit();
        break;

    case P::CC_AUTH_CMD:
    case P::CC_PREAUTH_CMD:
    case P::CC_VOID_CMD:
    case P::CC_VOID_CANCEL_CMD:
    case P::CC_REFUND_CMD:
    case P::CC_REFUND_CANCEL_CMD:
    case P::CC_SETTLE_CMD:
    case P::CC_INIT_CMD:
    case P::CC_TOTALS_CMD:
    case P::CC_DETAILS_CMD:
    case P::CC_CLEARSAF_CMD:
    case P::CC_SAFDETAILS_CMD:
        break;

    default:
        vt::Logger::warn("qt_term: unknown command {}", code);
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Drawing commands
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::cmdUpdateAll()
{
    use_clip_ = false;
    select_anchor_set_   = false;
    edit_cursor_visible_ = false;
    update();
}

void TermWidget::cmdUpdateArea()
{
    int px = s_buffer_in.Get16();
    int py = s_buffer_in.Get16();
    int pw = s_buffer_in.Get16();
    int ph = s_buffer_in.Get16();
    use_clip_ = false;
    // Transform page-space rect to screen-space before scheduling repaint
    int sx = view_x_ + (int)(px * scale_x_);
    int sy = view_y_ + (int)(py * scale_y_);
    int sw = (int)(pw * scale_x_) + 1;
    int sh = (int)(ph * scale_y_) + 1;
    update(sx, sy, sw, sh);
}

void TermWidget::cmdSetClip()
{
    clip_x_ = s_buffer_in.Get16();
    clip_y_ = s_buffer_in.Get16();
    clip_w_ = s_buffer_in.Get16();
    clip_h_ = s_buffer_in.Get16();
    use_clip_ = true;
}

void TermWidget::cmdBlankPage()
{
    int mode      = s_buffer_in.Get8();
    int texture   = s_buffer_in.Get8();
    int color     = s_buffer_in.Get8();
    int size_enum = s_buffer_in.Get8();
    int split     = s_buffer_in.Get16();
    int split_opt = s_buffer_in.Get8();
    static std::array<char, 256> title{}, time_str{};
    RStr(title.data());
    RStr(time_str.data());
    (void)mode; (void)split; (void)split_opt;

    // A new page is a clean slate — discard any clip region, cursor state,
    // and selection left over from the previous page's partial updates.
    use_clip_           = false;
    select_anchor_set_  = false;
    edit_cursor_visible_ = false;

    int pw = pageSizeWidth(size_enum);
    int ph = pageSizeHeight(size_enum);
    if (pw <= 0) pw = WinWidth;
    if (ph <= 0) ph = WinHeight;
    page_w_  = pw;
    page_h_  = ph;
    page_x_  = (WinWidth  - pw) / 2;
    page_y_  = (WinHeight - ph) / 2;
    bg_texture_ = texture;

    // Compute display scale so the page fills the screen while preserving aspect ratio
    float sc = qMin((float)WinWidth / pw, (float)WinHeight / ph);
    scale_x_ = sc;
    scale_y_ = sc;
    view_w_  = (int)(pw * sc);
    view_h_  = (int)(ph * sc);
    view_x_  = (WinWidth  - view_w_) / 2;
    view_y_  = (WinHeight - view_h_) / 2;

    QPainter p(&back_buffer_);
    p.fillRect(back_buffer_.rect(), Qt::black);
    fillTexture(p, page_x_, page_y_, pw, ph, texture);

    title_height_ = 0;
    if (color != 0) {
        frame_width_  = std::max(2, pw / 200);
        title_height_ = getFont(FONT_TIMES_20).pointSize() * 2 + 4;
        p.fillRect(page_x_, page_y_, pw, title_height_, textColor(color));
        if (time_str[0]) {
            p.setPen(textColor(COLOR_WHITE));
            p.setFont(getFont(FONT_TIMES_20));
            p.drawText(QRect(page_x_, page_y_, pw, title_height_),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString(time_str.data()).trimmed());
        }
    }
}

void TermWidget::cmdBackground()
{
    int x, y, w, h;
    if (use_clip_) {
        x = page_x_ + clip_x_; y = page_y_ + clip_y_;
        w = clip_w_;            h = clip_h_;
    } else {
        x = page_x_; y = page_y_; w = page_w_; h = page_h_;
    }
    QPainter p(&back_buffer_);
    fillTexture(p, x, y, w, h, bg_texture_);
}

void TermWidget::cmdTitleBar()
{
    static std::array<char, 256> ts{};
    RStr(ts.data());
    setWindowTitle(QString("ViewTouch – ") + ts.data());
}

void TermWidget::cmdZone()
{
    int x  = s_buffer_in.Get16();
    int y  = s_buffer_in.Get16();
    int w  = s_buffer_in.Get16();
    int h  = s_buffer_in.Get16();
    int fr = s_buffer_in.Get8();
    int tx = s_buffer_in.Get8();
    int sh = s_buffer_in.Get8();
    (void)sh;

    if (fr == ZF_NONE || fr == ZF_HIDDEN) return;
    if (fr == ZF_UNCHANGED) fr = ZF_RAISED1;

    int px = page_x_ + x, py = page_y_ + y;
    int thick = 1, flags = 0, interior = tx;

    switch (fr) {
    case ZF_RAISED:  case ZF_RAISED1:  thick=1; break;
    case ZF_RAISED2:                   thick=1; flags=FRAME_LIT; break;
    case ZF_RAISED3:                   thick=1; flags=FRAME_DARK; break;
    case ZF_INSET:   case ZF_INSET1:   thick=1; flags=FRAME_INSET; break;
    case ZF_INSET2:                    thick=1; flags=FRAME_INSET|FRAME_LIT; break;
    case ZF_INSET3:                    thick=1; flags=FRAME_INSET|FRAME_DARK; break;
    case ZF_DOUBLE:  case ZF_DOUBLE1:  thick=2; break;
    case ZF_DOUBLE2:                   thick=2; flags=FRAME_LIT; break;
    case ZF_DOUBLE3:                   thick=2; flags=FRAME_DARK; break;
    case ZF_BORDER:                    thick=2; break;
    case ZF_CLEAR_BORDER:              thick=2; interior=IMAGE_CLEAR; break;
    case ZF_SAND_BORDER:               thick=2; interior=IMAGE_SAND; break;
    case ZF_LIT_SAND_BORDER:           thick=2; interior=IMAGE_LIT_SAND; break;
    case ZF_INSET_BORDER:              thick=2; interior=IMAGE_DARK_SAND; flags=FRAME_INSET; break;
    case ZF_PARCHMENT_BORDER:          thick=2; interior=IMAGE_PARCHMENT; break;
    case ZF_DOUBLE_BORDER:             thick=3; break;
    case ZF_LIT_DOUBLE_BORDER:         thick=3; interior=IMAGE_LIT_SAND; break;
    default: thick=1; break;
    }

    // IMAGE_UNCHANGED/IMAGE_DEFAULT mean "inherit the page background".
    if (interior == IMAGE_UNCHANGED || interior == IMAGE_DEFAULT)
        interior = bg_texture_;

    QPainter p(&targetPixmap(target_window_));
    if (interior != IMAGE_CLEAR)
        fillTexture(p, px + thick, py + thick, w - 2*thick, h - 2*thick, interior);
    drawFrame(p, px, py, w, h, thick, flags);
}

void TermWidget::cmdText(int align)
{
    static std::array<char, 1024> s{};
    RStr(s.data());
    int x     = s_buffer_in.Get16();
    int y     = s_buffer_in.Get16();
    int color = s_buffer_in.Get8();
    int font  = s_buffer_in.Get8();
    int max_w = s_buffer_in.Get16();

    QPainter p(&targetPixmap(target_window_));
    drawText(p, QString::fromUtf8(s.data()),
             page_x_ + x, page_y_ + y,
             color, font, align, max_w);
}

void TermWidget::cmdZoneText(int align)
{
    static std::array<char, 1024> s{};
    RStr(s.data());
    int x     = s_buffer_in.Get16();
    int y     = s_buffer_in.Get16();
    int w     = s_buffer_in.Get16();
    int h     = s_buffer_in.Get16();
    int color = s_buffer_in.Get8();
    int font  = s_buffer_in.Get8();

    QPainter p(&targetPixmap(target_window_));
    drawZoneText(p, QString::fromUtf8(s.data()),
                 page_x_ + x, page_y_ + y, w, h,
                 color, font, align);
}

void TermWidget::cmdShadow()
{
    int x = s_buffer_in.Get16();
    int y = s_buffer_in.Get16();
    int w = s_buffer_in.Get16();
    int h = s_buffer_in.Get16();
    int d = s_buffer_in.Get8();
    int sh= s_buffer_in.Get8();
    (void)sh;

    QPainter p(&targetPixmap(target_window_));
    drawShadow(p, page_x_ + x, page_y_ + y, w, h, d);
}

void TermWidget::cmdRectangle()
{
    int x  = s_buffer_in.Get16();
    int y  = s_buffer_in.Get16();
    int w  = s_buffer_in.Get16();
    int h  = s_buffer_in.Get16();
    int tx = s_buffer_in.Get8();

    QPainter p(&targetPixmap(target_window_));
    fillTexture(p, page_x_ + x, page_y_ + y, w, h, tx);
}

void TermWidget::cmdSolidRectangle()
{
    int x     = s_buffer_in.Get16();
    int y     = s_buffer_in.Get16();
    int w     = s_buffer_in.Get16();
    int h     = s_buffer_in.Get16();
    int color = s_buffer_in.Get16();

    QPainter p(&targetPixmap(target_window_));
    p.fillRect(page_x_ + x, page_y_ + y, w, h, textColor(color));
}

void TermWidget::cmdPixmap()
{
    int x = s_buffer_in.Get16();
    int y = s_buffer_in.Get16();
    int w = s_buffer_in.Get16();
    int h = s_buffer_in.Get16();
    static std::array<char, 512> filename{};
    RStr(filename.data());

    QPixmap img(QString::fromUtf8(filename.data()));
    if (!img.isNull()) {
        QPainter p(&targetPixmap(target_window_));
        p.drawPixmap(page_x_ + x, page_y_ + y, w, h, img);
    }
}

void TermWidget::cmdHLine()
{
    int x  = s_buffer_in.Get16();
    int y  = s_buffer_in.Get16();
    int l  = s_buffer_in.Get16();
    int c  = s_buffer_in.Get8();
    int pw = s_buffer_in.Get8();

    QPainter p(&targetPixmap(target_window_));
    p.setPen(QPen(textColor(c), pw));
    int px = page_x_ + x, py = page_y_ + y;
    p.drawLine(px, py, px + l, py);
}

void TermWidget::cmdVLine()
{
    int x  = s_buffer_in.Get16();
    int y  = s_buffer_in.Get16();
    int l  = s_buffer_in.Get16();
    int c  = s_buffer_in.Get8();
    int pw = s_buffer_in.Get8();

    QPainter p(&targetPixmap(target_window_));
    p.setPen(QPen(textColor(c), pw));
    int px = page_x_ + x, py = page_y_ + y;
    p.drawLine(px, py, px, py + l);
}

void TermWidget::cmdFrame()
{
    int x     = s_buffer_in.Get16();
    int y     = s_buffer_in.Get16();
    int w     = s_buffer_in.Get16();
    int h     = s_buffer_in.Get16();
    int thick = s_buffer_in.Get8();
    int flags = s_buffer_in.Get8();

    QPainter p(&targetPixmap(target_window_));
    drawFrame(p, page_x_ + x, page_y_ + y, w, h, thick, flags);
}

void TermWidget::cmdFilledFrame()
{
    int x       = s_buffer_in.Get16();
    int y       = s_buffer_in.Get16();
    int w       = s_buffer_in.Get16();
    int h       = s_buffer_in.Get16();
    int fw      = s_buffer_in.Get8();
    int texture = s_buffer_in.Get8();
    int flags   = s_buffer_in.Get8();

    QPainter p(&targetPixmap(target_window_));
    drawFilledFrame(p, page_x_ + x, page_y_ + y, w, h, fw, texture, flags);
}

void TermWidget::cmdStatusBar()
{
    int x       = s_buffer_in.Get16();
    int y       = s_buffer_in.Get16();
    int w       = s_buffer_in.Get16();
    int h       = s_buffer_in.Get16();
    int bar_col = s_buffer_in.Get8();
    static std::array<char, 256> text{};
    RStr(text.data());
    int font    = s_buffer_in.Get8();
    int txt_col = s_buffer_in.Get8();

    int px = page_x_ + x, py = page_y_ + y;
    QPainter p(&targetPixmap(target_window_));
    p.fillRect(px, py, w, h, textColor(bar_col));
    drawZoneText(p, QString::fromUtf8(text.data()), px, py, w, h, txt_col, font, ALIGN_CENTER);
}

void TermWidget::cmdEditCursor()
{
    int x = s_buffer_in.Get16();
    int y = s_buffer_in.Get16();
    int w = s_buffer_in.Get16();
    int h = s_buffer_in.Get16();
    // Store in page-space coords; paintEvent draws handles as an overlay.
    edit_cursor_rect_    = QRect(page_x_ + x, page_y_ + y, w, h);
    edit_cursor_visible_ = true;
}

void TermWidget::cmdCursor()
{
    int type = s_buffer_in.Get16();
    switch (type) {
    case CURSOR_BLANK:   setCursor(Qt::BlankCursor); break;
    case CURSOR_POINTER: setCursor(Qt::ArrowCursor); break;
    case CURSOR_WAIT:    setCursor(Qt::WaitCursor);  break;
    default:             setCursor(Qt::ArrowCursor); break;
    }
}

void TermWidget::cmdNewWindow()
{
    int id = s_buffer_in.Get16();
    int x  = s_buffer_in.Get16();
    int y  = s_buffer_in.Get16();
    int w  = s_buffer_in.Get16();
    int h  = s_buffer_in.Get16();
    int fr = s_buffer_in.Get8();
    static std::array<char, 256> title{};
    RStr(title.data());
    (void)fr;

    WindowLayer wl;
    wl.id      = id;
    wl.rect    = QRect(page_x_ + x, page_y_ + y, w, h);
    wl.pix     = QPixmap(w, h);
    wl.pix.fill(Qt::transparent);
    wl.title   = QString::fromUtf8(title.data());
    wl.visible = false;
    windows_[id] = std::move(wl);
    target_window_ = id;  // subsequent draw commands go into this window
}

void TermWidget::cmdPushButton()
{
    static std::array<char, 256> label{};
    int btn_id = s_buffer_in.Get16();
    int bx     = s_buffer_in.Get16();
    int by     = s_buffer_in.Get16();
    int bw     = s_buffer_in.Get16();
    int bh     = s_buffer_in.Get16();
    RStr(label.data());
    int font   = s_buffer_in.Get8();
    int c_fg   = s_buffer_in.Get8();
    int c_bg   = s_buffer_in.Get8();

    // Store button hit rect in the window for click detection
    auto it = windows_.find(target_window_);
    if (it != windows_.end()) {
        PushButton pb;
        pb.id   = btn_id;
        pb.rect = QRect(bx, by, bw, bh);
        it->buttons.push_back(pb);
    }

    // Render the button into the target window pixmap
    QPixmap &pix = targetPixmap(target_window_);
    QPainter p(&pix);
    QColor bg = textColor(c_bg);
    p.fillRect(bx, by, bw, bh, bg);

    // Raised frame: lighter top/left edges, darker bottom/right
    for (int i = 0; i < 2; ++i) {
        p.setPen(bg.lighter(160));
        p.drawLine(bx+i,      by+i,      bx+bw-2-i, by+i);
        p.drawLine(bx+i,      by+i,      bx+i,      by+bh-2-i);
        p.setPen(bg.darker(160));
        p.drawLine(bx+bw-1-i, by+i,      bx+bw-1-i, by+bh-1-i);
        p.drawLine(bx+i,      by+bh-1-i, bx+bw-1-i, by+bh-1-i);
    }

    QString text = QString::fromUtf8(label.data()).replace("\\", "\n");
    p.setPen(textColor(c_fg));
    p.setFont(getFont(font));
    p.setRenderHint(QPainter::TextAntialiasing, use_antialiasing_);
    p.drawText(QRect(bx+4, by+4, bw-8, bh-8),
               Qt::AlignCenter | Qt::TextWordWrap, text);
}

void TermWidget::cmdShowWindow()
{
    int id = s_buffer_in.Get16();
    auto it = windows_.find(id);
    if (it != windows_.end()) {
        it->visible = true;
        target_window_ = 0;  // return drawing to main buffer after window is shown
        update();             // full repaint — window rect is in back_buffer_ space, not widget space
    }
}

void TermWidget::cmdKillWindow()
{
    int id = s_buffer_in.Get16();
    windows_.remove(id);
    if (target_window_ == id) target_window_ = 0;
    update();
}

void TermWidget::cmdTargetWindow()
{
    target_window_ = s_buffer_in.Get16();
}

// ─────────────────────────────────────────────────────────────────────────────
// Drawing primitives
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::drawText(QPainter &p, const QString &text,
                          int x, int y, int color, int font_id, int align,
                          int max_w)
{
    if (color == COLOR_CLEAR) return;
    if (text.isEmpty()) return;

    p.setFont(getFont(font_id));
    QFontMetrics fm(p.font());
    int text_w = fm.horizontalAdvance(text);
    if (max_w > 0 && text_w > max_w) return;

    int draw_x = x;
    if (align == ALIGN_CENTER)     draw_x = x - text_w / 2;
    else if (align == ALIGN_RIGHT) draw_x = x - text_w;

    int baseline = y + fm.ascent();
    p.setRenderHint(QPainter::TextAntialiasing, use_antialiasing_);

    if (use_drop_shadows_ || use_embossed_) {
        p.setPen(shadowColor(color));
        p.drawText(draw_x + shadow_offset_x_, baseline + shadow_offset_y_, text);
    }
    p.setPen(textColor(color));
    p.drawText(draw_x, baseline, text);
}

void TermWidget::drawZoneText(QPainter &p, const QString &text,
                              int x, int y, int w, int h,
                              int color, int font_id, int align)
{
    if (color == COLOR_CLEAR) return;
    if (text.isEmpty()) return;

    // ViewTouch uses '\' as a line separator (same role as '\n').
    // Convert before passing to Qt so multi-line labels display correctly.
    QString display = text;
    display.replace(QLatin1Char('\\'), QLatin1Char('\n'));

    p.setFont(getFont(font_id));
    p.setRenderHint(QPainter::TextAntialiasing, use_antialiasing_);

    Qt::Alignment qt_align = Qt::AlignVCenter;
    if (align == ALIGN_LEFT)        qt_align |= Qt::AlignLeft;
    else if (align == ALIGN_CENTER) qt_align |= Qt::AlignHCenter;
    else                            qt_align |= Qt::AlignRight;

    // Qt::TextWordWrap lets individual segments wrap if they're still too wide
    // for the zone, matching the Xlib renderer's word-break behaviour.
    const int flags = static_cast<int>(qt_align) | Qt::TextWordWrap;

    QRect rect(x, y, w, h);
    if (use_drop_shadows_ || use_embossed_) {
        p.setPen(shadowColor(color));
        p.drawText(rect.translated(shadow_offset_x_, shadow_offset_y_), flags, display);
    }
    p.setPen(textColor(color));
    p.drawText(rect, flags, display);
}

void TermWidget::drawFrame(QPainter &p, int x, int y, int w, int h,
                           int thick, int flags)
{
    bool inset = (flags & FRAME_INSET) != 0;
    bool lit   = (flags & FRAME_LIT)   != 0;
    bool dark  = (flags & FRAME_DARK)  != 0;

    QColor top, bottom, left, right;
    if (lit)       { top=c_lte_; bottom=c_lbe_; left=c_lle_; right=c_lre_; }
    else if (dark) { top=c_dte_; bottom=c_dbe_; left=c_dle_; right=c_dre_; }
    else           { top=c_te_;  bottom=c_be_;  left=c_le_;  right=c_re_;  }
    if (inset) { std::swap(top, bottom); std::swap(left, right); }

    const int t = (thick > 0) ? thick : 1;
    p.fillRect(x,       y,       w,       t,       top);
    p.fillRect(x,       y+h-t,   w,       t,       bottom);
    p.fillRect(x,       y+t,     t,       h-2*t,   left);
    p.fillRect(x+w-t,   y+t,     t,       h-2*t,   right);
}

void TermWidget::drawFilledFrame(QPainter &p,
                                 int x, int y, int w, int h,
                                 int fw, int texture, int flags)
{
    fillTexture(p, x, y, w, h, texture);
    drawFrame(p, x, y, w, h, fw, flags);
}

void TermWidget::drawShadow(QPainter &p, int x, int y, int w, int h, int depth)
{
    // Soft multi-layer shadow instead of a single hard rectangle
    for (int i = depth; i > 0; --i) {
        int alpha = 12 + (depth - i) * 10;
        if (alpha > 80) alpha = 80;
        p.fillRect(x + i, y + i, w, h, QColor(0, 0, 0, alpha));
    }
}

void TermWidget::fillTexture(QPainter &p, int x, int y, int w, int h, int texture)
{
    if (w <= 0 || h <= 0) return;
    if (texture == IMAGE_CLEAR) return;
    if (texture == IMAGE_BLACK) { p.fillRect(x, y, w, h, QColor(26, 26, 26)); return; }

    const int tid = (texture >= 0 && texture < IMAGE_COUNT) ? texture : 0;

    // Use the real XPM tile when it loaded successfully.
    if (!textures_[tid].isNull()) {
        p.fillRect(x, y, w, h, QBrush(textures_[tid]));
        return;
    }

    // Fallback: flat/gradient fill (kModernTextures) when no XPM is available.
    const TextureGrad &tg = kModernTextures[static_cast<size_t>(tid)];
    if (tg.top == tg.base) {
        p.fillRect(x, y, w, h, QColor(QRgb(tg.base)));
    } else {
        QLinearGradient grad(x, y, x, y + h);
        grad.setColorAt(0.0, QColor(QRgb(tg.top)));
        grad.setColorAt(1.0, QColor(QRgb(tg.base)));
        p.fillRect(x, y, w, h, QBrush(grad));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Screensaver
// ─────────────────────────────────────────────────────────────────────────────
void TermWidget::onScreensaverTick()
{
    // Server drives blanking via BLANKSCREEN command; no client-side logic needed
}

void TermWidget::resetScreensaver()
{
    screen_blanked_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Page size helpers
// ─────────────────────────────────────────────────────────────────────────────
int TermWidget::pageSizeEnum(int win_w, int win_h) noexcept
{
    if (win_w >= 2560 && win_h >= 1600) return PAGE_SIZE_2560x1600;
    if (win_w >= 2560)                  return PAGE_SIZE_2560x1440;
    if (win_w >= 1920 && win_h >= 1200) return PAGE_SIZE_1920x1200;
    if (win_w >= 1920)                  return PAGE_SIZE_1920x1080;
    if (win_w >= 1680)                  return PAGE_SIZE_1680x1050;
    if (win_w >= 1600 && win_h >= 1200) return PAGE_SIZE_1600x1200;
    if (win_w >= 1600)                  return PAGE_SIZE_1600x900;
    if (win_w >= 1440)                  return PAGE_SIZE_1440x900;
    if (win_w >= 1366)                  return PAGE_SIZE_1366x768;
    if (win_w >= 1280 && win_h >= 1024) return PAGE_SIZE_1280x1024;
    if (win_w >= 1280)                  return PAGE_SIZE_1280x800;
    if (win_w >= 1024 && win_h >= 768)  return PAGE_SIZE_1024x768;
    if (win_w >= 1024)                  return PAGE_SIZE_1024x600;
    if (win_w >= 800  && win_h >= 600)  return PAGE_SIZE_800x600;
    if (win_w >= 800)                   return PAGE_SIZE_800x480;
    if (win_h >= 1024)                  return PAGE_SIZE_768x1024;
    return PAGE_SIZE_640x480;
}

int TermWidget::pageSizeWidth(int sz) noexcept
{
    if (sz >= 1 && sz < static_cast<int>(kPageSizes.size()))
        return kPageSizes[sz].first;
    return 0;
}

int TermWidget::pageSizeHeight(int sz) noexcept
{
    if (sz >= 1 && sz < static_cast<int>(kPageSizes.size()))
        return kPageSizes[sz].second;
    return 0;
}

// MOC-generated code for Q_OBJECT (must be last)
#include "qt_term_view.moc"
