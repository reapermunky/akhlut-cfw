/**
 * ui.h — UI Widget System
 *
 * High-level components built on the gfx primitives.
 * Menu, StatusBar, HintBar, Toast, Dialog, TextScreen.
 *
 * Design language:
 *   - Dark bg, cyan highlights, no decoration
 *   - Information-dense, no wasted pixels
 *   - Every pixel earns its place
 */

#ifndef UI_H
#define UI_H

#include "gfx.h"
#include "ipp_defs.h"

/* ──────────────────────────────────────────────────────────
 * Menu Widget
 *
 * Scrollable list with optional icons. Highlight bar tracks
 * the selected item. Handles wrapping at top/bottom.
 *
 * Layout per item (CONTENT_WIDTH × 20px):
 *   [2px pad][8px icon][4px gap][text...][right-arrow if submenu]
 * ────────────────────────────────────────────────────────── */
#define MENU_ITEM_HEIGHT    20
#define MENU_ICON_SIZE      8
#define MENU_ICON_PAD       2
#define MENU_TEXT_X         (MENU_ICON_PAD + MENU_ICON_SIZE + 4)
#define MENU_MAX_ITEMS      32
#define MENU_MAX_NAME_LEN   32
#define MENU_VISIBLE_ITEMS  (CONTENT_HEIGHT / MENU_ITEM_HEIGHT) // 10 items visible

typedef struct {
    char            name[MENU_MAX_NAME_LEN];
    uint8_t         icon_id;
    bool            disabled;
    bool            has_submenu;
} ui_menu_item_t;

typedef struct {
    ui_menu_item_t  items[MENU_MAX_ITEMS];
    uint8_t         item_count;
    uint8_t         selected;       // Currently highlighted item
    uint8_t         scroll_offset;  // First visible item index
    bool            dirty;          // Needs redraw
} ui_menu_t;

void ui_menu_init(ui_menu_t *menu);
void ui_menu_add(ui_menu_t *menu, const char *name, uint8_t icon_id,
                 bool disabled, bool has_submenu);
void ui_menu_clear(ui_menu_t *menu);
void ui_menu_up(ui_menu_t *menu);
void ui_menu_down(ui_menu_t *menu);
uint8_t ui_menu_selected(ui_menu_t *menu);
void ui_menu_draw(ui_menu_t *menu);

/* ──────────────────────────────────────────────────────────
 * Status Bar (top 16px)
 *
 * Layout:
 *   [tool_name                    R1 R2 BT ▮▮▮ 85%]
 * ────────────────────────────────────────────────────────── */
typedef struct {
    char    tool_name[24];
    uint8_t battery_pct;
    bool    charging;
    bool    radio1_ok;
    bool    radio2_ok;
    bool    orca_ok;
    bool    usb_connected;
    bool    capturing;      // Shows a red dot when actively capturing
    bool    dirty;
} ui_status_bar_t;

void ui_status_bar_init(ui_status_bar_t *sb);
void ui_status_bar_draw(ui_status_bar_t *sb);

/* ──────────────────────────────────────────────────────────
 * Hint Bar (bottom 16px)
 *
 * Shows the current Gray button action.
 * Layout:
 *   [▲▼ Navigate    ● Select    ◀ Back    ◆ Capture]
 * ────────────────────────────────────────────────────────── */
typedef struct {
    char    gray_action[16];    // What Gray does right now
    bool    show_nav;           // Show ▲▼ Navigate
    bool    show_select;        // Show ● Select
    bool    show_back;          // Show ◀ Back
    bool    dirty;
} ui_hint_bar_t;

void ui_hint_bar_init(ui_hint_bar_t *hb);
void ui_hint_bar_draw(ui_hint_bar_t *hb);

/* ──────────────────────────────────────────────────────────
 * Toast — Temporary notification overlay
 * ────────────────────────────────────────────────────────── */
typedef struct {
    char        text[48];
    uint32_t    show_until_ms;  // Absolute time to dismiss
    bool        active;
} ui_toast_t;

void ui_toast_show(ui_toast_t *toast, const char *text, uint16_t duration_ms);
void ui_toast_draw(ui_toast_t *toast, uint32_t now_ms);
bool ui_toast_expired(ui_toast_t *toast, uint32_t now_ms);

/* ──────────────────────────────────────────────────────────
 * Text Screen — Scrollable text log
 * ────────────────────────────────────────────────────────── */
#define TEXT_MAX_LINES  64
#define TEXT_MAX_LINE_LEN 53  // 320px / 6px per char

typedef struct {
    char        lines[TEXT_MAX_LINES][TEXT_MAX_LINE_LEN + 1];
    uint16_t    line_count;
    uint16_t    scroll_offset;
    bool        auto_scroll;
    bool        dirty;
} ui_text_screen_t;

void ui_text_init(ui_text_screen_t *ts);
void ui_text_add_line(ui_text_screen_t *ts, const char *line);
void ui_text_clear(ui_text_screen_t *ts);
void ui_text_scroll_up(ui_text_screen_t *ts);
void ui_text_scroll_down(ui_text_screen_t *ts);
void ui_text_draw(ui_text_screen_t *ts);

/* ──────────────────────────────────────────────────────────
 * Dialog — Modal confirmation
 * ────────────────────────────────────────────────────────── */
#define DIALOG_MAX_OPTIONS 3

typedef struct {
    char        title[24];
    char        text[64];
    char        options[DIALOG_MAX_OPTIONS][16];
    uint8_t     option_count;
    uint8_t     selected;
    bool        active;
} ui_dialog_t;

void ui_dialog_show(ui_dialog_t *dlg, const char *title, const char *text,
                     const char **options, uint8_t count);
void ui_dialog_left(ui_dialog_t *dlg);
void ui_dialog_right(ui_dialog_t *dlg);
uint8_t ui_dialog_selected(ui_dialog_t *dlg);
void ui_dialog_draw(ui_dialog_t *dlg);

/* ──────────────────────────────────────────────────────────
 * Built-in Icons (8x8 bitmaps)
 * ────────────────────────────────────────────────────────── */
extern const gfx_icon_t icon_radio;     // Sub-GHz
extern const gfx_icon_t icon_wifi;      // WiFi
extern const gfx_icon_t icon_bluetooth; // BLE
extern const gfx_icon_t icon_bus;       // Bus analysis
extern const gfx_icon_t icon_gpio;      // GPIO
extern const gfx_icon_t icon_ir;        // IR remote
extern const gfx_icon_t icon_settings;  // Gear
extern const gfx_icon_t icon_info;      // About/info
extern const gfx_icon_t icon_battery;   // Battery
extern const gfx_icon_t icon_usb;       // USB connected
extern const gfx_icon_t icon_capture;   // Recording dot
extern const gfx_icon_t icon_arrow_r;   // Submenu indicator

// Get icon by ID (matches menu item icon_id)
const gfx_icon_t *ui_get_icon(uint8_t id);

#endif // UI_H
