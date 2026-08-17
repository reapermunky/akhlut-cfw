/**
 * tool_ghost.c — GhostESP Controller
 *
 * Akhlut CFW
 *
 * Full control of GhostESP through the UART header.
 * Category menu → command menu → live output viewer.
 * WiFi scan results are parsed and displayed as a compact table.
 * All other output shown as a scrollable text terminal.
 */

#include "tool.h"
#include "board.h"
#include "ipp_defs.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Modes ────────────────────────────────────────────── */
#define MODE_CATEGORIES   0
#define MODE_COMMANDS     1
#define MODE_OUTPUT       2
#define MODE_AP_TABLE     3

/* ── View types (inside MODE_OUTPUT) ──────────────────── */
#define VIEW_GENERIC      0
#define VIEW_SCANNING     1

/* ── Limits ───────────────────────────────────────────── */
#define MAX_APS           24
#define GEN_LINES         20
#define GEN_LINE_LEN      42
#define LINE_BUF_SIZE     160

/* ── Command flags ────────────────────────────────────── */
#define CMD_SIMPLE        0
#define CMD_STAY_ACTIVE   1
#define CMD_WIFI_SCAN     2

typedef struct {
    const char *label;
    const char *cmd;
    uint8_t     flags;
} ghost_cmd_t;

typedef struct {
    const char *name;
    const ghost_cmd_t *cmds;
    uint8_t count;
} ghost_cat_t;

typedef struct {
    char    ssid[18];
    int8_t  rssi;
    uint8_t channel;
    char    security[6];
} compact_ap_t;

/* ── Command Tables ───────────────────────────────────── */

static const ghost_cmd_t wifi_cmds[] = {
    {"Scan APs",          "scanap",      CMD_WIFI_SCAN},
    {"Deauth",            "attack -d",   CMD_STAY_ACTIVE},
    {"Capture Handshake", "attack -hsd", CMD_STAY_ACTIVE},
    {"Channel Switch",    "attack -c",   CMD_STAY_ACTIVE},
    {"EAPOL Logoff",      "attack -e",   CMD_STAY_ACTIVE},
    {"Scan Stations",     "scansta",     CMD_STAY_ACTIVE},
    {"Scan All",          "scanall",     CMD_STAY_ACTIVE},
    {"WPA3 Check",        "wpa3check",   CMD_STAY_ACTIVE},
    {"Congestion",        "congestion",  CMD_STAY_ACTIVE},
};

static const ghost_cmd_t beacon_cmds[] = {
    {"Random Spam",     "beaconspam -r",    CMD_STAY_ACTIVE},
    {"Rickroll Spam",   "beaconspam -rr",   CMD_STAY_ACTIVE},
    {"AP List Spam",    "beaconspamlist",    CMD_STAY_ACTIVE},
    {"Show List",       "beaconshow",       CMD_STAY_ACTIVE},
    {"Clear List",      "beaconclear",      CMD_SIMPLE},
};

static const ghost_cmd_t ble_cmds[] = {
    {"Apple Spam",      "blespam -apple",   CMD_STAY_ACTIVE},
    {"Google Spam",     "blespam -google",  CMD_STAY_ACTIVE},
    {"Samsung Spam",    "blespam -samsung", CMD_STAY_ACTIVE},
    {"Microsoft Spam",  "blespam -ms",      CMD_STAY_ACTIVE},
    {"Random Spam",     "blespam -random",  CMD_STAY_ACTIVE},
    {"BLE Scan",        "blescan",          CMD_STAY_ACTIVE},
    {"Find Flippers",   "blescan -f",       CMD_STAY_ACTIVE},
    {"AirTag Scanner",  "blescan -a",       CMD_STAY_ACTIVE},
    {"Spam Detector",   "blescan -ds",      CMD_STAY_ACTIVE},
    {"GATT Scan",       "blescan -g",       CMD_STAY_ACTIVE},
};

static const ghost_cmd_t attack_cmds[] = {
    {"SAE Flood",       "attack -s",          CMD_STAY_ACTIVE},
    {"DHCP Starve",     "dhcpstarve start",   CMD_STAY_ACTIVE},
    {"DHCP Status",     "dhcpstarve display", CMD_STAY_ACTIVE},
};

static const ghost_cmd_t capture_cmds[] = {
    {"Wardrive",    "wdstream start -wifi -ble", CMD_STAY_ACTIVE},
    {"Env Sweep",   "sweep",                     CMD_STAY_ACTIVE},
};

static const ghost_cmd_t misc_cmds[] = {
    {"Help",            "help all",         CMD_STAY_ACTIVE},
    {"Status",          "status",           CMD_STAY_ACTIVE},
    {"List Stations",   "list -s",          CMD_STAY_ACTIVE},
    {"List AirTags",    "list -airtags",    CMD_STAY_ACTIVE},
    {"LED Rainbow",     "rgbmode rainbow",  CMD_SIMPLE},
    {"LED Police",      "rgbmode police",   CMD_SIMPLE},
    {"LED Strobe",      "rgbmode strobe",   CMD_SIMPLE},
    {"LED Off",         "rgbmode off",      CMD_SIMPLE},
};

#define ARRAY_LEN(a) (sizeof(a)/sizeof(a[0]))

static const ghost_cat_t categories[] = {
    {"WiFi",      wifi_cmds,    ARRAY_LEN(wifi_cmds)},
    {"Beacon",    beacon_cmds,  ARRAY_LEN(beacon_cmds)},
    {"BLE",       ble_cmds,     ARRAY_LEN(ble_cmds)},
    {"Attacks",   attack_cmds,  ARRAY_LEN(attack_cmds)},
    {"Capture",   capture_cmds, ARRAY_LEN(capture_cmds)},
    {"Misc",      misc_cmds,    ARRAY_LEN(misc_cmds)},
};
#define CAT_COUNT ARRAY_LEN(categories)

/* ── State ────────────────────────────────────────────── */
typedef struct {
    uint8_t  mode;
    uint8_t  cat_sel;
    uint8_t  cmd_sel;
    uint8_t  cmd_scroll;
    uint8_t  ap_cursor;
    uint8_t  view_mode;
    bool     stay_active;

    uint8_t  ap_count;
    int8_t   parse_idx;
    bool     scan_done;
    bool     got_prompt;

    uint8_t  out_count;
    uint8_t  out_scroll;

    char     line_buf[LINE_BUF_SIZE];
    uint16_t line_pos;

    uint32_t last_display_ms;
    uint32_t start_ms;
    bool     display_dirty;

    compact_ap_t aps[MAX_APS];
    char     gen_lines[GEN_LINES][GEN_LINE_LEN];
} ghost_state_t;

_Static_assert(sizeof(ghost_state_t) <= TOOL_STATE_POOL_SIZE,
               "ghost_state_t exceeds state pool");

/* ── ANSI stripping ───────────────────────────────────── */
static void strip_ansi(char *line) {
    char *rd = line, *wr = line;
    while (*rd) {
        if (*rd == '\x1b') {
            rd++;
            if (*rd == '[') {
                rd++;
                while (*rd && *rd != 'm' && *rd != 'K' && *rd != 'H'
                       && *rd != 'J' && *rd != 'A' && *rd != 'B'
                       && *rd != 'C' && *rd != 'D')
                    rd++;
                if (*rd) rd++;
            }
            continue;
        }
        *wr++ = *rd++;
    }
    *wr = '\0';
}

/* ── Generic output buffer ────────────────────────────── */
static void gen_add_line(ghost_state_t *s, const char *text) {
    if (s->out_count < GEN_LINES) {
        strncpy(s->gen_lines[s->out_count], text, GEN_LINE_LEN - 1);
        s->gen_lines[s->out_count][GEN_LINE_LEN - 1] = '\0';
        s->out_count++;
    } else {
        memmove(&s->gen_lines[0], &s->gen_lines[1],
                (GEN_LINES - 1) * GEN_LINE_LEN);
        strncpy(s->gen_lines[GEN_LINES - 1], text, GEN_LINE_LEN - 1);
        s->gen_lines[GEN_LINES - 1][GEN_LINE_LEN - 1] = '\0';
    }
    s->display_dirty = true;
}

/* ── WiFi scan parser ─────────────────────────────────── */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static void parse_scan_line(ghost_state_t *s, const char *line) {
    const char *p = skip_ws(line);

    if (p[0] == '[' && p[1] >= '0' && p[1] <= '9') {
        int idx = atoi(p + 1);
        const char *ssid_s = strstr(p, "SSID: ");
        if (ssid_s && idx < MAX_APS) {
            ssid_s += 6;
            if (idx >= s->ap_count) s->ap_count = idx + 1;
            s->parse_idx = idx;
            compact_ap_t *ap = &s->aps[idx];
            memset(ap, 0, sizeof(*ap));

            const char *end = ssid_s + strlen(ssid_s);
            while (end > ssid_s && (end[-1] == ',' || end[-1] == ' '))
                end--;
            size_t len = end - ssid_s;

            if (strncmp(ssid_s, "(Hidden)", 8) == 0) {
                memcpy(ap->ssid, "(Hidden)", 9);
            } else {
                if (len > 17) len = 17;
                memcpy(ap->ssid, ssid_s, len);
                ap->ssid[len] = '\0';
            }
            s->display_dirty = true;
        }
        return;
    }

    if (s->parse_idx >= 0 && s->parse_idx < MAX_APS) {
        compact_ap_t *ap = &s->aps[s->parse_idx];

        const char *r = strstr(p, "RSSI: ");
        if (r) { ap->rssi = (int8_t)atoi(r + 6); return; }

        const char *c = strstr(p, "Channel: ");
        if (c) { ap->channel = (uint8_t)atoi(c + 9); return; }

        const char *sec = strstr(p, "Security: ");
        if (sec) {
            sec += 10;
            size_t len = strlen(sec);
            while (len > 0 && (sec[len-1] == ',' || sec[len-1] == ' '))
                len--;
            if (len > 5) len = 5;
            memcpy(ap->security, sec, len);
            ap->security[len] = '\0';
            return;
        }
    }

    if (strncmp(p, "Found ", 6) == 0 && strstr(p, "access point")) {
        s->scan_done = true;
        s->display_dirty = true;
    }

    if (s->scan_done && strstr(p, "ghost>")) {
        s->got_prompt = true;
        s->display_dirty = true;
    }
}

/* ── Display helpers ──────────────────────────────────── */

static void show_categories(ghost_state_t *s) {
    const char *items[4];
    uint8_t icons[4];
    uint8_t page = (s->cat_sel / 4) * 4;
    uint8_t n = CAT_COUNT - page;
    if (n > 4) n = 4;
    for (uint8_t i = 0; i < n; i++) {
        items[i] = categories[page + i].name;
        icons[i] = 2;
    }
    tool_send_menu(s->cat_sel - page, items, icons, n);
}

static void show_commands(ghost_state_t *s) {
    const ghost_cat_t *cat = &categories[s->cat_sel];
    const char *items[4];
    uint8_t icons[4];
    uint8_t n = cat->count - s->cmd_scroll;
    if (n > 4) n = 4;
    for (uint8_t i = 0; i < n; i++) {
        items[i] = cat->cmds[s->cmd_scroll + i].label;
        icons[i] = 2;
    }
    tool_send_menu(s->cmd_sel - s->cmd_scroll, items, icons, n);
}

static void show_scanning(ghost_state_t *s) {
    char lb[6][42];
    const char *lp[6];
    int n = 0;
    lp[n++] = "";
    lp[n++] = "     WiFi AP Scan";
    lp[n++] = "";
    uint32_t elapsed = (to_ms_since_boot(get_absolute_time()) - s->start_ms) / 1000;
    snprintf(lb[3], 42, "  Scanning... %us  %u APs",
             (unsigned)elapsed, s->ap_count);
    lp[n++] = lb[3];
    lp[n++] = "";
    lp[n++] = "  [RED] Stop & show results";
    tool_send_text_screen(lp, (uint8_t)n);
}

static void show_ap_table(ghost_state_t *s) {
    char lb[14][36];
    const char *lp[14];
    int n = 0;

    snprintf(lb[0], 36, " %u APs  [GRN] Select", s->ap_count);
    lp[n++] = lb[0];

    if (s->ap_count == 0) {
        lp[n++] = "";
        lp[n++] = "  No access points found.";
        lp[n++] = "";
        lp[n++] = "  [RED] Back";
    } else {
        uint8_t per_page = 12;
        uint8_t start = s->out_scroll;
        uint8_t count = s->ap_count - start;
        if (count > per_page) count = per_page;

        for (uint8_t i = 0; i < count; i++) {
            uint8_t idx = start + i;
            compact_ap_t *ap = &s->aps[idx];
            const char *ssid = ap->ssid[0] ? ap->ssid : "?";
            char marker = (idx == s->ap_cursor) ? '>' : ' ';
            snprintf(lb[n], 36, "%c%2d %-12.12s %2d %3d %s",
                     marker, idx, ssid,
                     ap->channel, ap->rssi, ap->security);
            lp[n] = lb[n];
            n++;
        }

        if (start + count < s->ap_count) {
            snprintf(lb[n], 36, "   ... %d more",
                     s->ap_count - start - count);
            lp[n] = lb[n]; n++;
        }
    }

    tool_send_text_screen(lp, (uint8_t)n);
}

static void show_generic_output(ghost_state_t *s) {
    char lb[14][36];
    const char *lp[14];
    int n = 0;

    uint8_t start = s->out_scroll;
    uint8_t avail = s->out_count - start;
    if (avail > 13) avail = 13;

    for (uint8_t i = 0; i < avail; i++) {
        snprintf(lb[n], 36, "%s", s->gen_lines[start + i]);
        lp[n] = lb[n];
        n++;
    }

    while (n < 13) {
        lp[n++] = "";
    }
    lp[n++] = s->stay_active ? " [RED] Stop" : " [RED] Back";

    tool_send_text_screen(lp, (uint8_t)n);
}

/* ── Feed bytes ───────────────────────────────────────── */

static void process_line(ghost_state_t *s, char *line) {
    strip_ansi(line);
    const char *p = skip_ws(line);
    if (*p == '\0') return;

    if (s->view_mode == VIEW_SCANNING) {
        parse_scan_line(s, line);
    } else {
        if (strlen(p) > 34) {
            char trunc[36];
            memcpy(trunc, p, 32);
            trunc[32] = '.';
            trunc[33] = '.';
            trunc[34] = '\0';
            gen_add_line(s, trunc);
        } else {
            gen_add_line(s, p);
        }

        if (strstr(p, "ghost>") && strlen(p) < 10)
            s->got_prompt = true;
    }
}

static void feed_byte(ghost_state_t *s, uint8_t byte) {
    if (byte == '\r') return;
    if (byte == '\n') {
        s->line_buf[s->line_pos] = '\0';
        if (s->line_pos > 0)
            process_line(s, s->line_buf);
        s->line_pos = 0;
        return;
    }
    if (s->line_pos < LINE_BUF_SIZE - 1)
        s->line_buf[s->line_pos++] = byte;
}

/* ── Send command with flush ──────────────────────────── */
static void send_ghost_cmd(ghost_state_t *s, const char *cmd) {
    s->out_count = 0;
    s->out_scroll = 0;
    s->line_pos = 0;
    s->display_dirty = true;
    s->got_prompt = false;
    s->last_display_ms = to_ms_since_boot(get_absolute_time());

    tool_orca_raw_mode = true;
    tool_orca_raw_flush();
    tool_orca_raw_send("\r\n");
    sleep_ms(50);
    tool_orca_raw_flush();
    tool_orca_raw_send(cmd);
    tool_orca_raw_send("\r\n");
}

static void start_wifi_scan(ghost_state_t *s) {
    s->view_mode = VIEW_SCANNING;
    s->ap_count = 0;
    s->parse_idx = -1;
    s->scan_done = false;
    s->start_ms = to_ms_since_boot(get_absolute_time());
    memset(s->aps, 0, sizeof(s->aps));
    send_ghost_cmd(s, "scanap");
    s->mode = MODE_OUTPUT;
    s->stay_active = true;
    tool_send_status_bar("WiFi Scan");
    show_scanning(s);
}

/* ── Tool callbacks ───────────────────────────────────── */

static tool_result_t ghost_enter(tool_ctx_t *ctx) {
    ghost_state_t *s = (ghost_state_t *)ctx->state;
    memset(s, 0, sizeof(*s));
    s->mode = MODE_CATEGORIES;
    s->parse_idx = -1;
    tool_send_status_bar("GhostESP");
    show_categories(s);
    return TOOL_OK;
}

static tool_result_t ghost_update(tool_ctx_t *ctx) {
    ghost_state_t *s = (ghost_state_t *)ctx->state;

    if (s->mode != MODE_OUTPUT) return TOOL_OK;

    uint8_t buf[128];
    uint16_t n = tool_orca_raw_read(buf, sizeof(buf));
    for (uint16_t i = 0; i < n; i++)
        feed_byte(s, buf[i]);

    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (s->view_mode == VIEW_SCANNING) {
        if (s->got_prompt && s->scan_done) {
            s->mode = MODE_AP_TABLE;
            s->out_scroll = 0;
            s->ap_cursor = 0;
            tool_orca_raw_mode = false;
            tool_send_status_bar("Scan Results");
            show_ap_table(s);
            return TOOL_OK;
        }

        if (now - s->last_display_ms > 500) {
            s->last_display_ms = now;
            show_scanning(s);
        }

        if (now - s->start_ms > 20000) {
            tool_orca_raw_send("stopscan\r\n");
            sleep_ms(100);
            s->mode = MODE_AP_TABLE;
            s->out_scroll = 0;
            s->ap_cursor = 0;
            tool_orca_raw_mode = false;
            tool_send_status_bar("Scan Results");
            show_ap_table(s);
        }
    } else {
        if (s->display_dirty && now - s->last_display_ms > 150) {
            s->last_display_ms = now;
            s->display_dirty = false;
            s->out_scroll = s->out_count > 13 ? s->out_count - 13 : 0;
            show_generic_output(s);
        }
    }

    return TOOL_OK;
}

static tool_result_t ghost_on_button(tool_ctx_t *ctx,
                                      uint8_t btn_id, uint8_t btn_state) {
    ghost_state_t *s = (ghost_state_t *)ctx->state;
    if (btn_state != BTN_STATE_PRESSED) return TOOL_OK;

    switch (s->mode) {
    case MODE_CATEGORIES:
        switch (btn_id) {
        case IPP_BTN_YELLOW:
            s->cat_sel = s->cat_sel > 0 ? s->cat_sel - 1 : CAT_COUNT - 1;
            show_categories(s);
            break;
        case IPP_BTN_BLUE:
            s->cat_sel = s->cat_sel < CAT_COUNT - 1 ? s->cat_sel + 1 : 0;
            show_categories(s);
            break;
        case IPP_BTN_GREEN:
            s->mode = MODE_COMMANDS;
            s->cmd_sel = 0;
            s->cmd_scroll = 0;
            tool_send_status_bar(categories[s->cat_sel].name);
            show_commands(s);
            break;
        case IPP_BTN_RED:
            return TOOL_EXIT;
        }
        break;

    case MODE_COMMANDS: {
        const ghost_cat_t *cat = &categories[s->cat_sel];
        switch (btn_id) {
        case IPP_BTN_YELLOW:
            if (s->cmd_sel > 0) {
                s->cmd_sel--;
                if (s->cmd_sel < s->cmd_scroll)
                    s->cmd_scroll = s->cmd_sel;
            } else {
                s->cmd_sel = cat->count - 1;
                s->cmd_scroll = cat->count > 4 ? cat->count - 4 : 0;
            }
            show_commands(s);
            break;
        case IPP_BTN_BLUE:
            if (s->cmd_sel < cat->count - 1) {
                s->cmd_sel++;
                if (s->cmd_sel >= s->cmd_scroll + 4)
                    s->cmd_scroll = s->cmd_sel - 3;
            } else {
                s->cmd_sel = 0;
                s->cmd_scroll = 0;
            }
            show_commands(s);
            break;
        case IPP_BTN_GREEN: {
            const ghost_cmd_t *cmd = &cat->cmds[s->cmd_sel];
            if (cmd->flags == CMD_WIFI_SCAN) {
                start_wifi_scan(s);
            } else {
                s->mode = MODE_OUTPUT;
                s->view_mode = VIEW_GENERIC;
                s->stay_active = (cmd->flags == CMD_STAY_ACTIVE);
                tool_send_status_bar(cmd->label);
                send_ghost_cmd(s, cmd->cmd);
                show_generic_output(s);
            }
            break;
        }
        case IPP_BTN_RED:
            s->mode = MODE_CATEGORIES;
            tool_send_status_bar("GhostESP");
            show_categories(s);
            break;
        }
        break;
    }

    case MODE_OUTPUT:
        switch (btn_id) {
        case IPP_BTN_YELLOW:
            if (s->out_scroll > 0) {
                s->out_scroll--;
                show_generic_output(s);
            }
            break;
        case IPP_BTN_BLUE:
            if (s->out_scroll + 13 < s->out_count) {
                s->out_scroll++;
                show_generic_output(s);
            }
            break;
        case IPP_BTN_RED:
            if (s->view_mode == VIEW_SCANNING) {
                tool_orca_raw_send("stopscan\r\n");
                sleep_ms(300);
                s->mode = MODE_AP_TABLE;
                s->out_scroll = 0;
                s->ap_cursor = 0;
                tool_orca_raw_mode = false;
                tool_send_status_bar("Scan Results");
                show_ap_table(s);
            } else {
                if (s->stay_active) {
                    const ghost_cmd_t *cmd = &categories[s->cat_sel].cmds[s->cmd_sel];
                    const char *c = cmd->cmd;
                    if (strstr(c, "blespam"))
                        tool_orca_raw_send("blespam -s\r\n");
                    else if (strstr(c, "blescan"))
                        tool_orca_raw_send("blescan -s\r\n");
                    else if (strstr(c, "attack -s"))
                        tool_orca_raw_send("stopsaeflood\r\n");
                    else if (strstr(c, "attack"))
                        tool_orca_raw_send("stopdeauth\r\n");
                    else if (strstr(c, "beaconspam") || strstr(c, "beacon"))
                        tool_orca_raw_send("stopspam\r\n");
                    else if (strstr(c, "scan"))
                        tool_orca_raw_send("stopscan\r\n");
                    else if (strstr(c, "dhcpstarve"))
                        tool_orca_raw_send("dhcpstarve stop\r\n");
                    else if (strstr(c, "wdstream"))
                        tool_orca_raw_send("wdstream stop\r\n");
                    sleep_ms(100);
                }
                tool_orca_raw_mode = false;
                s->mode = MODE_COMMANDS;
                tool_send_status_bar(categories[s->cat_sel].name);
                show_commands(s);
            }
            break;
        }
        break;

    case MODE_AP_TABLE:
        switch (btn_id) {
        case IPP_BTN_YELLOW:
            if (s->ap_cursor > 0) {
                s->ap_cursor--;
                if (s->ap_cursor < s->out_scroll)
                    s->out_scroll = s->ap_cursor;
                show_ap_table(s);
            }
            break;
        case IPP_BTN_BLUE:
            if (s->ap_cursor + 1 < s->ap_count) {
                s->ap_cursor++;
                if (s->ap_cursor >= s->out_scroll + 12)
                    s->out_scroll = s->ap_cursor - 11;
                show_ap_table(s);
            }
            break;
        case IPP_BTN_GREEN:
            if (s->ap_count > 0) {
                char sel_cmd[32];
                snprintf(sel_cmd, sizeof(sel_cmd),
                         "select -a %u\r\n", s->ap_cursor);
                tool_orca_raw_mode = true;
                tool_orca_raw_flush();
                tool_orca_raw_send(sel_cmd);
                sleep_ms(200);
                tool_orca_raw_mode = false;
                tool_orca_raw_flush();

                compact_ap_t *ap = &s->aps[s->ap_cursor];
                char toast[36];
                snprintf(toast, sizeof(toast), "Target: %s",
                         ap->ssid[0] ? ap->ssid : "?");
                tool_send_toast(toast, 2000);

                s->cat_sel = 0;
                s->cmd_sel = 1;
                s->cmd_scroll = 0;
                s->mode = MODE_COMMANDS;
                tool_send_status_bar("WiFi");
                show_commands(s);
            }
            break;
        case IPP_BTN_RED:
            s->mode = MODE_COMMANDS;
            tool_send_status_bar(categories[s->cat_sel].name);
            show_commands(s);
            break;
        }
        break;
    }

    return TOOL_OK;
}

static void ghost_exit(tool_ctx_t *ctx) {
    ghost_state_t *s = (ghost_state_t *)ctx->state;
    if (s->mode == MODE_OUTPUT || s->mode == MODE_AP_TABLE) {
        tool_orca_raw_mode = true;
        tool_orca_raw_send("\x03\r\n");
        sleep_ms(50);
        tool_orca_raw_send("stopscan\r\n");
        sleep_ms(50);
        tool_orca_raw_send("stopdeauth\r\n");
        sleep_ms(50);
        tool_orca_raw_send("blespam -s\r\n");
        sleep_ms(50);
        tool_orca_raw_send("blescan -s\r\n");
        sleep_ms(50);
        tool_orca_raw_send("stopspam\r\n");
        sleep_ms(50);
        tool_orca_raw_send("stopsaeflood\r\n");
        sleep_ms(50);
        tool_orca_raw_send("dhcpstarve stop\r\n");
        sleep_ms(50);
        tool_orca_raw_send("wdstream stop\r\n");
        sleep_ms(100);
    }
    tool_orca_raw_mode = false;
    tool_orca_raw_flush();
}

const tool_desc_t tool_ghost_desc = {
    .name       = "GhostESP",
    .icon_id    = 2,
    .requires   = REQUIRE_FPGA,
    .state_size = sizeof(ghost_state_t),
    .enter      = ghost_enter,
    .update     = ghost_update,
    .on_button  = ghost_on_button,
    .exit       = ghost_exit,
};
