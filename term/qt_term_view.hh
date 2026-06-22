/*
 * Copyright ViewTouch, Inc., 1995, 1996, 1997, 1998, 2025, 2026
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 * qt_term_view.hh
 * Qt6 terminal rendering backend — replaces the Xlib/Xft/Xt layer.
 *
 * Architecture:
 *   vt_main ──(CharQueue binary protocol over Unix socket)──> TermWidget
 *   TermWidget renders to a QPixmap back-buffer, blits on paintEvent.
 *   Qt's xcb platform plugin uses DISPLAY just like Xlib did, so
 *   XSDL and SSH X11 forwarding continue to work unchanged.
 */

#pragma once

#include "remote_link.hh"     // CharQueue, TerminalProtocol, ServerProtocol
#include "image_data.hh"      // ImageData[], textures enum

#include <QWidget>
#include <QPixmap>
#include <QFont>
#include <QColor>
#include <QBrush>
#include <QSocketNotifier>
#include <QTimer>
#include <QMap>
#include <QPushButton>
#include <QMouseEvent>
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Global protocol I/O (used by term_credit.cc which we still compile in)
// ─────────────────────────────────────────────────────────────────────────────
extern int SocketNo;
extern int TermNo;
extern int IsTermLocal;
extern int WinWidth;
extern int WinHeight;

// These are implemented in qt_term_view.cc and delegate to the global CharQueues.
extern int  SendNow() noexcept;
extern int  WInt8(int val)        noexcept;
extern int  RInt8()               noexcept;
extern int  WInt16(int val)       noexcept;
extern int  RInt16()              noexcept;
extern int  WInt32(int val)       noexcept;
extern int  RInt32()              noexcept;
extern long WLong(long val)       noexcept;
extern long RLong()               noexcept;
extern int  WFlt(Flt val)         noexcept;
extern Flt  RFlt()                noexcept;
extern int  WStr(const char* s, int len = 0);
extern genericChar* RStr(genericChar* s = nullptr);

// ─────────────────────────────────────────────────────────────────────────────
// Colour index constants (match term_view.hh)
// ─────────────────────────────────────────────────────────────────────────────
#define COLOR_BLACK        0
#define COLOR_WHITE        1
#define COLOR_RED          2
#define COLOR_GREEN        3
#define COLOR_BLUE         4
#define COLOR_YELLOW       5
#define COLOR_BROWN        6
#define COLOR_ORANGE       7
#define COLOR_PURPLE       8
#define COLOR_TEAL         9
#define COLOR_GRAY         10
#define COLOR_MAGENTA      11
#define COLOR_REDORANGE    12
#define COLOR_SEAGREEN     13
#define COLOR_LT_BLUE      14
#define COLOR_DK_RED       15
#define COLOR_DK_GREEN     16
#define COLOR_DK_BLUE      17
#define COLOR_DK_TEAL      18
#define COLOR_DK_MAGENTA   19
#define COLOR_DK_SEAGREEN  20
#define COLOR_DEFAULT      255
#define COLOR_CLEAR        253
#define TEXT_COLORS        21

// Alignment
#define ALIGN_LEFT         0
#define ALIGN_CENTER       1
#define ALIGN_RIGHT        2

// Fonts
#define FONT_DEFAULT     0
#define FONT_TIMES_48    1
#define FONT_TIMES_48B   2
#define FONT_TIMES_20    4
#define FONT_TIMES_24    5
#define FONT_TIMES_34    6
#define FONT_TIMES_20B   7
#define FONT_TIMES_24B   8
#define FONT_TIMES_34B   9
#define FONT_TIMES_14    10
#define FONT_TIMES_14B   11
#define FONT_TIMES_18    12
#define FONT_TIMES_18B   13
#define FONT_COURIER_18  14
#define FONT_COURIER_18B 15
#define FONT_COURIER_20  16
#define FONT_COURIER_20B 17

// Zone frame types
#define ZF_UNCHANGED 0
#define ZF_DEFAULT   1
#define ZF_HIDDEN    2
#define ZF_NONE      3
#define ZF_RAISED    10
#define ZF_RAISED1   11
#define ZF_RAISED2   12
#define ZF_RAISED3   13
#define ZF_INSET     20
#define ZF_INSET1    21
#define ZF_INSET2    22
#define ZF_INSET3    23
#define ZF_DOUBLE    30
#define ZF_DOUBLE1   31
#define ZF_DOUBLE2   32
#define ZF_DOUBLE3   33
#define ZF_BORDER            40
#define ZF_CLEAR_BORDER      41
#define ZF_SAND_BORDER       42
#define ZF_LIT_SAND_BORDER   43
#define ZF_INSET_BORDER      44
#define ZF_PARCHMENT_BORDER  45
#define ZF_DOUBLE_BORDER     50
#define ZF_LIT_DOUBLE_BORDER 51

// Frame flags
#define SHAPE_RECTANGLE 1
#define FRAME_LIT       8
#define FRAME_DARK      16
#define FRAME_INSET     32
#define FRAME_2COLOR    64

// Cursor types
#define CURSOR_DEFAULT 0
#define CURSOR_BLANK   1
#define CURSOR_POINTER 2
#define CURSOR_WAIT    3

// Page size enum (mirrors term_view.hh page_sizes)
enum page_sizes : uint8_t {
    PAGE_SIZE_640x480 = 1,
    PAGE_SIZE_768x1024,
    PAGE_SIZE_800x480,
    PAGE_SIZE_800x600,
    PAGE_SIZE_1024x600,
    PAGE_SIZE_1024x768,
    PAGE_SIZE_1280x800,
    PAGE_SIZE_1280x1024,
    PAGE_SIZE_1366x768,
    PAGE_SIZE_1440x900,
    PAGE_SIZE_1600x900,
    PAGE_SIZE_1600x1200,
    PAGE_SIZE_1680x1050,
    PAGE_SIZE_1920x1080,
    PAGE_SIZE_1920x1200,
    PAGE_SIZE_2560x1440,
    PAGE_SIZE_2560x1600
};

// ─────────────────────────────────────────────────────────────────────────────
// WindowWidget: an overlay window (toolbar, dialog) rendered as a true
// Qt child widget of TermWidget.  Qt handles compositing, hit-testing,
// and event routing automatically — no manual coordinate math required.
// ─────────────────────────────────────────────────────────────────────────────
class WindowWidget : public QWidget {
    Q_OBJECT
public:
    explicit WindowWidget(int id, int frame_flags,
                          const QString &title, QWidget *parent = nullptr);
    // Writable surface that receives draw commands routed via targetPixmap()
    QPixmap &editPixmap() { return pix_; }

signals:
    void buttonPressed(int win_id, int btn_id);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    static constexpr int kTitleH = 22;
    int     id_;
    int     frame_;
    QString title_;
    QPixmap pix_;
    bool    dragging_{false};
    QPoint  dragOffset_;
};

// ─────────────────────────────────────────────────────────────────────────────
// TermWidget: main Qt6 terminal display widget
// ─────────────────────────────────────────────────────────────────────────────
class TermWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TermWidget(int socket_fd, int is_local = 1, QWidget *parent = nullptr);
    ~TermWidget() override;

    // Call after show() — sends SrvTermInfo to server
    void start();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    bool event(QEvent *ev) override;

private slots:
    void onSocketReady();
    void onScreensaverTick();

private:
    // ── socket / charqueue ───────────────────────────────────────────────────
    int socket_fd_;
    QSocketNotifier *socket_notifier_{nullptr};

    // ── rendering ────────────────────────────────────────────────────────────
    QPixmap back_buffer_;
    int page_x_{0}, page_y_{0};
    int page_w_{1024}, page_h_{768};
    int frame_width_{2};
    int bg_texture_{IMAGE_SAND};
    int title_color_{0};
    int title_height_{0};

    // clip region (set by SETCLIP, cleared by UPDATEALL/UPDATEAREA)
    int clip_x_{0}, clip_y_{0}, clip_w_{0}, clip_h_{0};
    bool use_clip_{false};

    // ── resources ────────────────────────────────────────────────────────────
    std::array<QFont,    20>          fonts_{};
    std::array<QColor,   TEXT_COLORS> text_colors_{};
    std::array<QColor,   TEXT_COLORS> shadow_colors_{};
    std::array<QColor,   TEXT_COLORS> hilight_colors_{};
    std::array<QPixmap,  IMAGE_COUNT> textures_{};

    // edge colours for frame drawing
    QColor c_te_, c_be_, c_le_, c_re_;       // normal
    QColor c_lte_, c_lbe_, c_lle_, c_lre_;   // lit
    QColor c_dte_, c_dbe_, c_dle_, c_dre_;   // dark

    // ── viewport scaling (page → screen) ────────────────────────────────────
    float scale_x_{1.0f}, scale_y_{1.0f};
    int view_x_{0}, view_y_{0}, view_w_{0}, view_h_{0};

    // ── rendering settings ───────────────────────────────────────────────────
    bool use_embossed_{false};
    bool use_antialiasing_{true};
    bool use_drop_shadows_{false};
    int  shadow_offset_x_{2};
    int  shadow_offset_y_{2};
    int  shadow_blur_{1};

    // ── screensaver ──────────────────────────────────────────────────────────
    QTimer *screensaver_timer_{nullptr};
    int  blank_time_{0};       // seconds; 0 = disabled
    bool screen_blanked_{false};

    // ── mouse state ──────────────────────────────────────────────────────────
    int moves_count_{0};
    int last_x_{0}, last_y_{0};

    // ── rubber-band selection (SELECTUPDATE/SELECTOFF) ───────────────────────
    bool select_anchor_set_{false};
    int  select_ax_{0}, select_ay_{0};   // anchor corner (page coords)
    int  select_ex_{0}, select_ey_{0};   // current end corner (page coords)

    // ── edit cursor resize handles (EDITCURSOR) ───────────────────────────────
    bool  edit_cursor_visible_{false};
    QRect edit_cursor_rect_{};            // zone bounds in page coords

    // ── multi-window overlay support ─────────────────────────────────────────
    // Each overlay window is a true QWidget child; Qt handles compositing,
    // hit-testing, and drag.  targetPixmap() routes draw commands to the
    // correct WindowWidget::editPixmap() surface.
    QMap<int, WindowWidget*> windows_;
    int target_window_{0};  // 0 = main back_buffer_; non-0 = window id

    // ── list-dialog state (LISTSTART..LISTEND) ───────────────────────────────
    QStringList list_items_;
    int         list_layer_id_{0};
    QRect       list_rect_;

    // ── page-edit dialog data (received from EDITPAGE) ───────────────────────
    struct PageData {
        int  full_edit{0};
        int  size{0};
        int  page_type{0};
        char name[256]{};
        int  id{0};
        int  title_color{0};
        int  image{0};
        int  default_font{0};
        int  frame[3]{};
        int  texture[3]{};
        int  color[3]{};
        int  default_spacing{0};
        int  default_shadow{0};
        int  parent_id{0};
        int  index{0};
    };
    PageData pending_page_{};

    // ── global defaults dialog data (received from DEFPAGE) ──────────────────
    struct DefaultsData {
        int  default_font{0};
        int  default_shadow{0};
        int  default_spacing{0};
        int  frame[3]{};
        int  texture[3]{};
        int  color[3]{};
        int  default_image{0};
        int  default_title_color{0};
        int  default_size{0};
    };
    DefaultsData pending_defaults_{};

    // ── multi-zone dialog data (received from EDITMULTIZONE) ─────────────────
    struct MultiZoneData {
        int  full_edit{0};
        int  behave{-1};
        int  font{-1};
        int  frame1{-1}, tex1{-1}, color1{-1};
        int  frame2{-1}, tex2{-1}, color2{-1};
        int  shape{-1};
        int  shadow{-1};
    };
    MultiZoneData pending_multizone_{};

    // ── zone-edit dialog data (received from EDITZONE) ───────────────────────
    struct ZoneData {
        int  full_edit{0};
        int  ztype{0};
        char name[256]{};
        int  page_id{0};
        int  group_id{0};
        int  behave{0};
        int  confirm{0};
        char confirm_msg[256]{};
        int  font{0};
        int  states{1};
        int  frame[3]{};
        int  texture[3]{};
        int  color[3]{};
        int  image[3]{};
        int  shape{0};
        int  shadow{0};
        int  key{0};
        char expression[256]{};
        char message[512]{};
        char filename[256]{};
        char image_filename[256]{};
        int  tender_type{0};
        char tender_amount[64]{};
        int  report_type{0};
        int  check_disp_num{0};
        int  video_target{0};
        int  report_print{0};
        char script[256]{};          // "page_list" / Script
        int  spacing{0};             // Flt stored as int*100
        int  qualifier{0};
        int  amount{0};
        int  switch_type{0};
        int  jtype{0};
        int  jump_id{0};
        int  customer_type{0};
        int  drawer_zone_type{0};
        char item_name[256]{};
        char item_print_name[256]{};
        char item_zone_name[256]{};
        int  itype{0};
        char item_location[256]{};
        char item_event_time[256]{};
        char item_total_tickets[64]{};
        char item_available_tickets[64]{};
        char item_price_label[64]{};
        char item_price[64]{};
        char item_subprice[64]{};
        char item_employee_price[64]{};
        int  item_family{0};
        int  item_sales{0};
        int  item_printer{0};
        int  item_order{0};
    };
    ZoneData pending_zone_{};
    void showZoneDialog();
    void sendZoneData(const ZoneData &d);
    void showPageDialog();
    void sendPageData(const PageData &d);
    void showDefaultsDialog();
    void sendDefaultsData(const DefaultsData &d);
    void showMultiZoneDialog();
    void sendMultiZoneData(const MultiZoneData &d);
    void showListDialog();

    // ── initialisation ───────────────────────────────────────────────────────
    void initFonts();
    void initColors();
    void initTextures();
    void initEdgeColors();

    // ── resource accessors ───────────────────────────────────────────────────
    QColor   textColor(int id)    const noexcept;
    QColor   shadowColor(int id)  const noexcept;
    QColor   hilightColor(int id) const noexcept;
    QFont    getFont(int id)      const noexcept;
    QBrush   textureBrush(int id) const noexcept;
    QPixmap  &targetPixmap(int layer_id = 0) noexcept;

    // ── server communication ─────────────────────────────────────────────────
    void sendToServer();
    void sendTermInfo();
    void sendTouch(int x, int y);
    void sendKey(genericChar ch, int xkeysym, int state);
    void sendMouse(int button_code, int x, int y);
    void sendButtonPress(int layer_id, int button_id);
    void sendError(const char *msg);
    void sendShutdown();

    // ── command dispatch ─────────────────────────────────────────────────────
    void processBuffer();
    void dispatchCommand(int code);

    // ── drawing commands ─────────────────────────────────────────────────────
    void cmdUpdateAll();
    void cmdUpdateArea();
    void cmdPushButton();
    void cmdSetClip();
    void cmdBlankPage();
    void cmdBackground();
    void cmdTitleBar();
    void cmdZone();
    void cmdText(int align);
    void cmdZoneText(int align);
    void cmdShadow();
    void cmdRectangle();
    void cmdSolidRectangle();
    void cmdPixmap();
    void cmdHLine();
    void cmdVLine();
    void cmdFrame();
    void cmdFilledFrame();
    void cmdStatusBar();
    void cmdEditCursor();
    void cmdCursor();
    void cmdNewWindow();
    void cmdShowWindow();
    void cmdKillWindow();
    void cmdTargetWindow();

    // ── drawing primitives (QPainter-based) ──────────────────────────────────
    void drawText(QPainter &p, const QString &text,
                  int x, int y, int color, int font_id, int align,
                  int max_w = 0);
    void drawZoneText(QPainter &p, const QString &text,
                      int x, int y, int w, int h,
                      int color, int font_id, int align);
    void drawFrame(QPainter &p, int x, int y, int w, int h,
                   int thick, int flags);
    void drawFilledFrame(QPainter &p, int x, int y, int w, int h,
                         int fw, int texture, int flags);
    void drawShadow(QPainter &p, int x, int y, int w, int h, int depth);
    void fillTexture(QPainter &p, int x, int y, int w, int h, int texture);

    // ── helpers ──────────────────────────────────────────────────────────────
    void handleDisconnect();
    void resetScreensaver();
};
