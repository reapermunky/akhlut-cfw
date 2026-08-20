/**
 * ipp_defs.h — Inter-Processor Protocol Definitions
 *
 * Akhlut CFW
 * Shared between Main, Display, and Bottlenose firmware.
 *
 * Frame format:
 *   [SYNC:1][SEQ:1][TYPE:1][LENGTH:2-LE][PAYLOAD:0-4096][CRC:2]
 */

#ifndef IPP_DEFS_H
#define IPP_DEFS_H

#include <stdint.h>
#include <stdbool.h>

/* ──────────────────────────────────────────────────────────
 * Frame Constants
 * ────────────────────────────────────────────────────────── */
#define IPP_SYNC_BYTE       0xF1
#define IPP_MAX_PAYLOAD     4096
#define IPP_HEADER_SIZE     5     // SYNC + SEQ + TYPE + LENGTH(2)
#define IPP_CRC_SIZE        2
#define IPP_FRAME_OVERHEAD  (IPP_HEADER_SIZE + IPP_CRC_SIZE)  // 7 bytes
#define IPP_MAX_FRAME_SIZE  (IPP_FRAME_OVERHEAD + IPP_MAX_PAYLOAD)

/* ──────────────────────────────────────────────────────────
 * Message Types — Main → Display (0x01–0x3F)
 * ────────────────────────────────────────────────────────── */
#define IPP_MSG_MENU_SHOW       0x01
#define IPP_MSG_MENU_UPDATE     0x02
#define IPP_MSG_STATUS_BAR      0x03
#define IPP_MSG_TEXT_SCREEN     0x04
#define IPP_MSG_DATA_TABLE      0x05
#define IPP_MSG_GRAPH_DRAW      0x06
#define IPP_MSG_PIXEL_BUFFER    0x07  // Partial region only — see bandwidth constraints
#define IPP_MSG_TOAST           0x08
#define IPP_MSG_PROGRESS        0x09
#define IPP_MSG_LED_SET         0x0A
#define IPP_MSG_LED_PATTERN     0x0B
#define IPP_MSG_AUDIO_PLAY      0x0C
#define IPP_MSG_AUDIO_STOP      0x0D
#define IPP_MSG_SCREEN_CLEAR    0x0E
#define IPP_MSG_BACKLIGHT       0x0F
#define IPP_MSG_IR_SEND         0x10
#define IPP_MSG_SPLASH          0x11
#define IPP_MSG_DIALOG          0x12
#define IPP_MSG_INPUT_PROMPT    0x13
#define IPP_MSG_DRAW_RECT       0x14
#define IPP_MSG_DRAW_LINE       0x15
#define IPP_MSG_DRAW_TEXT       0x16
#define IPP_MSG_DRAW_CIRCLE     0x17
#define IPP_MSG_DRAW_BATCH      0x18
#define IPP_MSG_FB_FLIP         0x19  // Commit draw ops to screen
#define IPP_MSG_REBOOT_BOOTLOADER 0x1A  // Tell Display to enter UF2 bootloader
#define IPP_MSG_RESET_DISPLAY   0x1B    // Software reset Display RP2040
#define IPP_MSG_I2C_SCAN_REQ   0x1C    // Request Display to scan its local I2C bus
#define IPP_MSG_IOEXP_WRITE    0x1D    // Write PCA9555 register: {reg, value}
#define IPP_MSG_IOEXP_READ     0x1E    // Read PCA9555 register: {reg}
#define IPP_MSG_ACCEL_REQ      0x1F    // Request accelerometer data from Display

/* ──────────────────────────────────────────────────────────
 * Message Types — Display → Main (0x81–0xBF)
 * ────────────────────────────────────────────────────────── */
#define IPP_MSG_BUTTON_EVENT    0x81
#define IPP_MSG_ACCEL_DATA      0x82
#define IPP_MSG_BATTERY_DATA    0x83
#define IPP_MSG_RTC_TIME        0x84
#define IPP_MSG_IR_RECEIVED     0x85
#define IPP_MSG_AUDIO_DONE      0x86
#define IPP_MSG_DIALOG_RESULT   0x87
#define IPP_MSG_RENDER_READY    0x88
#define IPP_MSG_I2C_SCAN_RESP  0x89    // Display → Main: local I2C scan results
#define IPP_MSG_IOEXP_RESP     0x8A    // Display → Main: {reg, value, status(0=ok)}

/* ──────────────────────────────────────────────────────────
 * Message Types — Main → Bottlenose (0x41–0x5F)
 * ────────────────────────────────────────────────────────── */
#define IPP_MSG_WIFI_SCAN_START     0x41
#define IPP_MSG_WIFI_SCAN_STOP      0x42
#define IPP_MSG_WIFI_MONITOR        0x43
#define IPP_MSG_WIFI_DEAUTH_DETECT  0x44
#define IPP_MSG_WIFI_PROBE_LISTEN   0x45
#define IPP_MSG_BLE_SCAN_START      0x46
#define IPP_MSG_BLE_SCAN_STOP       0x47
#define IPP_MSG_BLE_CONNECT         0x48
#define IPP_MSG_BLE_GATT_DISCOVER   0x49
#define IPP_MSG_BLE_DISCONNECT      0x4A
#define IPP_MSG_PING                0x4B
#define IPP_MSG_VERSION_GET         0x4C
#define IPP_MSG_ORCA_RESET          0x4D

/* ──────────────────────────────────────────────────────────
 * Message Types — Bottlenose → Main (0xC1–0xDF)
 * ────────────────────────────────────────────────────────── */
#define IPP_MSG_WIFI_AP_FOUND       0xC1
#define IPP_MSG_WIFI_SCAN_DONE      0xC2
#define IPP_MSG_WIFI_FRAME          0xC3
#define IPP_MSG_WIFI_DEAUTH_ALERT   0xC4
#define IPP_MSG_WIFI_PROBE          0xC5
#define IPP_MSG_BLE_ADV_FOUND       0xC6
#define IPP_MSG_BLE_SCAN_DONE       0xC7
#define IPP_MSG_BLE_GATT_RESULT     0xC8
#define IPP_MSG_BLE_DATA            0xC9
#define IPP_MSG_PONG                0xCA
#define IPP_MSG_VERSION             0xCB
#define IPP_MSG_ORCA_ERROR          0xCC

/* ──────────────────────────────────────────────────────────
 * Frame Structure
 * ────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t  seq;
    uint8_t  type;
    uint16_t length;
    uint8_t  payload[IPP_MAX_PAYLOAD];
    uint16_t crc;
} ipp_frame_t;

/* ──────────────────────────────────────────────────────────
 * Payload Structures — Display Events → Main
 * ────────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  button_id;   // BTN_ID_GRAY .. BTN_ID_RED
    uint8_t  state;       // RELEASED / PRESSED / HELD
    uint32_t hold_ms;     // Duration if HELD, 0 otherwise
} ipp_button_event_t;

// Button IDs for ipp_button_event_t.button_id
#define IPP_BTN_GRAY    0
#define IPP_BTN_YELLOW  1
#define IPP_BTN_GREEN   2
#define IPP_BTN_BLUE    3
#define IPP_BTN_RED     4

typedef struct __attribute__((packed)) {
    int16_t  x;           // mg (milli-g)
    int16_t  y;
    int16_t  z;
    uint16_t g_total;     // Total acceleration magnitude
    int16_t  temp_c_x10;  // Temperature × 10 (e.g., 235 = 23.5°C)
} ipp_accel_data_t;

typedef struct __attribute__((packed)) {
    uint16_t vbatt_mv;    // Battery voltage (mV)
    uint16_t vbus_mv;     // USB bus voltage (mV)
    uint16_t vsys_mv;     // System voltage (mV)
    uint16_t ichg_ma;     // Charge current (mA)
    uint8_t  flags;       // Bit 0: charging, Bit 1: charge_complete
} ipp_battery_data_t;

#define BATTERY_FLAG_CHARGING   (1 << 0)
#define BATTERY_FLAG_COMPLETE   (1 << 1)
#define BATTERY_FLAG_VBUS_GD    (1 << 2)

/* ──────────────────────────────────────────────────────────
 * Payload Structures — Main → Display
 * ────────────────────────────────────────────────────────── */

// Menu item (variable-length: fixed header + null-terminated name)
typedef struct __attribute__((packed)) {
    uint8_t  icon_id;     // Icon index (0 = none)
    uint8_t  flags;       // Bit 0: disabled, Bit 1: has_submenu indicator
    // Followed by: null-terminated name string
} ipp_menu_item_header_t;

// Menu show payload: [selected_index:1][item_count:1][items...]
// Each item is: [ipp_menu_item_header_t][name_string\0]

typedef struct __attribute__((packed)) {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t color;       // RGB565
    uint8_t  filled;      // 0 = outline, 1 = filled
} ipp_draw_rect_t;

typedef struct __attribute__((packed)) {
    uint16_t x0;
    uint16_t y0;
    uint16_t x1;
    uint16_t y1;
    uint16_t color;       // RGB565
} ipp_draw_line_t;

typedef struct __attribute__((packed)) {
    uint16_t x;
    uint16_t y;
    uint8_t  font_id;     // 0 = small, 1 = medium, 2 = large
    uint16_t color;       // RGB565
    // Followed by: null-terminated string
} ipp_draw_text_header_t;

typedef struct __attribute__((packed)) {
    uint8_t  percent;     // 0-100
    // Followed by: null-terminated label string
} ipp_progress_t;

typedef struct __attribute__((packed)) {
    uint16_t duration_ms; // How long to show (0 = until dismissed)
    // Followed by: null-terminated text
} ipp_toast_header_t;

typedef struct __attribute__((packed)) {
    uint8_t  led_index;   // 0-6
    uint8_t  r;
    uint8_t  g;
    uint8_t  b;
} ipp_led_set_t;

typedef struct __attribute__((packed)) {
    uint8_t  count;
    uint8_t  addrs[16];
} ipp_i2c_scan_resp_t;

typedef struct __attribute__((packed)) {
    uint8_t  brightness;  // 0-255
} ipp_backlight_t;

typedef struct __attribute__((packed)) {
    uint16_t cx;
    uint16_t cy;
    uint16_t r;
    uint16_t color;       // RGB565
    uint8_t  filled;      // 0 = outline, 1 = filled
} ipp_draw_circle_t;

/* ──────────────────────────────────────────────────────────
 * Payload Structures — Main → Display: Status Bar
 * ────────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  battery_pct; // 0-100
    uint8_t  flags;       // Bit 0: charging, Bit 1: radio1_ok, Bit 2: radio2_ok,
                          // Bit 3: orca_ok, Bit 4: usb_connected
    int8_t   rssi_radio1; // dBm (0 = inactive)
    int8_t   rssi_radio2;
    // Followed by: null-terminated tool name (shown in status bar)
} ipp_status_bar_t;

#define STATUS_FLAG_CHARGING    (1 << 0)
#define STATUS_FLAG_RADIO1_OK   (1 << 1)
#define STATUS_FLAG_RADIO2_OK   (1 << 2)
#define STATUS_FLAG_ORCA_OK     (1 << 3)
#define STATUS_FLAG_USB         (1 << 4)

/* ──────────────────────────────────────────────────────────
 * Payload Structures — Bottlenose
 * ────────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  bssid[6];
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  auth_type;   // 0=Open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3, 5=Enterprise
    uint8_t  band;        // 0 = 2.4 GHz, 1 = 5 GHz
    // Followed by: null-terminated SSID string
} ipp_wifi_ap_header_t;

typedef struct __attribute__((packed)) {
    uint8_t  mac[6];
    int8_t   rssi;
    // Followed by: null-terminated SSID being probed
} ipp_wifi_probe_t;

typedef struct __attribute__((packed)) {
    uint8_t  bssid[6];
    uint8_t  source_mac[6];
    uint16_t count;       // Number of deauths seen
    uint8_t  reason;      // 802.11 reason code
} ipp_wifi_deauth_t;

typedef struct __attribute__((packed)) {
    uint8_t  mac[6];
    int8_t   rssi;
    uint8_t  addr_type;   // 0 = public, 1 = random
    uint8_t  adv_type;    // Connectable, scannable, etc.
    uint8_t  adv_data_len;
    // Followed by: adv_data bytes, then null-terminated name (empty if none)
} ipp_ble_adv_header_t;

typedef struct __attribute__((packed)) {
    uint32_t uptime_sec;
    uint32_t free_heap;
} ipp_pong_t;

/* ──────────────────────────────────────────────────────────
 * WiFi Monitor Filter (sent with WIFI_MONITOR command)
 * ────────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  channel;
    uint8_t  frame_type_mask;  // Bit 0: mgmt, Bit 1: ctrl, Bit 2: data
    int8_t   rssi_floor;       // Ignore frames below this RSSI
    uint8_t  mac_filter_count; // 0 = accept all, >0 = whitelist
    // Followed by: mac_filter_count × 6-byte MACs
} ipp_wifi_monitor_filter_t;

#define WIFI_FRAME_MGMT     (1 << 0)
#define WIFI_FRAME_CTRL     (1 << 1)
#define WIFI_FRAME_DATA     (1 << 2)

/* ──────────────────────────────────────────────────────────
 * Payload Structures — IR
 * ────────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  protocol;    // 0=NEC, 1=Sony, 2=RC5, 3=RC6, 4=Samsung
    uint32_t code;
    uint8_t  bits;
} ipp_ir_code_t;

#define IR_PROTO_NEC     0
#define IR_PROTO_SONY    1
#define IR_PROTO_RC5     2
#define IR_PROTO_RC6     3
#define IR_PROTO_SAMSUNG 4

#endif // IPP_DEFS_H
