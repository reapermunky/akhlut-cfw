/**
 * ui.c — UI Widget Implementation
 */

#include "ui.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

/* ──────────────────────────────────────────────────────────
 * Icons — 8x8 1-bit bitmaps (MSB-first per row)
 * Designed for readability at 8px. Minimal, geometric.
 * ────────────────────────────────────────────────────────── */
const gfx_icon_t icon_radio = {{ // Radio waves
    0x00, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x00
}};

const gfx_icon_t icon_wifi = {{ // WiFi arcs
    0x00, 0x7E, 0x42, 0x3C, 0x24, 0x18, 0x18, 0x00
}};

const gfx_icon_t icon_bluetooth = {{ // BT rune
    0x08, 0x4A, 0x2C, 0x18, 0x18, 0x2C, 0x4A, 0x08
}};

const gfx_icon_t icon_bus = {{ // Bus/data lines
    0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF
}};

const gfx_icon_t icon_gpio = {{ // Pin header
    0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA
}};

const gfx_icon_t icon_ir = {{ // IR beam
    0x18, 0x18, 0x24, 0x42, 0x81, 0x00, 0x81, 0x42
}};

const gfx_icon_t icon_settings = {{ // Gear
    0x18, 0x7E, 0x5A, 0xDB, 0xDB, 0x5A, 0x7E, 0x18
}};

const gfx_icon_t icon_info = {{ // i in circle
    0x3C, 0x42, 0x5A, 0x42, 0x5A, 0x5A, 0x42, 0x3C
}};

const gfx_icon_t icon_battery = {{ // Battery outline
    0x7E, 0x42, 0xC3, 0xC3, 0xC3, 0xC3, 0x42, 0x7E
}};

const gfx_icon_t icon_usb = {{ // USB trident
    0x18, 0x18, 0x5A, 0x3C, 0x18, 0x18, 0x18, 0x18
}};

const gfx_icon_t icon_capture = {{ // Solid dot
    0x00, 0x18, 0x3C, 0x7E, 0x7E, 0x3C, 0x18, 0x00
}};

const gfx_icon_t icon_arrow_r = {{ // Right chevron
    0x00, 0x08, 0x0C, 0x0E, 0x0E, 0x0C, 0x08, 0x00
}};

static const gfx_icon_t *icon_table[] = {
    NULL,               // 0 = no icon
    &icon_radio,        // 1
    &icon_wifi,         // 2
    &icon_bluetooth,    // 3
    &icon_bus,          // 4
    &icon_gpio,         // 5
    &icon_ir,           // 6
    &icon_settings,     // 7
    &icon_info,         // 8
};

const gfx_icon_t *ui_get_icon(uint8_t id) {
    if (id >= sizeof(icon_table) / sizeof(icon_table[0])) return NULL;
    return icon_table[id];
}

/* ──────────────────────────────────────────────────────────
 * Menu Widget
 * ────────────────────────────────────────────────────────── */
void ui_menu_init(ui_menu_t *menu) {
    memset(menu, 0, sizeof(*menu));
    menu->dirty = true;
}

void ui_menu_add(ui_menu_t *menu, const char *name, uint8_t icon_id,
                 bool disabled, bool has_submenu) {
    if (menu->item_count >= MENU_MAX_ITEMS) return;

    ui_menu_item_t *item = &menu->items[menu->item_count];
    strncpy(item->name, name, MENU_MAX_NAME_LEN - 1);
    item->name[MENU_MAX_NAME_LEN - 1] = '\0';
    item->icon_id = icon_id;
    item->disabled = disabled;
    item->has_submenu = has_submenu;
    menu->item_count++;
    menu->dirty = true;
}

void ui_menu_clear(ui_menu_t *menu) {
    menu->item_count = 0;
    menu->selected = 0;
    menu->scroll_offset = 0;
    menu->dirty = true;
}

void ui_menu_up(ui_menu_t *menu) {
    if (menu->item_count == 0) return;

    if (menu->selected > 0) {
        menu->selected--;
    } else {
        menu->selected = menu->item_count - 1; // Wrap to bottom
    }

    // Adjust scroll
    if (menu->selected < menu->scroll_offset) {
        menu->scroll_offset = menu->selected;
    }
    if (menu->selected >= menu->scroll_offset + MENU_VISIBLE_ITEMS) {
        menu->scroll_offset = menu->selected - MENU_VISIBLE_ITEMS + 1;
    }

    menu->dirty = true;
}

void ui_menu_down(ui_menu_t *menu) {
    if (menu->item_count == 0) return;

    if (menu->selected < menu->item_count - 1) {
        menu->selected++;
    } else {
        menu->selected = 0; // Wrap to top
    }

    // Adjust scroll
    if (menu->selected >= menu->scroll_offset + MENU_VISIBLE_ITEMS) {
        menu->scroll_offset = menu->selected - MENU_VISIBLE_ITEMS + 1;
    }
    if (menu->selected < menu->scroll_offset) {
        menu->scroll_offset = menu->selected;
    }

    menu->dirty = true;
}

uint8_t ui_menu_selected(ui_menu_t *menu) {
    return menu->selected;
}

void ui_menu_draw(ui_menu_t *menu) {
    if (!menu->dirty) return;

    // Clear content area
    gfx_fill_rect(0, CONTENT_Y, CONTENT_WIDTH, CONTENT_HEIGHT, COL_BG);

    uint8_t visible_count = menu->item_count - menu->scroll_offset;
    if (visible_count > MENU_VISIBLE_ITEMS) visible_count = MENU_VISIBLE_ITEMS;

    for (uint8_t i = 0; i < visible_count; i++) {
        uint8_t idx = menu->scroll_offset + i;
        ui_menu_item_t *item = &menu->items[idx];
        uint16_t y = CONTENT_Y + (i * MENU_ITEM_HEIGHT);
        bool selected = (idx == menu->selected);

        // Selection highlight bar
        uint16_t bg = selected ? COL_HL_BG : COL_BG;
        uint16_t fg = item->disabled ? COL_DISABLED :
                      (selected ? COL_HIGHLIGHT : COL_TEXT);

        gfx_fill_rect(0, y, CONTENT_WIDTH, MENU_ITEM_HEIGHT, bg);

        // Selection indicator — left edge bar
        if (selected) {
            gfx_fill_rect(0, y + 2, 2, MENU_ITEM_HEIGHT - 4, COL_HIGHLIGHT);
        }

        // Icon
        const gfx_icon_t *icon = ui_get_icon(item->icon_id);
        if (icon) {
            uint16_t icon_fg = item->disabled ? COL_DISABLED :
                               (selected ? COL_HIGHLIGHT : COL_ACCENT);
            gfx_draw_icon(MENU_ICON_PAD + 4, y + (MENU_ITEM_HEIGHT - 8) / 2,
                          icon, icon_fg, bg);
        }

        // Item name
        gfx_draw_str_trunc(MENU_TEXT_X + 4, y + (MENU_ITEM_HEIGHT - 10) / 2,
                           item->name, FONT_MAIN, fg, bg,
                           CONTENT_WIDTH - MENU_TEXT_X - 16);

        // Submenu arrow
        if (item->has_submenu && !item->disabled) {
            gfx_draw_icon(CONTENT_WIDTH - 12, y + (MENU_ITEM_HEIGHT - 8) / 2,
                          &icon_arrow_r,
                          selected ? COL_HIGHLIGHT : COL_DIM, bg);
        }

        // Bottom divider (subtle)
        if (i < visible_count - 1) {
            gfx_hline(MENU_TEXT_X, y + MENU_ITEM_HEIGHT - 1,
                      CONTENT_WIDTH - MENU_TEXT_X - 4, COL_DIVIDER);
        }
    }

    // Scroll indicator (right edge)
    if (menu->item_count > MENU_VISIBLE_ITEMS) {
        uint16_t track_h = CONTENT_HEIGHT;
        uint16_t thumb_h = (track_h * MENU_VISIBLE_ITEMS) / menu->item_count;
        if (thumb_h < 8) thumb_h = 8;
        uint16_t thumb_y = CONTENT_Y +
            ((track_h - thumb_h) * menu->scroll_offset) /
            (menu->item_count - MENU_VISIBLE_ITEMS);

        // Track
        gfx_fill_rect(CONTENT_WIDTH - 2, CONTENT_Y, 2, CONTENT_HEIGHT, COL_DIVIDER);
        // Thumb
        gfx_fill_rect(CONTENT_WIDTH - 2, thumb_y, 2, thumb_h, COL_DIM);
    }

    menu->dirty = false;
}

/* ──────────────────────────────────────────────────────────
 * Status Bar
 * ────────────────────────────────────────────────────────── */
void ui_status_bar_init(ui_status_bar_t *sb) {
    memset(sb, 0, sizeof(*sb));
    strncpy(sb->tool_name, "Home", sizeof(sb->tool_name));
    sb->dirty = true;
}

void ui_status_bar_draw(ui_status_bar_t *sb) {
    if (!sb->dirty) return;

    // Background
    gfx_fill_rect(0, 0, TFT_WIDTH, STATUS_BAR_HEIGHT, COL_STATUS_BG);

    // Tool name (left)
    gfx_draw_str(4, 4, sb->tool_name, FONT_SMALL, COL_ACCENT, COL_STATUS_BG);

    // Right-side indicators (right to left)
    uint16_t rx = TFT_WIDTH - 4;

    // Battery percentage
    char batt_str[8];
    snprintf(batt_str, sizeof(batt_str), "%d%%", sb->battery_pct);
    rx -= gfx_str_width(batt_str, FONT_SMALL);
    gfx_draw_str(rx, 4, batt_str, FONT_SMALL,
                 sb->battery_pct < 20 ? COL_ERROR :
                 sb->battery_pct < 50 ? COL_WARN : COL_DIM,
                 COL_STATUS_BG);
    rx -= 4;

    // Charging indicator
    if (sb->charging) {
        rx -= 5;
        gfx_draw_char(rx, 4, '+', FONT_SMALL, COL_ACTIVE, COL_STATUS_BG);
        rx -= 2;
    }

    // Capture indicator (red dot)
    if (sb->capturing) {
        rx -= 10;
        gfx_draw_icon(rx, 4, &icon_capture, COL_ERROR, COL_STATUS_BG);
        rx -= 2;
    }

    // Orca
    if (sb->orca_ok) {
        rx -= 5;
        gfx_draw_char(rx, 4, 'W', FONT_SMALL, COL_ACTIVE, COL_STATUS_BG);
        rx -= 2;
    }

    // Radio indicators
    if (sb->radio2_ok) {
        rx -= 10;
        gfx_draw_str(rx, 4, "R2", FONT_SMALL, COL_ACTIVE, COL_STATUS_BG);
        rx -= 2;
    }
    if (sb->radio1_ok) {
        rx -= 10;
        gfx_draw_str(rx, 4, "R1", FONT_SMALL, COL_ACTIVE, COL_STATUS_BG);
        rx -= 2;
    }

    // USB
    if (sb->usb_connected) {
        rx -= 10;
        gfx_draw_icon(rx, 4, &icon_usb, COL_DIM, COL_STATUS_BG);
    }

    // Bottom divider
    gfx_hline(0, STATUS_BAR_HEIGHT - 1, TFT_WIDTH, COL_DIVIDER);

    sb->dirty = false;
}

/* ──────────────────────────────────────────────────────────
 * Hint Bar
 * ────────────────────────────────────────────────────────── */
void ui_hint_bar_init(ui_hint_bar_t *hb) {
    memset(hb, 0, sizeof(*hb));
    hb->show_nav = true;
    hb->show_select = true;
    hb->show_back = true;
    hb->dirty = true;
}

void ui_hint_bar_draw(ui_hint_bar_t *hb) {
    if (!hb->dirty) return;

    uint16_t y = TFT_HEIGHT - HINT_BAR_HEIGHT;

    // Background
    gfx_fill_rect(0, y, TFT_WIDTH, HINT_BAR_HEIGHT, COL_STATUS_BG);

    // Top divider
    gfx_hline(0, y, TFT_WIDTH, COL_DIVIDER);

    uint16_t x = 4;
    uint16_t text_y = y + 4;

    if (hb->show_nav) {
        // Yellow/Blue = Up/Down
        gfx_draw_char(x, text_y, '^', FONT_SMALL, COL_WARN, COL_STATUS_BG);
        x += 5;
        gfx_draw_char(x, text_y, 'v', FONT_SMALL, COL_HIGHLIGHT, COL_STATUS_BG);
        x += 7;
        x += gfx_draw_str(x, text_y, "Nav", FONT_SMALL, COL_DIM, COL_STATUS_BG);
        x += 8;
    }

    if (hb->show_select) {
        gfx_draw_char(x, text_y, '*', FONT_SMALL, COL_ACTIVE, COL_STATUS_BG);
        x += 7;
        x += gfx_draw_str(x, text_y, "Sel", FONT_SMALL, COL_DIM, COL_STATUS_BG);
        x += 8;
    }

    if (hb->show_back) {
        gfx_draw_char(x, text_y, '<', FONT_SMALL, COL_ERROR, COL_STATUS_BG);
        x += 7;
        x += gfx_draw_str(x, text_y, "Back", FONT_SMALL, COL_DIM, COL_STATUS_BG);
        x += 8;
    }

    if (hb->gray_action[0]) {
        // Right-align gray action
        char buf[24];
        snprintf(buf, sizeof(buf), "o %s", hb->gray_action);
        gfx_draw_str_right(TFT_WIDTH - 4, text_y, buf,
                           FONT_SMALL, COL_DIM, COL_STATUS_BG);
    }

    hb->dirty = false;
}

/* ──────────────────────────────────────────────────────────
 * Toast
 * ────────────────────────────────────────────────────────── */
void ui_toast_show(ui_toast_t *toast, const char *text, uint16_t duration_ms) {
    strncpy(toast->text, text, sizeof(toast->text) - 1);
    toast->text[sizeof(toast->text) - 1] = '\0';
    toast->show_until_ms = to_ms_since_boot(get_absolute_time()) + duration_ms;
    toast->active = true;
}

void ui_toast_draw(ui_toast_t *toast, uint32_t now_ms) {
    if (!toast->active) return;

    if (now_ms >= toast->show_until_ms) {
        toast->active = false;
        return; // Caller should redraw underlying content
    }

    // Centered overlay box
    uint16_t text_w = gfx_str_width(toast->text, FONT_MAIN);
    uint16_t box_w = text_w + 24;
    uint16_t box_h = 28;
    uint16_t box_x = (TFT_WIDTH - box_w) / 2;
    uint16_t box_y = (TFT_HEIGHT - box_h) / 2;

    // Dark background with border
    gfx_fill_rect(box_x, box_y, box_w, box_h, COL_STATUS_BG);
    gfx_draw_rect(box_x, box_y, box_w, box_h, COL_HIGHLIGHT);

    // Text
    gfx_draw_str(box_x + 12, box_y + 9, toast->text,
                 FONT_MAIN, COL_TEXT, COL_STATUS_BG);
}

bool ui_toast_expired(ui_toast_t *toast, uint32_t now_ms) {
    return toast->active && now_ms >= toast->show_until_ms;
}

/* ──────────────────────────────────────────────────────────
 * Text Screen
 * ────────────────────────────────────────────────────────── */
void ui_text_init(ui_text_screen_t *ts) {
    memset(ts, 0, sizeof(*ts));
    ts->auto_scroll = true;
    ts->dirty = true;
}

void ui_text_add_line(ui_text_screen_t *ts, const char *line) {
    if (ts->line_count < TEXT_MAX_LINES) {
        strncpy(ts->lines[ts->line_count], line, TEXT_MAX_LINE_LEN);
        ts->lines[ts->line_count][TEXT_MAX_LINE_LEN] = '\0';
        ts->line_count++;
    } else {
        // Shift up
        memmove(&ts->lines[0], &ts->lines[1],
                (TEXT_MAX_LINES - 1) * (TEXT_MAX_LINE_LEN + 1));
        strncpy(ts->lines[TEXT_MAX_LINES - 1], line, TEXT_MAX_LINE_LEN);
        ts->lines[TEXT_MAX_LINES - 1][TEXT_MAX_LINE_LEN] = '\0';
    }

    if (ts->auto_scroll) {
        uint8_t visible = CONTENT_HEIGHT / 10; // font_main height
        if (ts->line_count > visible) {
            ts->scroll_offset = ts->line_count - visible;
        }
    }
    ts->dirty = true;
}

void ui_text_clear(ui_text_screen_t *ts) {
    ts->line_count = 0;
    ts->scroll_offset = 0;
    ts->dirty = true;
}

void ui_text_scroll_up(ui_text_screen_t *ts) {
    if (ts->scroll_offset > 0) {
        ts->scroll_offset--;
        ts->auto_scroll = false;
        ts->dirty = true;
    }
}

void ui_text_scroll_down(ui_text_screen_t *ts) {
    uint8_t visible = CONTENT_HEIGHT / 10;
    if (ts->scroll_offset + visible < ts->line_count) {
        ts->scroll_offset++;
        ts->dirty = true;
    }
    if (ts->scroll_offset + visible >= ts->line_count) {
        ts->auto_scroll = true;
    }
}

void ui_text_draw(ui_text_screen_t *ts) {
    if (!ts->dirty) return;

    gfx_fill_rect(0, CONTENT_Y, CONTENT_WIDTH, CONTENT_HEIGHT, COL_BG);

    uint8_t visible = CONTENT_HEIGHT / 10;
    uint16_t y = CONTENT_Y;

    for (uint8_t i = 0; i < visible && (ts->scroll_offset + i) < ts->line_count; i++) {
        gfx_draw_str(4, y, ts->lines[ts->scroll_offset + i],
                     FONT_MAIN, COL_TEXT, COL_BG);
        y += 10;
    }

    ts->dirty = false;
}

/* ──────────────────────────────────────────────────────────
 * Dialog
 * ────────────────────────────────────────────────────────── */
void ui_dialog_show(ui_dialog_t *dlg, const char *title, const char *text,
                     const char **options, uint8_t count) {
    strncpy(dlg->title, title, sizeof(dlg->title) - 1);
    strncpy(dlg->text, text, sizeof(dlg->text) - 1);
    dlg->option_count = count > DIALOG_MAX_OPTIONS ? DIALOG_MAX_OPTIONS : count;
    for (uint8_t i = 0; i < dlg->option_count; i++) {
        strncpy(dlg->options[i], options[i], 15);
        dlg->options[i][15] = '\0';
    }
    dlg->selected = 0;
    dlg->active = true;
}

void ui_dialog_left(ui_dialog_t *dlg) {
    if (dlg->selected > 0) dlg->selected--;
}

void ui_dialog_right(ui_dialog_t *dlg) {
    if (dlg->selected < dlg->option_count - 1) dlg->selected++;
}

uint8_t ui_dialog_selected(ui_dialog_t *dlg) {
    return dlg->selected;
}

void ui_dialog_draw(ui_dialog_t *dlg) {
    if (!dlg->active) return;

    uint16_t box_w = 260;
    uint16_t box_h = 100;
    uint16_t box_x = (TFT_WIDTH - box_w) / 2;
    uint16_t box_y = (TFT_HEIGHT - box_h) / 2;

    // Background + border
    gfx_fill_rect(box_x, box_y, box_w, box_h, COL_STATUS_BG);
    gfx_draw_rect(box_x, box_y, box_w, box_h, COL_HIGHLIGHT);

    // Title
    gfx_draw_str(box_x + 8, box_y + 8, dlg->title,
                 FONT_MAIN, COL_HIGHLIGHT, COL_STATUS_BG);

    // Divider under title
    gfx_hline(box_x + 8, box_y + 22, box_w - 16, COL_DIVIDER);

    // Text
    gfx_draw_str(box_x + 8, box_y + 30, dlg->text,
                 FONT_MAIN, COL_TEXT, COL_STATUS_BG);

    // Option buttons at bottom
    uint16_t btn_y = box_y + box_h - 28;
    uint16_t total_w = 0;
    for (uint8_t i = 0; i < dlg->option_count; i++) {
        total_w += gfx_str_width(dlg->options[i], FONT_MAIN) + 16;
    }
    total_w += (dlg->option_count - 1) * 8; // gaps

    uint16_t bx = box_x + (box_w - total_w) / 2;
    for (uint8_t i = 0; i < dlg->option_count; i++) {
        uint16_t bw = gfx_str_width(dlg->options[i], FONT_MAIN) + 16;
        bool sel = (i == dlg->selected);

        if (sel) {
            gfx_fill_rect(bx, btn_y, bw, 20, COL_HL_BG);
            gfx_draw_rect(bx, btn_y, bw, 20, COL_HIGHLIGHT);
        } else {
            gfx_fill_rect(bx, btn_y, bw, 20, COL_STATUS_BG);
            gfx_draw_rect(bx, btn_y, bw, 20, COL_DIVIDER);
        }

        gfx_draw_str(bx + 8, btn_y + 5, dlg->options[i],
                     FONT_MAIN, sel ? COL_HIGHLIGHT : COL_DIM, 
                     sel ? COL_HL_BG : COL_STATUS_BG);

        bx += bw + 8;
    }
}
